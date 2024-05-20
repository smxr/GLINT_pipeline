#include <cuda.h>
#include "mygpu.h"
#include "cuda_util.cuh"
#include "hilbert_curve.cuh"
#include "../geometry/geometry.h"
#include "../util/query_context.h"
#include "../tracing/partitioner.h"
#include "../tracing/workbench.h"
#include <thrust/host_vector.h>
#include <thrust/device_vector.h>
#include <thrust/generate.h>
#include <thrust/sort.h>
#include <thrust/copy.h>

/*
 *
 * some utility functions
 *
 * */

__device__
inline double height(box *b){
	return (b->high[1]-b->low[1])/degree_per_meter_latitude_cuda;
}

__device__
inline double distance(box *b,Point *p){

	double dx = max(abs(p->x-(b->low[0]+b->high[0])/2) - (b->high[0]-b->low[0])/2, 0.0);
	double dy = max(abs(p->y-(b->low[1]+b->high[1])/2) - (b->high[1]-b->low[1])/2, 0.0);
	dy = dy/degree_per_meter_latitude_cuda;
	dx = dx/degree_per_meter_longitude_cuda(p->y);

	return sqrt(dx * dx + dy * dy);
}

__device__
inline bool contain(box *b, Point *p){            //?? double  bool
	return p->x>=b->low[0]&&
		   p->x<=b->high[0]&&
		   p->y>=b->low[1]&&
		   p->y<=b->high[1];
}

__device__
inline bool box_contain(box *b, box *target){            //?? double  bool
    return target->low[0]>=b->low[0]&&
           target->high[0]<=b->high[0]&&
           target->low[1]>=b->low[1]&&
           target->high[1]<=b->high[1];
}

__device__
inline void print_box_point(box *b, Point *p){
	printf("POLYGON((%f %f, %f %f, %f %f, %f %f, %f %f))\nPOINT(%f %f)\n",
						b->low[0],b->low[1],
						b->high[0],b->low[1],
						b->high[0],b->high[1],
						b->low[0],b->high[1],
						b->low[0],b->low[1],
						p->x,p->y);
}

__device__
inline void print_box(f_box *b){
	printf("POLYGON((%f %f, %f %f, %f %f, %f %f, %f %f))\n",
						b->low[0],b->low[1],
						b->high[0],b->low[1],
						b->high[0],b->high[1],
						b->low[0],b->high[1],
						b->low[0],b->low[1]);
}

__device__
inline void mbr_update(box &mbr, Point *p){
    if(mbr.low[0]>p->x){
        mbr.low[0] = p->x;
    }
    if(mbr.high[0]<p->x){
        mbr.high[0] = p->x;
    }

    if(mbr.low[1]>p->y){
        mbr.low[1] = p->y;
    }
    if(mbr.high[1]<p->y){
        mbr.high[1] = p->y;
    }
}

__device__
inline void print_point(Point *p){
	printf("Point(%f %f)\n",p->x,p->y);
}

//__device__
//__uint128_t box_to_128(box *b){
//    return ((__uint128_t)float_to_uint(b->low[0]) << 66) + ((__uint128_t)float_to_uint(b->low[1]) << 44) + ((__uint128_t)float_to_uint(b->high[0]) << 22) + ((__uint128_t)float_to_uint(b->high[1]));
//}

__device__
void write_kv_box(f_box * kv_b, box * meeting_b){
    kv_b->low[0] = meeting_b->low[0];
    kv_b->low[1] = meeting_b->low[1];
    kv_b->high[0] = meeting_b->high[0];
    kv_b->high[1] = meeting_b->high[1];
}

//first hilbert
__device__
void update_wid(unsigned short & hilbert_low, uint edge_length, uint low0, uint low1){
    if(hilbert_low){
        uint old_low0, old_low1;
        d2xy(edge_length, hilbert_low, old_low0, old_low1);
        //uint new_low0 = min(old_low0, low0), new_low1 = min(old_low1, low1);            //always the left bottom
        uint new_low0 = old_low0/2 + low0/2, new_low1 = old_low1/2 + low1/2;            //nearly the centroid
        hilbert_low = xy2d(edge_length, new_low0, new_low1);
        if(!hilbert_low){                                                               //0 used to be ambiguous, now 0 -> not appear, 1 is ambiguous
            hilbert_low = 1;
        }
    }
    else{
        hilbert_low = xy2d(edge_length, low0, low1);
        if(!hilbert_low){
            hilbert_low = 1;
        }
    }
}

__host__ __device__
float uint_to_float(uint f){
    float ret = (float)f/10000;
    ret -= 180;
    return ret;
}

__host__ __device__
uint get_key_wid(__uint128_t key){
    //return (uint)((key >> (PID_BIT*2 + MBR_BIT + DURATION_BIT + END_BIT)) & ((1ULL << WID_BIT) - 1));
    return (uint)(key >> (PID_BIT*2 + MBR_BIT + DURATION_BIT + END_BIT));
}

__host__ __device__
uint get_key_pid(__uint128_t key){
    return (uint)((key >> (PID_BIT + MBR_BIT + DURATION_BIT + END_BIT)) & ((1ULL << PID_BIT) - 1));
}

__host__ __device__
uint get_key_target(__uint128_t key){
    return (uint)((key >> (MBR_BIT + DURATION_BIT + END_BIT)) & ((1ULL << PID_BIT) - 1));
}

__host__ __device__
uint64_t get_key_mbr_code(__uint128_t key){
    return (uint64_t)((key >> ( DURATION_BIT + END_BIT)) & ((1ULL << MBR_BIT) - 1));
}

__host__ __device__
uint get_key_duration(__uint128_t key){
    return (uint)((key >> END_BIT) & ((1ULL << DURATION_BIT) - 1));
}

__host__ __device__
uint get_key_end(__uint128_t key){
    return (uint)(key & ((1ULL << END_BIT) - 1));
}

__host__ __device__
uint64_t serialize_mbr(box* b, box* bitmap_mbr){
    uint64_t low0 = (b->low[0] - bitmap_mbr->low[0])/(bitmap_mbr->high[0] - bitmap_mbr->low[0]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t low1 = (b->low[1] - bitmap_mbr->low[1])/(bitmap_mbr->high[1] - bitmap_mbr->low[1]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t high0 = (b->high[0] - bitmap_mbr->low[0])/(bitmap_mbr->high[0] - bitmap_mbr->low[0]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t high1 = (b->high[1] - bitmap_mbr->low[1])/(bitmap_mbr->high[1] - bitmap_mbr->low[1]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t value_mbr = ((uint64_t)low0 << (MBR_BIT/4*3)) + ((uint64_t)low1 << (MBR_BIT/2)) + ((uint64_t)high0 << (MBR_BIT/4)) + (uint64_t)high1;
    return value_mbr;
}

__host__ __device__
uint64_t serialize_mbr(f_box* b, box* bitmap_mbr){
    uint64_t low0 = (b->low[0] - bitmap_mbr->low[0])/(bitmap_mbr->high[0] - bitmap_mbr->low[0]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t low1 = (b->low[1] - bitmap_mbr->low[1])/(bitmap_mbr->high[1] - bitmap_mbr->low[1]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t high0 = (b->high[0] - bitmap_mbr->low[0])/(bitmap_mbr->high[0] - bitmap_mbr->low[0]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t high1 = (b->high[1] - bitmap_mbr->low[1])/(bitmap_mbr->high[1] - bitmap_mbr->low[1]) * ((1ULL << (MBR_BIT/4)) - 1);
    uint64_t value_mbr = ((uint64_t)low0 << (MBR_BIT/4*3)) + ((uint64_t)low1 << (MBR_BIT/2)) + ((uint64_t)high0 << (MBR_BIT/4)) + (uint64_t)high1;
    return value_mbr;
}

/*
 *
 * kernel functions
 *
 * */

__global__
void cuda_cleargrids(workbench *bench){
	int gid = blockIdx.x*blockDim.x+threadIdx.x;
	if(gid>=bench->grids_stack_capacity){
		return;
	}
	bench->grid_counter[gid] = 0;
}

__global__
void cuda_reset_bench(workbench *bench){
	bench->grid_check_counter = 0;
	//bench->meeting_counter = 0;
	bench->num_active_meetings = 0;
	bench->num_taken_buckets = 0;
	bench->filter_list_index = 0;
	bench->split_list_index = 0;
	bench->merge_list_index = 0;
}

__global__
void cuda_clean_buckets(workbench *bench){
	size_t bid = blockIdx.x*blockDim.x+threadIdx.x;
	if(bid>=bench->config->num_meeting_buckets){
		return;
	}
	bench->meeting_buckets[bid].key = ULL_MAX;
}

//  partition with cuda
__global__
void cuda_partition(workbench *bench){
	int pid = blockIdx.x*blockDim.x+threadIdx.x;
	if(pid>=bench->config->num_objects){
		return;
	}

	// search the tree to get in which grid
	uint curnode = 0;
	uint gid = 0;

	Point *p = bench->points+pid;
	uint last_valid = 0;
	while(true){
		int loc = (p->y>bench->schema[curnode].mid_y)*2 + (p->x>bench->schema[curnode].mid_x);
		curnode = bench->schema[curnode].children[loc];

		// not near the right and top border
		if(p->x+bench->config->x_buffer<bench->schema[curnode].mbr.high[0]&&
		   p->y+bench->config->y_buffer<bench->schema[curnode].mbr.high[1]){
			last_valid = curnode;
		}

		// is leaf
		if(bench->schema[curnode].type==LEAF){
			gid = bench->schema[curnode].grid_id;
			break;
		}
	}

	// insert current pid to proper memory space of the target gid
	// todo: consider the situation that grid buffer is too small
	uint *cur_grid = bench->grids+bench->grid_capacity*gid;
	uint cur_loc = atomicAdd(bench->grid_counter+gid,1);
	if(cur_loc<bench->grid_capacity){
		*(cur_grid+cur_loc) = pid;
	}
	uint glid = atomicAdd(&bench->grid_check_counter,1);
	bench->grid_check[glid].pid = pid;
	bench->grid_check[glid].gid = gid;
	bench->grid_check[glid].offset = 0;
	bench->grid_check[glid].inside = true;

	if(last_valid!=curnode){
		uint stack_index = atomicAdd(&bench->filter_list_index,1);
		assert(stack_index<bench->filter_list_capacity);
		bench->filter_list[stack_index*2] = pid;
		bench->filter_list[stack_index*2+1] = last_valid;
	}

}


/*
 *
 * functions for filtering
 *
 * */

#define PER_STACK_SIZE 5

__global__
void cuda_pack_lookup(workbench *bench){
	int pid = blockIdx.x*blockDim.x+threadIdx.x;
	if(pid>=bench->config->num_objects){
		return;
	}

	uint idx = atomicAdd(&bench->filter_list_index,1);
	assert(idx<bench->filter_list_capacity);
	bench->filter_list[idx*2] = pid;
	bench->filter_list[idx*2+1] = 0;
}

__global__
void cuda_filtering(workbench *bench, int start_idx, int batch_size, bool include_contain){
	int cur_idx = blockIdx.x*blockDim.x+threadIdx.x;
	int idx = cur_idx + start_idx;
	if(cur_idx>=batch_size){
		return;
	}
	int pid = bench->filter_list[idx*2];
	int nodeid = bench->filter_list[idx*2+1];

	// get the block shared stack
	int block_stack_size = 1024*2*PER_STACK_SIZE;
	int stack_offset = blockIdx.x*block_stack_size;

	assert(stack_offset+block_stack_size<bench->tmp_space_capacity);

	int *cur_stack_idx = (int *)bench->tmp_space+stack_offset;
	int *cur_worker_idx = (int *)bench->tmp_space+stack_offset+1;
	uint *cur_stack = bench->tmp_space+stack_offset+2;

	*cur_stack_idx = 0;
	*cur_worker_idx = 0;
	__syncthreads();

	int stack_index = atomicAdd(cur_stack_idx, 1);
	cur_stack[2*stack_index] = pid;
	cur_stack[2*stack_index+1] = nodeid;

	//printf("%d:\tinit push %d\n",threadIdx.x,stack_index);
	__syncthreads();

	while(true){
		bool busy = false;
		stack_index = atomicSub(cur_stack_idx, 1)-1;
		//printf("%d:\tpop %d\n",threadIdx.x, stack_index);
		__syncthreads();
		if(stack_index<0){
			stack_index = atomicAdd(cur_stack_idx, 1);
			//printf("%d:\tinc %d\n",threadIdx.x, stack_index);
		}else{
			busy = true;
			atomicAdd(cur_worker_idx, 1);
		}
		__syncthreads();

		//printf("num workers: %d\n",*cur_worker_idx);
		if(*cur_worker_idx==0){
			break;
		}
		if(busy){
			uint pid = cur_stack[2*stack_index];
			uint curnode = cur_stack[2*stack_index+1];
			Point *p = bench->points+pid;
			//printf("process: %d %d %d\n",stack_index,pid,curnode);

			for(int i=0;i<4;i++){
				uint child_offset = bench->schema[curnode].children[i];
				double dist = distance(&bench->schema[child_offset].mbr, p);
				if(dist<=bench->config->reach_distance){
					if(bench->schema[child_offset].type==LEAF){
						uint gid = bench->schema[child_offset].grid_id;
						assert(gid<bench->grids_stack_capacity);
						if(include_contain&&contain(&bench->schema[child_offset].mbr,p)){
							uint *cur_grid = bench->grids+bench->grid_capacity*gid;
							uint cur_loc = atomicAdd(bench->grid_counter+gid,1);
							if(cur_loc<bench->grid_capacity){
								*(cur_grid+cur_loc) = pid;
							}
							uint glid = atomicAdd(&bench->grid_check_counter,1);
							assert(glid<bench->grid_check_capacity);
							bench->grid_check[glid].pid = pid;
							bench->grid_check[glid].gid = gid;
							bench->grid_check[glid].offset = 0;
							bench->grid_check[glid].inside = true;
						}else if(p->y<bench->schema[child_offset].mbr.low[1]||
						   (p->y<bench->schema[child_offset].mbr.high[1]
							&& p->x<bench->schema[child_offset].mbr.low[0])){
							uint glid = atomicAdd(&bench->grid_check_counter,1);
							assert(glid<bench->grid_check_capacity);
							bench->grid_check[glid].pid = pid;
							bench->grid_check[glid].gid = gid;
							bench->grid_check[glid].offset = 0;
							bench->grid_check[glid].inside = false;
						}
					}else{
						stack_index = atomicAdd(cur_stack_idx, 1);
						//printf("%d:\tnew push %d\n",threadIdx.x,stack_index);
						assert(stack_index<PER_STACK_SIZE*1024);
						cur_stack[2*stack_index] = pid;
						cur_stack[2*stack_index+1] = child_offset;
					}
				}
			}
			atomicSub(cur_worker_idx, 1);
		}
		__syncthreads();
	}
}


/*
 *
 * kernel functions for the refinement step
 *
 * */

__global__
void cuda_unroll(workbench *bench, uint inistial_size){
	int glid = blockIdx.x*blockDim.x+threadIdx.x;
	if(glid>=inistial_size){
		return;
	}

	uint grid_size = min(bench->grid_counter[bench->grid_check[glid].gid],bench->grid_capacity);
	// the first batch already inserted during the partition and lookup steps
	uint offset = bench->config->zone_capacity;
	while(offset<grid_size){
		uint cu_index = atomicAdd(&bench->grid_check_counter, 1);
		if(cu_index>=bench->grid_check_capacity){
			printf("%d %d %d\n",bench->grid_counter[bench->grid_check[glid].gid],cu_index,bench->grid_check_capacity);
		}
		//assert(cu_index<bench->grid_check_capacity);
		bench->grid_check[cu_index] = bench->grid_check[glid];
		bench->grid_check[cu_index].offset = offset;
		offset += bench->config->zone_capacity;
	}
}


__global__
void cuda_refinement(workbench *bench){

    // the objects in which grid need be processed
    int loc = threadIdx.y;
    int pairid = blockIdx.x*blockDim.x+threadIdx.x;
    if(pairid>=bench->grid_check_counter){
        return;
    }

    uint gid = bench->grid_check[pairid].gid;
    uint offset = bench->grid_check[pairid].offset;

    uint size = min(bench->grid_counter[gid],bench->grid_capacity)-offset;
    if(bench->config->unroll && size>bench->config->zone_capacity){
        size = bench->config->zone_capacity;
    }
    if(loc>=size){
        return;
    }
    uint pid = bench->grid_check[pairid].pid;
    uint target_pid = *(bench->grids+bench->grid_capacity*gid+offset+loc);
    if(!bench->grid_check[pairid].inside||pid<target_pid){
//        Point *p1 = &bench->points[pid];
//        Point *p2 = &bench->points[target_pid];
        Point *p1 = bench->points+pid;
        Point *p2 = bench->points+target_pid;
        double dist = distance(bench->points[pid].x, bench->points[pid].y, bench->points[target_pid].x, bench->points[target_pid].y);
        if(dist<=bench->config->reach_distance){
            uint pid1 = min(pid,target_pid);
            uint pid2 = max(target_pid,pid);
            size_t key = ((size_t)pid1+pid2)*(pid1+pid2+1)/2+pid2;
            size_t slot = key%bench->config->num_meeting_buckets;
            int ite = 0;
            while (ite++<5){
                unsigned long long prev = atomicCAS((unsigned long long *)&bench->meeting_buckets[slot].key, ULL_MAX, (unsigned long long)key);
                //printf("%ld\n",prev,ULL_MAX,bench->meeting_buckets[bench->current_bucket][slot].key);
                if(prev == key){
                    bench->meeting_buckets[slot].end = bench->cur_time;
                    //mbr_update(bench->meeting_buckets[slot].mbr, bench->points[pid]);                     //"Point::~Point"
                    mbr_update(bench->meeting_buckets[slot].mbr, p1);
                    mbr_update(bench->meeting_buckets[slot].mbr, p2);
                    break;
                }else if (prev == ULL_MAX){
                    bench->meeting_buckets[slot].key = key;
                    bench->meeting_buckets[slot].start = bench->cur_time;
                    bench->meeting_buckets[slot].end = bench->cur_time;
                    bench->meeting_buckets[slot].mbr.low[0] = 100000.0;
                    bench->meeting_buckets[slot].mbr.low[1] = 100000.0;
                    bench->meeting_buckets[slot].mbr.high[0] = -100000.0;
                    bench->meeting_buckets[slot].mbr.high[1] = -100000.0;
                    mbr_update(bench->meeting_buckets[slot].mbr, p1);
                    mbr_update(bench->meeting_buckets[slot].mbr, p2);

                    break;
                }
                slot = (slot + 1)%bench->config->num_meeting_buckets;
            }
        }
    }
}

__global__
void cuda_refinement_unroll(workbench *bench, uint offset){

	// the objects in which grid need be processed
	int loc = threadIdx.y;
	int pairid = blockIdx.x*blockDim.x+threadIdx.x;
	if(pairid>=bench->grid_check_counter){
		return;
	}

	uint gid = bench->grid_check[pairid].gid;

	uint size = min(bench->grid_counter[gid],bench->grid_capacity);
	if(loc+offset>=size){
		return;
	}
	uint pid = bench->grid_check[pairid].pid;
	uint target_pid = *(bench->grids+bench->grid_capacity*gid+offset+loc);
	if(!bench->grid_check[pairid].inside||pid<target_pid){
		double dist = distance(bench->points[pid].x, bench->points[pid].y, bench->points[target_pid].x, bench->points[target_pid].y);
		if(dist<=bench->config->reach_distance){
			uint pid1 = min(pid,target_pid);
			uint pid2 = max(target_pid,pid);
			size_t key = ((size_t)pid1+pid2)*(pid1+pid2+1)/2+pid2;
			size_t slot = key%bench->config->num_meeting_buckets;
			int ite = 0;
			while (ite++<5){
				unsigned long long prev = atomicCAS((unsigned long long *)&bench->meeting_buckets[slot].key, ULL_MAX, (unsigned long long)key);
				//printf("%ld\n",prev,ULL_MAX,bench->meeting_buckets[bench->current_bucket][slot].key);
				if(prev == key){
					bench->meeting_buckets[slot].end = bench->cur_time;
					break;
				}else if (prev == ULL_MAX){
					bench->meeting_buckets[slot].key = key;
					bench->meeting_buckets[slot].start = bench->cur_time;
					bench->meeting_buckets[slot].end = bench->cur_time;
					break;
				}
				slot = (slot + 1)%bench->config->num_meeting_buckets;
			}
		}
	}
}

/*
 * kernel function for identify completed meetings
 *
 * */

__global__
void cuda_profile_meetings(workbench *bench){

	size_t bid = blockIdx.x*blockDim.x+threadIdx.x;
	if(bid>=bench->config->num_meeting_buckets){
		return;
	}
	// empty
	if(bench->meeting_buckets[bid].key==ULL_MAX){
		return;
	}
	if(bench->config->profile){
		atomicAdd((unsigned long long *)&bench->num_taken_buckets, (unsigned long long)1);
	}
	// is still active
	if(bench->meeting_buckets[bid].end==bench->cur_time){
		if(bench->config->profile){
			atomicAdd((unsigned long long *)&bench->num_active_meetings, (unsigned long long)1);
		}
		return;
	}
}

__global__
void cuda_identify_meetings(workbench *bench) {
    size_t bid = blockIdx.x * blockDim.x + threadIdx.x;
    if (bid >= bench->config->num_meeting_buckets) {
        return;
    }
    // empty
    if (bench->meeting_buckets[bid].key == ULL_MAX) {
        return;
    }
    // is still active
    bool meet_cut = false;
    if (bench->meeting_buckets[bid].end == bench->cur_time) {
        if(bench->meeting_buckets[bid].end - bench->meeting_buckets[bid].start >= bench->config->max_meet_time){
            meet_cut = true;
            atomicAdd(&bench->meeting_cut_count, 1);
        }
        if(bench->search_single) {
            if (bench->cur_time - bench->meeting_buckets[bid].start >= bench->config->min_meet_time + 1) {
                if (bench->search_single_pid == getpid1(bench->meeting_buckets[bid].key)) {
                    uint meeting_idx = atomicAdd(&bench->single_find_count, 1);
                    assert(bench->single_find_count < bench->config->search_single_capacity);
                    //bench->search_multi_list[meeting_idx].pid = bench->search_single_pid;
                    bench->search_single_list[meeting_idx].target = getpid2(bench->meeting_buckets[bid].key);
                    bench->search_single_list[meeting_idx].start = bench->meeting_buckets[bid].start;
                    bench->search_single_list[meeting_idx].end = bench->meeting_buckets[bid].end;                  //real end
                    bench->search_single_list[meeting_idx].low0 = bench->meeting_buckets[bid].mbr.low[0];
                    bench->search_single_list[meeting_idx].low1 = bench->meeting_buckets[bid].mbr.low[1];
                    bench->search_single_list[meeting_idx].high0 = bench->meeting_buckets[bid].mbr.high[0];
                    bench->search_single_list[meeting_idx].high1 = bench->meeting_buckets[bid].mbr.high[1];
                }
                else if (bench->search_single_pid == getpid2(bench->meeting_buckets[bid].key)) {
                    uint meeting_idx = atomicAdd(&bench->single_find_count, 1);
                    assert(bench->single_find_count < bench->config->search_single_capacity);
                    bench->search_single_list[meeting_idx].target = getpid1(bench->meeting_buckets[bid].key);
                    bench->search_single_list[meeting_idx].start = bench->meeting_buckets[bid].start;
                    bench->search_single_list[meeting_idx].end = bench->meeting_buckets[bid].end;                  //real end
                    bench->search_single_list[meeting_idx].low0 = bench->meeting_buckets[bid].mbr.low[0];
                    bench->search_single_list[meeting_idx].low1 = bench->meeting_buckets[bid].mbr.low[1];
                    bench->search_single_list[meeting_idx].high0 = bench->meeting_buckets[bid].mbr.high[0];
                    bench->search_single_list[meeting_idx].high1 = bench->meeting_buckets[bid].mbr.high[1];
                }
            }
        }
        if(bench->search_multi) {
            if (bench->cur_time - bench->meeting_buckets[bid].start >= bench->config->min_meet_time + 1) {
                for(int i = 0;i<bench->search_multi_length;i++){
                    if (bench->search_multi_pid[i] == getpid1(bench->meeting_buckets[bid].key)) {
                        uint meeting_idx = atomicAdd(&bench->multi_find_count, 1);
                        assert(bench->multi_find_count < bench->config->search_multi_capacity);
                        bench->search_multi_list[meeting_idx].pid = bench->search_multi_pid[i];
                        bench->search_multi_list[meeting_idx].target = getpid2(bench->meeting_buckets[bid].key);
                        bench->search_multi_list[meeting_idx].start = bench->meeting_buckets[bid].start;
                        bench->search_multi_list[meeting_idx].end = bench->meeting_buckets[bid].end;                  //real end
                        bench->search_multi_list[meeting_idx].low0 = bench->meeting_buckets[bid].mbr.low[0];
                        bench->search_multi_list[meeting_idx].low1 = bench->meeting_buckets[bid].mbr.low[1];
                        bench->search_multi_list[meeting_idx].high0 = bench->meeting_buckets[bid].mbr.high[0];
                        bench->search_multi_list[meeting_idx].high1 = bench->meeting_buckets[bid].mbr.high[1];
                    }
                    if (bench->search_multi_pid[i] == getpid2(bench->meeting_buckets[bid].key)) {
                        uint meeting_idx = atomicAdd(&bench->multi_find_count, 1);
                        assert(bench->multi_find_count < bench->config->search_multi_capacity);
                        bench->search_multi_list[meeting_idx].pid = bench->search_multi_pid[i];
                        bench->search_multi_list[meeting_idx].target = getpid1(bench->meeting_buckets[bid].key);
                        bench->search_multi_list[meeting_idx].start = bench->meeting_buckets[bid].start;
                        bench->search_multi_list[meeting_idx].end = bench->meeting_buckets[bid].end;                  //real end
                        bench->search_multi_list[meeting_idx].low0 = bench->meeting_buckets[bid].mbr.low[0];
                        bench->search_multi_list[meeting_idx].low1 = bench->meeting_buckets[bid].mbr.low[1];
                        bench->search_multi_list[meeting_idx].high0 = bench->meeting_buckets[bid].mbr.high[0];
                        bench->search_multi_list[meeting_idx].high1 = bench->meeting_buckets[bid].mbr.high[1];
                    }
                }
            }
        }
        if(!meet_cut) return;
    }
    //bench->meeting_buckets[bid].end - bench->meeting_buckets[bid].start >= bench->config->min_meet_time
    if (bench->cur_time - bench->meeting_buckets[bid].start >= bench->config->min_meet_time + 1) {
        if(!box_contain(&bench->mbr, &bench->meeting_buckets[bid].mbr)){
            bench->meeting_buckets[bid].key = ULL_MAX;
            return;
        }

        uint duration = bench->meeting_buckets[bid].end - bench->meeting_buckets[bid].start;
        if(duration >= 990){
            if(duration < 2000){
                atomicAdd(&bench->larger_than_1000s, 1);
            }
            if(duration >= 2000 && duration < 3000){
                atomicAdd(&bench->larger_than_2000s, 1);
            }
            if(duration >= 3000 && duration < 4000){
                atomicAdd(&bench->larger_than_3000s, 1);
            }
            if(duration >= 4000){
                atomicAdd(&bench->larger_than_4000s, 1);
            }
        }

        atomicMin(&bench->start_time_min,bench->meeting_buckets[bid].start);
        atomicMax(&bench->start_time_max,bench->meeting_buckets[bid].start);

        uint pid, target;
        pid = getpid1(bench->meeting_buckets[bid].key);
        target = getpid2(bench->meeting_buckets[bid].key);

        uint low0 = (bench->meeting_buckets[bid].mbr.low[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * ((1ULL << (WID_BIT/2)) - 1);
        uint low1 = (bench->meeting_buckets[bid].mbr.low[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * ((1ULL << (WID_BIT/2)) - 1);
        //uint high0 = (bench->meeting_buckets[bid].mbr.high[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * (pow(2,WID_BIT/2) - 1);
        //uint high1 = (bench->meeting_buckets[bid].mbr.high[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * (pow(2,WID_BIT/2) - 1);

        uint meeting_idx = atomicAdd(&bench->kv_count, 2);
        assert(meeting_idx < bench->config->kv_capacity);

        for (int k = 0; k < 2; k++) {
            if(k==1){
                meeting_idx++;
                uint swap = pid;
                pid = target;
                target = swap;
            }
            write_kv_box(&bench->kv_boxs[meeting_idx], &bench->meeting_buckets[bid].mbr);
            bench->d_keys[meeting_idx] =  ((__uint128_t)pid << (PID_BIT + MBR_BIT + DURATION_BIT + END_BIT)) + ((__uint128_t)target << (MBR_BIT + DURATION_BIT + END_BIT)) +
                    ((__uint128_t)(bench->meeting_buckets[bid].end - bench->meeting_buckets[bid].start) << END_BIT) + (__uint128_t)(bench->meeting_buckets[bid].end - bench->end_time_min);
            update_wid(bench->d_wids[pid], WID_BIT, low0, low1);
        }
    }
    // reset the bucket
    if(meet_cut){
        bench->meeting_buckets[bid].start = bench->cur_time;
    }
    else {
        bench->meeting_buckets[bid].key = ULL_MAX;
    }
}

__global__
void cuda_search_single_kv(workbench *bench){
//    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
//    if(kid>=bench->kv_count){
//        return;
//    }
//    if((uint)(bench->d_keys[kid] >> 23 & ((1ULL << 25) - 1)) == bench->search_single_pid){              //all the same
//        uint meeting_idx = atomicAdd(&bench->single_find_count, 1);
//        assert(bench->single_find_count<bench->config->search_single_capacity);
//        bench->search_single_list[meeting_idx].end = ((bench->d_keys[kid] >> 8) & ((1ULL << 15) - 1)) + bench->end_time_min;
//        bench->search_single_list[meeting_idx].start = bench->search_single_list[meeting_idx].end - (bench->d_values[kid] >> 113);
//        bench->search_single_list[meeting_idx].target = ((bench->d_values[kid] >> 88) & ((1ULL << 25) - 1));
//        bench->search_single_list[meeting_idx].low0 = uint_to_float((uint)((bench->d_values[kid] >> 66) & ((1ULL << 22) - 1)));
//        bench->search_single_list[meeting_idx].low1 = uint_to_float((uint)((bench->d_values[kid] >> 44) & ((1ULL << 22) - 1)));
//        bench->search_single_list[meeting_idx].high0 = uint_to_float((uint)((bench->d_values[kid] >> 22) & ((1ULL << 22) - 1)));
//        bench->search_single_list[meeting_idx].high1 = uint_to_float((uint)(bench->d_values[kid] & ((1ULL << 22) - 1)));
//    }
}

__global__
void cuda_search_multi_kv(workbench *bench){
//    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
//    if(kid>=bench->kv_count){
//        return;
//    }
//    for(int i = 0;i<bench->search_multi_length;i++){
//        if((uint)(bench->d_keys[kid] >> 23 & ((1ULL << 25) - 1)) == bench->search_multi_pid[i]){
//            uint meeting_idx = atomicAdd(&bench->multi_find_count, 1);
//            assert(bench->multi_find_count < bench->config->search_multi_capacity);
//            bench->search_multi_list[meeting_idx].pid = bench->search_multi_pid[i];
//            bench->search_multi_list[meeting_idx].end = ((bench->d_keys[kid] >> 8) & ((1ULL << 15) - 1)) + bench->end_time_min;
//            bench->search_multi_list[meeting_idx].start = bench->search_multi_list[meeting_idx].end - (bench->d_values[kid] >> 113);
//            bench->search_multi_list[meeting_idx].target = ((bench->d_values[kid] >> 88) & ((1ULL << 25) - 1));
//            bench->search_multi_list[meeting_idx].low0 = uint_to_float((uint)((bench->d_values[kid] >> 66) & ((1ULL << 22) - 1)));
//            bench->search_multi_list[meeting_idx].low1 = uint_to_float((uint)((bench->d_values[kid] >> 44) & ((1ULL << 22) - 1)));
//            bench->search_multi_list[meeting_idx].high0 = uint_to_float((uint)((bench->d_values[kid] >> 22) & ((1ULL << 22) - 1)));
//            bench->search_multi_list[meeting_idx].high1 = uint_to_float((uint)(bench->d_values[kid] & ((1ULL << 22) - 1)));
//        }
//    }
}

//__global__
//void write_bitboxs_valuembrs(workbench *bench){
//    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
//    if(kid>=bench->kv_count){
//        return;
//    }
//    if((bench->d_keys[kid] >> PID_BIT) > 0){
//        return;
//    }
//
//    uint pid = get_key_pid(bench->d_keys[kid]);
//    assert(bench->d_bitboxs[2*pid] <= bench->bit_count);
//    bench->d_keys[kid] = ((uint64_t)bench->d_bitboxs[2*pid] << PID_BIT) + (bench->d_keys[kid] & ((1ULL << PID_BIT) - 1));
//    //bench->d_keys[kid] += ((uint64_t)bench->d_bitboxs[pid] << PID_BIT);     //is both right
//
//    uint first_low0, first_low1, first_high0, first_high1;
//    d2xy(FIRST_HILBERT_BIT/2, bench->d_bitboxs[2*pid], first_low0, first_low1);
//    d2xy(FIRST_HILBERT_BIT/2, bench->d_bitboxs[2*pid+1], first_high0, first_high1);
//
//    double float_first_low0 = (double)first_low0/(pow(2,WID_BIT/2) - 1)*(bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
//    double float_first_low1 = (double)first_low1/(pow(2,WID_BIT/2) - 1)*(bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
//    double float_first_high0 = (double)first_high0/(pow(2,WID_BIT/2) - 1)*(bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
//    double float_first_high1 = (double)first_high1/(pow(2,WID_BIT/2) - 1)*(bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
//
//    uint second_low0 = (bench->kv_boxs[kid/2].low[0] - float_first_low0)/(float_first_high0 - float_first_low0) * 15;
//    uint second_low1 = (bench->kv_boxs[kid/2].low[1] - float_first_low1)/(float_first_high1 - float_first_low1) * 15;
//    uint second_high0 = (bench->kv_boxs[kid/2].high[0] - float_first_low0)/(float_first_high0 - float_first_low0) * 15;
//    uint second_high1 = (bench->kv_boxs[kid/2].high[1] - float_first_low1)/(float_first_high1 - float_first_low1) * 15;
//
//    uint64_t second_low = xy2d(SECOND_HILBERT_BIT/2, second_low0, second_low1);
//    uint64_t second_high = xy2d(SECOND_HILBERT_BIT/2, second_high0, second_high1);
//
//    bench->d_values[kid] += (second_low << (SECOND_HILBERT_BIT + PID_BIT + DURATION_BIT + END_BIT)) + (second_high << (PID_BIT + DURATION_BIT + END_BIT));
//}

__global__
void write_wid(workbench *bench){
    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
    if(kid>=bench->kv_count){           //write all
        return;
    }
    uint pid = get_key_pid(bench->d_keys[kid]);
    assert(bench->d_wids[pid]<=bench->bit_count);
    if(bench->d_wids[pid]){
        bench->d_keys[kid] = (bench->d_keys[kid] & (((__uint128_t)1 << (PID_BIT*2 + MBR_BIT + DURATION_BIT + END_BIT)) - 1))
                + ((__uint128_t)bench->d_wids[pid] << (PID_BIT*2 + MBR_BIT + DURATION_BIT + END_BIT));
    }
    else {
        //new wid : 0 -> old_wid
        uint old_wid = get_key_wid(bench->d_keys[kid]);
        if(old_wid){
            bench->d_wids[pid] = old_wid;
        }
    }
}

__global__
void BloomFilter_Add(workbench *bench){
    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
    if(kid>=bench->config->kv_restriction){
        return;
    }

    uint pdwHashPos;
    uint64_t hash1, hash2;
    uint key = bench->d_keys[kid]/100000000 / 100000000 / 100000000;
    for(int i=0;i<bench->dwHashFuncs; i++){
        hash1 = d_MurmurHash2_x64((const void *)&key, sizeof(uint), bench->dwSeed);            // double hash
        hash2 = d_MurmurHash2_x64((const void *)&key, sizeof(uint), MIX_UINT64(hash1));
        pdwHashPos = (hash1 + i*hash2) % bench->dwFilterBits;
        bench->d_pstFilter[pdwHashPos/8] |= (1<<(pdwHashPos%8));
    }
}

__global__
void write_bitmap(workbench *bench){
    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
    if(kid>=bench->config->kv_restriction){
        return;
    }
    uint low0 = (bench->kv_boxs[kid].low[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * ((1ULL << (WID_BIT/2)) - 1);
    uint low1 = (bench->kv_boxs[kid].low[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * ((1ULL << (WID_BIT/2)) - 1);
    uint high0 = (bench->kv_boxs[kid].high[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * ((1ULL << (WID_BIT/2)) - 1);
    uint high1 = (bench->kv_boxs[kid].high[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * ((1ULL << (WID_BIT/2)) - 1);

    uint bitmap_id = kid/(bench->config->kv_restriction / bench->config->SSTable_count);
    uint bit_pos = 0;
    for(uint i=low0;i<=high0;i++){
        for(uint j=low1;j<=high1;j++){
            bit_pos = xy2d(WID_BIT/2,i,j);
            //bench->d_bitmaps[bitmap_id*(bench->bit_count/8)+bit_pos/8] |= (1<<(bit_pos%8));
            //unsigned int *bitmap_ptr = reinterpret_cast<unsigned int *>(&bench->d_bitmaps[bitmap_id * (bench->bit_count / 8) + bit_pos / 32]);
            unsigned int *bitmap_ptr = reinterpret_cast<unsigned int *>(bench->d_bitmaps);
            atomicOr(&bitmap_ptr[bitmap_id * (bench->bit_count / 32) + bit_pos / 32], (1 << (bit_pos % 32)));
        }
    }
}

__global__
void mbr_bitmap(workbench *bench){
    __shared__ volatile int local_low[BLOCK_DIM][2];
    __shared__ volatile int local_high[BLOCK_DIM][2];

    uint temp_low[2] = {10000,10000};
    uint temp_high[2] = {0, 0};
    for (uint bit_pos = threadIdx.x; bit_pos < bench->bit_count; bit_pos += blockDim.x) {
        if (bench->d_bitmaps[blockIdx.x*(bench->bit_count/8)+bit_pos/8] & (1<<(bit_pos%8)) ) {
            uint temp[2];
            d2xy(WID_BIT/2, bit_pos, temp[0], temp[1]);
            temp_low[0] = min(temp_low[0], temp[0]);
            temp_low[1] = min(temp_low[1], temp[1]);
            temp_high[0] = max(temp_high[0], temp[0]);
            temp_high[1] = max(temp_high[1], temp[1]);
        }
    }
    local_low[threadIdx.x][0] = temp_low[0];
    local_low[threadIdx.x][1] = temp_low[1];
    local_high[threadIdx.x][0] = temp_high[0];
    local_high[threadIdx.x][1] = temp_high[1];
    __syncthreads();

    for (int j = blockDim.x >> 1; j > 32; j >>= 1) {
        if (threadIdx.x < j) {
            local_low[threadIdx.x][0] = min(local_low[threadIdx.x][0], local_low[threadIdx.x + j][0]);
            local_low[threadIdx.x][1] = min(local_low[threadIdx.x][1], local_low[threadIdx.x + j][1]);
            local_high[threadIdx.x][0] = max(local_high[threadIdx.x][0], local_high[threadIdx.x + j][0]);
            local_high[threadIdx.x][1] = max(local_high[threadIdx.x][1], local_high[threadIdx.x + j][1]);
        }
        __syncthreads();
    }

    if (threadIdx.x < 32) {
        for(int j = 32; j >= 1; j >>= 1){
            local_low[threadIdx.x][0] = min(local_low[threadIdx.x][0], local_low[threadIdx.x + j][0]);
            local_low[threadIdx.x][1] = min(local_low[threadIdx.x][1], local_low[threadIdx.x + j][1]);
            local_high[threadIdx.x][0] = max(local_high[threadIdx.x][0], local_high[threadIdx.x + j][0]);
            local_high[threadIdx.x][1] = max(local_high[threadIdx.x][1], local_high[threadIdx.x + j][1]);
        }
    }
    if (threadIdx.x == 0) {
        bench->d_bitmap_mbrs[blockIdx.x].low[0] = (double)local_low[0][0]/((1ULL << (WID_BIT/2)) - 1)*(bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
        bench->d_bitmap_mbrs[blockIdx.x].low[1] = (double)local_low[0][1]/((1ULL << (WID_BIT/2)) - 1)*(bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
        bench->d_bitmap_mbrs[blockIdx.x].high[0] = (double)local_high[0][0]/((1ULL << (WID_BIT/2)) - 1)*(bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
        bench->d_bitmap_mbrs[blockIdx.x].high[1] = (double)local_high[0][1]/((1ULL << (WID_BIT/2)) - 1)*(bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
    }
}

__global__
void write_key_mbr(workbench *bench){
    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
    if(kid>=bench->config->kv_restriction){
        return;
    }
    if(bench->cur_time > 2000){
        if(get_key_duration(bench->d_keys[kid]) > 2000){
            print_box(&bench->kv_boxs[kid]);
        }
    }

    uint bitmap_id = kid/(bench->config->kv_restriction / bench->config->SSTable_count);
    uint64_t value_mbr = serialize_mbr(&bench->kv_boxs[kid], &bench->d_bitmap_mbrs[bitmap_id]);
    bench->d_keys[kid] += (__uint128_t)value_mbr << (DURATION_BIT + END_BIT);
}

//__global__
//void mbr_bitmap(workbench *bench){
//    uint kid = blockIdx.x*blockDim.x+threadIdx.x;
//    if(kid>=bench->config->kv_restriction){
//        return;
//    }
////    uint low0,low1,high0,high1;
////    float f_low0,f_low1,f_high0,f_high1;
//
//    uint pid = get_key_pid(bench->d_keys[kid]);
//    uint low = bench->d_bitboxs[2*pid], high = bench->d_bitboxs[2*pid+1];
//    if(!low || !high){
//        return;
//    }
//    uint first_low0, first_low1, first_high0, first_high1;
//    d2xy(FIRST_HILBERT_BIT/2, low, first_low0, first_low1);
//    d2xy(FIRST_HILBERT_BIT/2, high, first_high0, first_high1);
//    int offset = first_high0 - first_low0;
//    if(offset > 250){
//        printf("outpid%d %d-%d=%d\n", pid, first_high0, first_low0, offset);
//    }
//
//    uint bitmap_id = kid/(bench->config->kv_restriction / bench->config->SSTable_count);
//    uint bit_pos = 0;
//    for(uint i=first_low0;i<=first_high0;i++){
//        for(uint j=first_low1;j<=first_high1;j++){
//            bit_pos = xy2d(FIRST_HILBERT_BIT/2, i, j);
//            bench->d_bitmaps[bitmap_id*(bench->bit_count/8)+bit_pos/8] |= (1<<(bit_pos%8));
//        }
//    }
//}

/*
 * kernel functions for index update
 *
 * */
__global__
void cuda_update_schema_split(workbench *bench, uint size){
	uint sidx = blockIdx.x*blockDim.x+threadIdx.x;
	if(sidx>=size){
		return;
	}
	uint curnode = bench->split_list[sidx];
	//printf("split: %d\n",curnode);
	//schema[curnode].mbr.print();
	bench->schema[curnode].type = BRANCH;
	// reuse by one of its child
	uint gid = bench->schema[curnode].grid_id;

	double xhalf = bench->schema[curnode].mid_x-bench->schema[curnode].mbr.low[0];
	double yhalf = bench->schema[curnode].mid_y-bench->schema[curnode].mbr.low[1];

	for(int i=0;i<4;i++){
		// pop space for schema and grid
		uint idx = atomicAdd(&bench->schema_stack_index, 1);
		assert(idx<bench->schema_stack_capacity);
		uint child = bench->schema_stack[idx];
		//printf("sidx: %d %d\n",idx,child);
		bench->schema[curnode].children[i] = child;

		if(i>0){
			idx = atomicAdd(&bench->grids_stack_index,1);
			assert(idx<bench->grids_stack_capacity);
			gid = bench->grids_stack[idx];
			//printf("gidx: %d %d\n",idx,gid);
		}
		bench->schema[child].grid_id = gid;
		bench->grid_counter[gid] = 0;
		bench->schema[child].level = bench->schema[curnode].level+1;
		bench->schema[child].type = LEAF;
		bench->schema[child].overflow_count = 0;
		bench->schema[child].underflow_count = 0;

		bench->schema[child].mbr.low[0] = bench->schema[curnode].mbr.low[0]+(i%2==1)*xhalf;
		bench->schema[child].mbr.low[1] = bench->schema[curnode].mbr.low[1]+(i/2==1)*yhalf;
		bench->schema[child].mbr.high[0] = bench->schema[curnode].mid_x+(i%2==1)*xhalf;
		bench->schema[child].mbr.high[1] = bench->schema[curnode].mid_y+(i/2==1)*yhalf;
		bench->schema[child].mid_x = (bench->schema[child].mbr.low[0]+bench->schema[child].mbr.high[0])/2;
		bench->schema[child].mid_y = (bench->schema[child].mbr.low[1]+bench->schema[child].mbr.high[1])/2;
	}
}

__global__
void cuda_update_schema_merge(workbench *bench, uint size){
	uint sidx = blockIdx.x*blockDim.x+threadIdx.x;
	if(sidx>=size){
		return;
	}
	uint curnode = bench->merge_list[sidx];
	//reclaim the children
	uint gid = 0;
	for(int i=0;i<4;i++){
		uint child_offset = bench->schema[curnode].children[i];
		assert(bench->schema[child_offset].type==LEAF);
		//bench->schema[child_offset].mbr.print();
		// push the bench->schema and grid spaces to stack for reuse

		bench->grid_counter[bench->schema[child_offset].grid_id] = 0;
		if(i<3){
			// push to stack
			uint idx = atomicSub(&bench->grids_stack_index,1)-1;
			bench->grids_stack[idx] = bench->schema[child_offset].grid_id;
		}else{
			// reused by curnode
			gid = bench->schema[child_offset].grid_id;
		}
		bench->schema[child_offset].type = INVALID;
		uint idx = atomicSub(&bench->schema_stack_index,1)-1;
		bench->schema_stack[idx] = child_offset;
	}
	bench->schema[curnode].type = LEAF;
	// reuse the grid of one of its child
	bench->schema[curnode].grid_id = gid;
}

__global__
void cuda_update_schema_collect(workbench *bench){
	uint curnode = blockIdx.x*blockDim.x+threadIdx.x;
	if(curnode>=bench->schema_stack_capacity){
		return;
	}
	if(bench->schema[curnode].type==LEAF){
		if(height(&bench->schema[curnode].mbr)>2*bench->config->reach_distance&&
				bench->grid_counter[bench->schema[curnode].grid_id]>bench->config->grid_capacity){
			// this node is overflowed a continuous number of times, split it
			if(++bench->schema[curnode].overflow_count>=bench->config->schema_update_delay){
				uint sidx = atomicAdd(&bench->split_list_index,1);
				bench->split_list[sidx] = curnode;
				bench->schema[curnode].overflow_count = 0;
			}
		}else{
			bench->schema[curnode].overflow_count = 0;
		}
	}else if(bench->schema[curnode].type==BRANCH){
		int leafchild = 0;
		int ncounter = 0;
		for(int i=0;i<4;i++){
			uint child_node = bench->schema[curnode].children[i];
			if(bench->schema[child_node].type==LEAF){
				leafchild++;
				ncounter += bench->grid_counter[bench->schema[child_node].grid_id];
			}
		}
		// this one need update
		if(leafchild==4&&ncounter<bench->config->grid_capacity){
			// this node need be merged
			if(++bench->schema[curnode].underflow_count>=bench->config->schema_update_delay){
				//printf("%d\n",curnode);
				uint sidx = atomicAdd(&bench->merge_list_index,1);
				bench->merge_list[sidx] = curnode;
				bench->schema[curnode].underflow_count = 0;
			}
		}else{
			bench->schema[curnode].underflow_count = 0;
		}
	}
}


__global__
void cuda_init_schema_stack(workbench *bench){
	uint curnode = blockIdx.x*blockDim.x+threadIdx.x;
	if(curnode>=bench->schema_stack_capacity){
		return;
	}
	bench->schema_stack[curnode] = curnode;
}
__global__
void cuda_init_grids_stack(workbench *bench){
	uint curnode = blockIdx.x*blockDim.x+threadIdx.x;
	if(curnode>=bench->grids_stack_capacity){
		return;
	}
	bench->grids_stack[curnode] = curnode;
}

#define one_dim 16384
//#define one_dim 8

__global__
void cuda_build_qtree(workbench *bench){
	uint pid = blockIdx.x*blockDim.x+threadIdx.x;
	if(pid>=bench->config->num_objects){
		return;
	}
	uint x = (bench->points[pid].x-bench->mbr.low[0])/(bench->mbr.high[0]-bench->mbr.low[0])*one_dim;
	uint y = (bench->points[pid].y-bench->mbr.low[1])/(bench->mbr.high[1]-bench->mbr.low[1])*one_dim;
	atomicAdd(&bench->part_counter[x+y*one_dim],1);
}

__global__
void cuda_clean_cells(workbench *bench){
	uint pid = blockIdx.x*blockDim.x+threadIdx.x;
	if(pid>=one_dim*one_dim){
		return;
	}
	bench->schema_assigned[pid] = 0;
	bench->part_counter[pid] = 0;
	if(pid==0){
		bench->grids_stack_index = 0;
		bench->schema_stack_index = 1;
	}
}

__global__
void cuda_merge_qtree(workbench *bench, uint gap){
	uint pid = blockIdx.x*blockDim.x+threadIdx.x;
	uint xdim = one_dim/gap;
	if(pid>=(xdim*xdim)){
		return;
	}

	uint x = pid%xdim;
	uint y = pid/xdim;
	if(gap==1){
		if(bench->part_counter[pid]>bench->config->grid_capacity){
			uint node = atomicAdd(&bench->schema_stack_index,1);
			bench->schema[node].type = BRANCH;
			bench->schema[node].mbr.low[0] = bench->mbr.low[0]+x*(bench->mbr.high[0]-bench->mbr.low[0])/xdim;
			bench->schema[node].mbr.low[1] = bench->mbr.low[1]+y*(bench->mbr.high[1]-bench->mbr.low[1])/xdim;
			bench->schema[node].mbr.high[0] = bench->mbr.low[0]+(x+1)*(bench->mbr.high[0]-bench->mbr.low[0])/xdim;
			bench->schema[node].mbr.high[1] = bench->mbr.low[1]+(y+1)*(bench->mbr.high[1]-bench->mbr.low[1])/xdim;
			bench->schema[node].mid_x = (bench->schema[node].mbr.low[0]+bench->schema[node].mbr.high[0])/2;
			bench->schema[node].mid_y = (bench->schema[node].mbr.low[1]+bench->schema[node].mbr.high[1])/2;
			double xhalf = bench->schema[node].mid_x-bench->schema[node].mbr.low[0];
			double yhalf = bench->schema[node].mid_y-bench->schema[node].mbr.low[1];

			for(uint i=0;i<4;i++){
				uint cnode = atomicAdd(&bench->schema_stack_index,1);
				bench->schema[cnode].grid_id = atomicAdd(&bench->grids_stack_index,1);
				bench->schema[cnode].type = LEAF;
				bench->schema[node].children[i] = cnode;
				bench->grid_counter[bench->schema[cnode].grid_id] = 0;
				bench->schema[cnode].type = LEAF;
				bench->schema[cnode].overflow_count = 0;
				bench->schema[cnode].underflow_count = 0;
				bench->schema[cnode].mbr.low[0] = bench->schema[node].mbr.low[0]+(i%2==1)*xhalf;
				bench->schema[cnode].mbr.low[1] = bench->schema[node].mbr.low[1]+(i/2==1)*yhalf;
				bench->schema[cnode].mbr.high[0] = bench->schema[node].mid_x+(i%2==1)*xhalf;
				bench->schema[cnode].mbr.high[1] = bench->schema[node].mid_y+(i/2==1)*yhalf;
				bench->schema[cnode].mid_x = (bench->schema[cnode].mbr.low[0]+bench->schema[cnode].mbr.high[0])/2;
				bench->schema[cnode].mid_y = (bench->schema[cnode].mbr.low[1]+bench->schema[cnode].mbr.high[1])/2;
				//print_box(&bench->schema[cnode].mbr);
			}
			bench->schema_assigned[pid] = node;
		}
	}else{

		uint step = gap/2;
		uint p[4];
		p[0] = y*gap*one_dim+x*gap;
		p[1] = y*gap*one_dim+x*gap+step;
		p[2] = y*gap*one_dim+step*one_dim+x*gap;
		p[3] = y*gap*one_dim+step*one_dim+x*gap+step;
		uint size = 0;
		for(uint i=0;i<4;i++){
			size += bench->part_counter[p[i]];
		}
		// parent node
		if(size>bench->config->grid_capacity){
			uint node = 0;
			// node 0 is for the root only
			if(xdim!=1){
				node = atomicAdd(&bench->schema_stack_index,1);
			}
			bench->schema[node].type = BRANCH;
			bench->schema[node].mbr.low[0] = bench->mbr.low[0]+x*(bench->mbr.high[0]-bench->mbr.low[0])/xdim;
			bench->schema[node].mbr.low[1] = bench->mbr.low[1]+y*(bench->mbr.high[1]-bench->mbr.low[1])/xdim;
			bench->schema[node].mbr.high[0] = bench->mbr.low[0]+(x+1)*(bench->mbr.high[0]-bench->mbr.low[0])/xdim;
			bench->schema[node].mbr.high[1] = bench->mbr.low[1]+(y+1)*(bench->mbr.high[1]-bench->mbr.low[1])/xdim;
			bench->schema[node].mid_x = (bench->schema[node].mbr.low[0]+bench->schema[node].mbr.high[0])/2;
			bench->schema[node].mid_y = (bench->schema[node].mbr.low[1]+bench->schema[node].mbr.high[1])/2;

			double xhalf = bench->schema[node].mid_x-bench->schema[node].mbr.low[0];
			double yhalf = bench->schema[node].mid_y-bench->schema[node].mbr.low[1];
			for(uint i=0;i<4;i++){
				uint cnode = 0;
				if(bench->schema_assigned[p[i]]!=0){
					cnode = bench->schema_assigned[p[i]];
				}else{
					cnode = atomicAdd(&bench->schema_stack_index,1);
					bench->schema[cnode].grid_id = atomicAdd(&bench->grids_stack_index,1);
					bench->schema[cnode].type = LEAF;
					bench->grid_counter[bench->schema[cnode].grid_id] = 0;
					bench->schema[cnode].type = LEAF;
					bench->schema[cnode].overflow_count = 0;
					bench->schema[cnode].underflow_count = 0;
					bench->schema[cnode].mbr.low[0] = bench->schema[node].mbr.low[0]+(i%2==1)*xhalf;
					bench->schema[cnode].mbr.low[1] = bench->schema[node].mbr.low[1]+(i/2==1)*yhalf;
					bench->schema[cnode].mbr.high[0] = bench->schema[node].mid_x+(i%2==1)*xhalf;
					bench->schema[cnode].mbr.high[1] = bench->schema[node].mid_y+(i/2==1)*yhalf;
					bench->schema[cnode].mid_x = (bench->schema[cnode].mbr.low[0]+bench->schema[cnode].mbr.high[0])/2;
					bench->schema[cnode].mid_y = (bench->schema[cnode].mbr.low[1]+bench->schema[cnode].mbr.high[1])/2;
					//print_box(&bench->schema[cnode].mbr);
				}
				bench->schema[node].children[i] = cnode;
			}
			bench->schema_assigned[p[0]] = node;
		}
		// for next upper level
		bench->part_counter[p[0]] = size;
	}
}

workbench *cuda_create_device_bench(workbench *bench, gpu_info *gpu){
	log("GPU memory:");
	struct timeval start = get_cur_time();
	gpu->clear();
	// use h_bench as a container to copy in and out GPU
	workbench h_bench(bench);
	// space for the raw points data
	h_bench.points = (Point *)gpu->allocate(bench->config->num_objects*sizeof(Point));
	size_t size = bench->config->num_objects*sizeof(Point);
	log("\t%.2f MB\tpoints",1.0*size/1024/1024);

	// space for the pids of all the grids
	h_bench.grids = (uint *)gpu->allocate(bench->grids_stack_capacity*bench->grid_capacity*sizeof(uint));
	h_bench.grid_counter = (uint *)gpu->allocate(bench->grids_stack_capacity*sizeof(uint));
	h_bench.grids_stack = (uint *)gpu->allocate(bench->grids_stack_capacity*sizeof(uint));
	size = bench->grids_stack_capacity*bench->grid_capacity*sizeof(uint)+bench->grids_stack_capacity*sizeof(uint)+bench->grids_stack_capacity*sizeof(uint);
	log("\t%.2f MB\tgrids",1.0*size/1024/1024);

	// space for the QTtree schema
	h_bench.schema = (QTSchema *)gpu->allocate(bench->schema_stack_capacity*sizeof(QTSchema));
	h_bench.schema_stack = (uint *)gpu->allocate(bench->schema_stack_capacity*sizeof(uint));
	size = bench->schema_stack_capacity*sizeof(QTSchema)+bench->schema_stack_capacity*sizeof(uint);
	log("\t%.2f MB\tschema",1.0*size/1024/1024);

	// space for the pid-zid pairs
	h_bench.grid_check = (checking_unit *)gpu->allocate(bench->grid_check_capacity*sizeof(checking_unit));
	size = bench->grid_check_capacity*sizeof(checking_unit);
	log("\t%.2f MB\trefine list",1.0*size/1024/1024);

	size = 2*bench->filter_list_capacity*sizeof(uint);
	h_bench.filter_list = (uint *)gpu->allocate(size);
	log("\t%.2f MB\tfiltering list",1.0*size/1024/1024);


	// space for processing stack
	h_bench.tmp_space = (uint *)gpu->allocate(bench->tmp_space_capacity*sizeof(uint));
	size = bench->tmp_space_capacity*sizeof(uint);
	h_bench.merge_list = h_bench.tmp_space;
	h_bench.split_list = h_bench.tmp_space+bench->tmp_space_capacity/2;
	log("\t%.2f MB\ttemporary space",1.0*size/1024/1024);

	h_bench.meeting_buckets = (meeting_unit *)gpu->allocate(bench->config->num_meeting_buckets*sizeof(meeting_unit));
	size = bench->config->num_meeting_buckets*sizeof(meeting_unit);
	log("\t%.2f MB\thash table",1.0*size/1024/1024);

//	h_bench.meetings = (meeting_unit *)gpu->allocate(bench->meeting_capacity*sizeof(meeting_unit));
//	size = bench->meeting_capacity*sizeof(meeting_unit);
//	log("\t%.2f MB\tmeetings",1.0*size/1024/1024);

    //cuda sort
    h_bench.d_keys = (__uint128_t *)gpu->allocate(bench->config->kv_capacity*sizeof(__uint128_t));
    size = bench->config->kv_capacity*sizeof(__uint128_t);
    log("\t%.2f MB\td_keys",1.0*size/1024/1024);
//    h_bench.d_values = (uint64_t *)gpu->allocate(bench->config->kv_capacity*sizeof(uint64_t));
//    size = bench->config->kv_capacity*sizeof(uint64_t);
//    log("\t%.2f MB\td_values",1.0*size/1024/1024);


    //cuda search
    h_bench.search_single_list = (search_info_unit *)gpu->allocate(bench->config->search_single_capacity*sizeof(search_info_unit));
    size = bench->config->search_single_capacity*sizeof(search_info_unit);
    log("\t%.2f MB\tsearch_single_list",1.0*size/1024/1024);
    h_bench.search_multi_pid = (uint *)gpu->allocate(bench->config->search_single_capacity*sizeof(uint));
    size = bench->config->search_single_capacity*sizeof(uint);
    log("\t%.2f MB\tsearch_single_list",1.0*size/1024/1024);
    h_bench.search_multi_list = (search_info_unit *)gpu->allocate(bench->config->search_multi_capacity*sizeof(search_info_unit));
    size = bench->config->search_single_capacity*sizeof(search_info_unit);
    log("\t%.2f MB\tsearch_single_list",1.0*size/1024/1024);

    if(bench->config->bloom_filter) {
        //bloom filter
        h_bench.d_pstFilter = (unsigned char *) gpu->allocate(bench->dwFilterSize);
        size = bench->dwFilterSize;
        log("\t%.2f MB\td_pstFilter", 1.0 * size / 1024 / 1024);
        cudaMemset(h_bench.d_pstFilter, 0, bench->dwFilterSize);
    }

    //bitmap
    if(true) {
        //bloom filter
        h_bench.d_bitmaps = (unsigned char *) gpu->allocate(bench->bitmaps_size);
        size = bench->bitmaps_size;
        log("\t%.2f MB\td_bitmaps", 1.0 * size / 1024 / 1024);
        cudaMemset(h_bench.d_bitmaps, 0, size);

        h_bench.d_wids = (unsigned short*)gpu->allocate(bench->config->num_objects*sizeof(unsigned short));
        size = bench->config->num_objects*sizeof(unsigned short);
        log("\t%.2f MB\td_wids", 1.0 * size / 1024 / 1024);

        h_bench.kv_boxs = (f_box *)gpu->allocate(bench->config->kv_capacity*sizeof(f_box));
        size = bench->config->kv_capacity*sizeof(f_box);
        log("\t%.2f MB\tkv_boxs",1.0*size/1024/1024);

        h_bench.d_bitmap_mbrs = (box *)gpu->allocate(bench->config->SSTable_count*sizeof(box));
        size = bench->config->SSTable_count*sizeof(box);
        log("\t%.2f MB\td_bitmap_mbrs",1.0*size/1024/1024);
    }

	h_bench.part_counter = (uint *)gpu->allocate(one_dim*one_dim*sizeof(uint));
	h_bench.schema_assigned = (uint *)gpu->allocate(one_dim*one_dim*sizeof(uint));

	// space for the configuration
	h_bench.config = (configuration *)gpu->allocate(sizeof(configuration));
	// space for the mapping of bench in GPU
	workbench *d_bench = (workbench *)gpu->allocate(sizeof(workbench));

	// the configuration and schema are fixed
	CUDA_SAFE_CALL(cudaMemcpy(h_bench.schema, bench->schema, bench->schema_stack_capacity*sizeof(QTSchema), cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpy(h_bench.config, bench->config, sizeof(configuration), cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpy(d_bench, &h_bench, sizeof(workbench), cudaMemcpyHostToDevice));

	cuda_init_grids_stack<<<bench->grids_stack_capacity/1024, 1024>>>(d_bench);
	cuda_init_schema_stack<<<bench->schema_stack_capacity/1024, 1024>>>(d_bench);
	cuda_clean_buckets<<<bench->config->num_meeting_buckets/1024+1,1024>>>(d_bench);

	logt("GPU allocating space %ld MB", start,gpu->size_allocated()/1024/1024);

	return d_bench;
}

/*
 *
 * check the reachability of objects in a list of partitions
 * ctx.data contains the list of
 *
 * */
void process_with_gpu(workbench *bench, workbench* d_bench, gpu_info *gpu){
	struct timeval start = get_cur_time();
	//gpu->print();
	assert(bench);
	assert(d_bench);
	assert(gpu);
	cudaSetDevice(gpu->device_id);

	/* 1. copy data */
	// setup the current time and points for this round
	workbench h_bench(bench);
	CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
    if(bench->search_single) {
        h_bench.search_single = true;
        h_bench.single_find_count = 0;
    }
    else {
        h_bench.search_single = false;
    }
    if(bench->search_multi) {
        h_bench.search_multi = true;
        h_bench.multi_find_count = 0;
        h_bench.search_multi_length = bench->search_multi_length;
        CUDA_SAFE_CALL(cudaMemcpy(h_bench.search_multi_pid, bench->search_multi_pid, bench->search_multi_length * sizeof(search_info_unit), cudaMemcpyHostToDevice));
    }
    else {
        h_bench.search_multi = false;
    }
	h_bench.cur_time = bench->cur_time;
    h_bench.end_time_min = bench->end_time_min;
	CUDA_SAFE_CALL(cudaMemcpy(d_bench, &h_bench, sizeof(workbench), cudaMemcpyHostToDevice));
	CUDA_SAFE_CALL(cudaMemcpy(h_bench.points, bench->points, bench->config->num_objects*sizeof(Point), cudaMemcpyHostToDevice));
	bench->pro.copy_time += get_time_elapsed(start,false);
	logt("copy in data", start);

	if(!bench->config->dynamic_schema){
		struct timeval newstart = get_cur_time();
		cuda_clean_cells<<<one_dim*one_dim/1024+1,1024>>>(d_bench);
		cuda_build_qtree<<<bench->config->num_objects/1024+1,1024>>>(d_bench);
//		check_execution();
//		cudaDeviceSynchronize();
//		logt("build qtree", newstart);

		for(uint i=1;i<=one_dim;i*=2){
			uint num = one_dim*one_dim/(i*i);
			cuda_merge_qtree<<<num/1024+1,1024>>>(d_bench,i);
//			check_execution();
//			cudaDeviceSynchronize();
//			CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
//			logt("merge qtree %d %d %d %d", newstart,i, h_bench.schema_stack_index, h_bench.grids_stack_index, h_bench.grid_check_counter);
		}
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		cudaDeviceSynchronize();
		bench->pro.index_update_time += get_time_elapsed(start,false);
		logt("build qtree %d nodes %d partitions", start, h_bench.schema_stack_index, h_bench.grids_stack_index);
		//exit(0);
	}

	/* 2. filtering */
	if(bench->config->phased_lookup){
		// do the partition
		cuda_partition<<<bench->config->num_objects/1024+1,1024>>>(d_bench);

		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		bench->pro.partition_time += get_time_elapsed(start,false);
		logt("partition data %d still need lookup", start,h_bench.filter_list_index);
		bench->filter_list_index = h_bench.filter_list_index;
	}else{
		cuda_pack_lookup<<<bench->config->num_objects/1024+1,1024>>>(d_bench);
		check_execution();
		cudaDeviceSynchronize();
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
	}

	uint batch_size = bench->tmp_space_capacity/(PER_STACK_SIZE*2+1);
	for(int i=0;i<h_bench.filter_list_index;i+=batch_size){
		int bs = min(batch_size,h_bench.filter_list_index-i);
		cuda_filtering<<<bs/1024+1,1024>>>(d_bench, i, bs, !bench->config->phased_lookup);
		check_execution();
		cudaDeviceSynchronize();
	}
	CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
	bench->pro.filter_time += get_time_elapsed(start,false);
	logt("filtering with %d checkings", start,h_bench.grid_check_counter);

	/* 3. refinement step */
	if(false){
		for(uint offset=0;offset<bench->grid_capacity;offset+=bench->config->zone_capacity){
			struct timeval ss = get_cur_time();
			bench->grid_check_counter = h_bench.grid_check_counter;
			uint thread_y = bench->config->zone_capacity;
			uint thread_x = 1024/thread_y;
			dim3 block(thread_x, thread_y);
			cuda_refinement_unroll<<<h_bench.grid_check_counter/thread_x+1,block>>>(d_bench,offset);
			check_execution();
			cudaDeviceSynchronize();
			logt("process %d",ss,offset);
		}
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		bench->pro.refine_time += get_time_elapsed(start,false);
		logt("refinement step", start);
	}else{
		if(bench->config->unroll){
			cuda_unroll<<<h_bench.grid_check_counter/1024+1,1024>>>(d_bench,h_bench.grid_check_counter);
			check_execution();
			cudaDeviceSynchronize();
			CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
			bench->pro.refine_time += get_time_elapsed(start,false);
			logt("%d pid-grid-offset tuples need be checked", start,h_bench.grid_check_counter);
		}

		bench->grid_check_counter = h_bench.grid_check_counter;
		uint thread_y = bench->config->unroll?bench->config->zone_capacity:bench->grid_capacity;
		uint thread_x = 1024/thread_y;
		dim3 block(thread_x, thread_y);
		cuda_refinement<<<h_bench.grid_check_counter/thread_x+1,block>>>(d_bench);
		check_execution();
		cudaDeviceSynchronize();
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		bench->pro.refine_time += get_time_elapsed(start,false);
		logt("refinement step", start);
	}

	/* 4. identify the completed meetings */
	if(bench->config->profile){
		cuda_profile_meetings<<<bench->config->num_meeting_buckets/1024+1,1024>>>(d_bench);
		check_execution();
		cudaDeviceSynchronize();
		logt("profile meetings",start);
	}
    int before_kv = h_bench.kv_count;
	cuda_identify_meetings<<<bench->config->num_meeting_buckets/1024+1,1024>>>(d_bench);
	check_execution();
	cudaDeviceSynchronize();
	CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
    bench->pro.meeting_identify_time += get_time_elapsed(start,false);
    int kv_increase = h_bench.kv_count-before_kv;
    printf("second %d finished meetings : %d\n",bench->cur_time , kv_increase);
    //cout<<"ave_s_mbr"<<(float)h_bench.s_of_all_mbr/100<<endl;
    //h_bench.s_of_all_mbr = 0;
    logt("meeting identify: %d meetings", start, kv_increase);
	bench->num_active_meetings = h_bench.num_active_meetings;
	bench->num_taken_buckets = h_bench.num_taken_buckets;
    //bench->kv_count = h_bench.kv_count;
	//logt("meeting identify: %d taken %d active %d new meetings found", start, h_bench.num_taken_buckets, h_bench.num_active_meetings, h_bench.meeting_counter);

    if(bench->cur_time == 2200){
        cout <<"1000~2000 " << h_bench.larger_than_1000s << " 2000~3000 " << h_bench.larger_than_2000s << " 3000~4000 " << h_bench.larger_than_3000s << " >4000 " << h_bench.larger_than_4000s <<endl;
    }
    //4.5 cuda sort
    if(h_bench.kv_count>bench->config->kv_restriction){
        cout <<"1000~2000 " << h_bench.larger_than_1000s << " 2000~3000 " << h_bench.larger_than_2000s << " 3000~4000 " << h_bench.larger_than_3000s << " >4000 " << h_bench.larger_than_4000s <<endl;
        bench->meeting_cut_count = h_bench.meeting_cut_count;
        bench->start_time_min = h_bench.start_time_min;
        bench->start_time_max = h_bench.start_time_max;
        uint offset = 0;
        if(bench->big_sorted_run_count%2==1){
            offset = bench->config->MemTable_capacity/2;
        }
        write_wid<<<h_bench.kv_count / 1024 + 1, 1024>>>(d_bench);
        check_execution();
        cudaDeviceSynchronize();
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        logt("write_wid", start);

        // wrap raw pointer with a device_ptr
        thrust::device_ptr<__uint128_t> d_vector_keys = thrust::device_pointer_cast(h_bench.d_keys);
        __uint128_t * box128 = reinterpret_cast<__uint128_t *>(h_bench.kv_boxs);
        thrust::device_ptr<__uint128_t> d_vector_boxs = thrust::device_pointer_cast(box128);
        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
        logt("pointer_cast: ",start);
        // use device_ptr in Thrust algorithms
        thrust::sort_by_key(d_vector_keys, d_vector_keys + bench->config->kv_restriction, d_vector_boxs);
        check_execution();
        cudaDeviceSynchronize();
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
        logt("cuda_sort_time: ",start);

        //mbr bit map
        cout<<"h_bench.bit_count:"<<h_bench.bit_count<<endl;
        cout<<"h_bench.bitmaps_size:"<<h_bench.bitmaps_size<<endl;
        write_bitmap<<<bench->config->kv_restriction / 1024 + 1, 1024>>>(d_bench);
        check_execution();
        cudaDeviceSynchronize();
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        logt("write_bitmap: ",start);

        mbr_bitmap<<<bench->config->SSTable_count, 1024>>>(d_bench);
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        logt("mbr_bitmap: ",start);

        write_key_mbr<<<bench->config->kv_restriction / 1024 + 1, 1024>>>(d_bench);
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        logt("write_value_mbr: ",start);

        if(bench->config->bloom_filter){
            BloomFilter_Add<<<bench->config->kv_restriction / 1024 + 1,1024>>>(d_bench);
            check_execution();
            cudaDeviceSynchronize();
            CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
            logt("bloom filter ", start);
            CUDA_SAFE_CALL(cudaMemcpy(bench->pstFilter[bench->MemTable_count], h_bench.d_pstFilter, bench->dwFilterSize, cudaMemcpyDeviceToHost));
            cudaMemset(h_bench.d_pstFilter, 0, bench->dwFilterSize);
        }

        //copy to device
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        CUDA_SAFE_CALL(cudaMemcpy(bench->h_wids[offset + bench->MemTable_count], h_bench.d_wids, bench->config->num_objects * sizeof(unsigned short), cudaMemcpyDeviceToHost));
        CUDA_SAFE_CALL(cudaMemcpy(bench->h_bitmaps[offset+bench->MemTable_count], h_bench.d_bitmaps, bench->bitmaps_size, cudaMemcpyDeviceToHost));
        CUDA_SAFE_CALL(cudaMemcpy(bench->h_bitmap_mbrs[offset+bench->MemTable_count], h_bench.d_bitmap_mbrs, bench->config->SSTable_count*sizeof(box), cudaMemcpyDeviceToHost));
        CUDA_SAFE_CALL(cudaMemcpy(bench->h_keys[offset+bench->MemTable_count], h_bench.d_keys, bench->config->kv_restriction * sizeof(__uint128_t), cudaMemcpyDeviceToHost));
        logt("cudaMemcpy kv and meta",start);

        cout<<"bench->end_time_min:"<<bench->end_time_min<<endl;
        print_parse_key(bench->h_keys[offset+bench->MemTable_count][10]);
        box b;
        parse_mbr(bench->h_keys[offset+bench->MemTable_count][10], b, bench->h_bitmap_mbrs[offset+bench->MemTable_count][0]);
        b.print();
        f_box * temp_f_box = new f_box[20];
        CUDA_SAFE_CALL(cudaMemcpy(temp_f_box, h_bench.kv_boxs, 20 * sizeof(f_box), cudaMemcpyDeviceToHost));
        temp_f_box[10].print();
        cerr << "kv box, real box, and then the bitmap_mbr"<<endl;
        bench->h_bitmap_mbrs[offset+bench->MemTable_count][0].print();
//        cerr<<"bitmap_mbrs:"<<endl;
//        for(int i = 0; i < bench->config->SSTable_count; i++){
//            bench->h_bitmap_mbrs[offset+bench->MemTable_count][i].print();
//        }

        //init
        h_bench.start_time_min = (1ULL<<32) -1;
        h_bench.start_time_max = 0;
        cudaMemset(h_bench.d_wids, 0, bench->config->num_objects*sizeof(unsigned short));
        cudaMemset(h_bench.d_bitmaps, 0, bench->bitmaps_size);
        cudaMemset(h_bench.d_bitmap_mbrs, 0, bench->config->SSTable_count*sizeof(box));
        bench->MemTable_count++;
        cout << "bench->MemTable_count " << bench->MemTable_count << "MemTable_capacity" << bench->config->MemTable_capacity <<endl;
        int overflow = h_bench.kv_count - bench->config->kv_restriction;
        CUDA_SAFE_CALL(cudaMemcpy(h_bench.d_keys, h_bench.d_keys + bench->config->kv_restriction, overflow * sizeof(__uint128_t), cudaMemcpyDeviceToDevice));              //for the overflow part
        CUDA_SAFE_CALL(cudaMemcpy(h_bench.kv_boxs, h_bench.kv_boxs + bench->config->kv_restriction, overflow * sizeof(f_box), cudaMemcpyDeviceToDevice));
        //CUDA_SAFE_CALL(cudaMemcpy(h_bench.d_values, h_bench.d_values + bench->config->kv_restriction, overflow * sizeof(uint64_t), cudaMemcpyDeviceToDevice));
        h_bench.kv_count = overflow;
        CUDA_SAFE_CALL(cudaMemcpy(d_bench, &h_bench, sizeof(workbench), cudaMemcpyHostToDevice));                       //update kv_count, other effect ???
        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
        logt("init after sort",start);
    }

//    if(bench->crash_consistency){       //not finish
//        cout<<"crash_consistency, 1 cuda sort"<<endl;
//        bench->start_time_min = h_bench.start_time_min;
//        bench->start_time_max = h_bench.start_time_max;
//        h_bench.start_time_min = (1ULL<<32) -1;
//        h_bench.start_time_max = 0;
//        uint offset = 0;
//        if(bench->big_sorted_run_count%2==1){
//            offset = bench->config->MemTable_capacity/2;
//        }
//        if(true){
//            write_bitboxs_valuembrs<<<h_bench.kv_count / 1024 + 1, 1024>>>(d_bench);
//            check_execution();
//            cudaDeviceSynchronize();
//            CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
//            //logt("bloom filter ", start);
//            CUDA_SAFE_CALL(cudaMemcpy(bench->h_bitboxs[offset + bench->MemTable_count], h_bench.d_bitboxs, 2*bench->config->num_objects * sizeof(unsigned short), cudaMemcpyDeviceToHost));      //offset not change
//
//            cudaMemset(h_bench.d_bitboxs, 0, 2 * bench->config->num_objects * sizeof(unsigned short));
//        }
//
//        // wrap raw pointer with a device_ptr
//        thrust::device_ptr<uint64_t> d_vector_keys = thrust::device_pointer_cast(h_bench.d_keys);
//        thrust::device_ptr<uint64_t> d_vector_values = thrust::device_pointer_cast(h_bench.d_values);
//        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
//        logt("pointer_cast: ",start);
//        thrust::sort_by_key(d_vector_keys, d_vector_keys + h_bench.kv_count, d_vector_values);
//        check_execution();
//        cudaDeviceSynchronize();
//        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
//        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
//        logt("cuda_sort_time: ",start);
//        CUDA_SAFE_CALL(cudaMemcpy(bench->h_keys[offset+bench->MemTable_count], h_bench.d_keys, h_bench.kv_count * sizeof(uint64_t), cudaMemcpyDeviceToHost));
//        CUDA_SAFE_CALL(cudaMemcpy(bench->h_values[offset+bench->MemTable_count], h_bench.d_values, h_bench.kv_count * sizeof(uint64_t), cudaMemcpyDeviceToHost));
//        bench->pro.cuda_sort_time += get_time_elapsed(start,false);
//        logt("cudaMemcpy kv",start);
//        printf("cudaMemcpy kv right\n");
//
//    }

	// todo do the data analyzes, for test only, should not copy out so much stuff
	if(bench->config->analyze_grid||bench->config->analyze_reach||bench->config->profile){
		CUDA_SAFE_CALL(cudaMemcpy(bench->grid_counter, h_bench.grid_counter,
				bench->grids_stack_capacity*sizeof(uint), cudaMemcpyDeviceToHost));
		logt("copy out grid counting data", start);
	}
	if(bench->config->analyze_reach){
		CUDA_SAFE_CALL(cudaMemcpy(bench->grids, h_bench.grids,
							bench->grids_stack_capacity*bench->grid_capacity*sizeof(uint), cudaMemcpyDeviceToHost));
		CUDA_SAFE_CALL(cudaMemcpy(bench->schema, h_bench.schema,
							bench->schema_stack_capacity*sizeof(QTSchema), cudaMemcpyDeviceToHost));
		CUDA_SAFE_CALL(cudaMemcpy(bench->meeting_buckets, h_bench.meeting_buckets,
							bench->config->num_meeting_buckets*sizeof(meeting_unit), cudaMemcpyDeviceToHost));
		bench->schema_stack_index = h_bench.schema_stack_index;
		bench->grids_stack_index = h_bench.grids_stack_index;
		logt("copy out grid, schema, meeting buckets data", start);
	}

	/* 5. update the index */
	if(bench->config->dynamic_schema){
		// update the schema for future processing
		cuda_update_schema_collect<<<bench->schema_stack_capacity/1024+1,1024>>>(d_bench);
		check_execution();
		cudaDeviceSynchronize();
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		if(h_bench.split_list_index>0){
			cuda_update_schema_split<<<h_bench.split_list_index/1024+1,1024>>>(d_bench, h_bench.split_list_index);
			check_execution();
			cudaDeviceSynchronize();
		}
		if(h_bench.merge_list_index>0){
			cuda_update_schema_merge<<<h_bench.merge_list_index/1024+1,1024>>>(d_bench, h_bench.merge_list_index);
			check_execution();
			cudaDeviceSynchronize();
		}
		CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
		bench->pro.index_update_time += get_time_elapsed(start,false);
		logt("schema update %d grids", start, h_bench.grids_stack_index);
	}

    /* 6. search kv info */
    if(bench->search_single){
        cuda_search_single_kv<<<h_bench.kv_count/1024+1,1024>>>(d_bench);
        check_execution();
        cudaDeviceSynchronize();
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        bench->single_find_count = h_bench.single_find_count;
        CUDA_SAFE_CALL(cudaMemcpy(bench->search_single_list, h_bench.search_single_list, bench->single_find_count*sizeof(search_info_unit), cudaMemcpyDeviceToHost));
        bench->pro.cuda_search_single_kv_time += get_time_elapsed(start,false);
        logt("search_single_kv ", start);
    }
    if(bench->search_multi){
        cout<<"before multi_find_count"<<h_bench.multi_find_count<<endl;
        cuda_search_multi_kv<<<h_bench.kv_count/1024+1,1024>>>(d_bench);
        check_execution();
        cudaDeviceSynchronize();
        CUDA_SAFE_CALL(cudaMemcpy(&h_bench, d_bench, sizeof(workbench), cudaMemcpyDeviceToHost));
        bench->multi_find_count = h_bench.multi_find_count;
        cout<<"after multi_find_count"<<h_bench.multi_find_count<<endl;
        CUDA_SAFE_CALL(cudaMemcpy(bench->search_multi_list, h_bench.search_multi_list, bench->multi_find_count*sizeof(search_info_unit), cudaMemcpyDeviceToHost));
        bench->pro.cuda_search_multi_kv_time += get_time_elapsed(start,false);
        logt("search_multi_kv ", start);
    }

	/* 6. post-process, copy out data*/
//	if(h_bench.meeting_counter>0){
//		bench->meeting_counter = h_bench.meeting_counter;
//		CUDA_SAFE_CALL(cudaMemcpy(bench->meetings, h_bench.meetings, min(bench->meeting_capacity, h_bench.meeting_counter)*sizeof(meeting_unit), cudaMemcpyDeviceToHost));
//		bench->pro.copy_time += get_time_elapsed(start,false);
//		logt("copy out %d meeting data", start,h_bench.meeting_counter);
//	}
	// clean the device bench for next round of checking
	cuda_cleargrids<<<bench->grids_stack_capacity/1024+1,1024>>>(d_bench);
	cuda_reset_bench<<<1,1>>>(d_bench);
}
