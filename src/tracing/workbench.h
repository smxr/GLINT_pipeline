/*
 * working_bench.h
 *
 *  Created on: Feb 18, 2021
 *      Author: teng
 */

#ifndef SRC_TRACING_WORKBENCH_H_
#define SRC_TRACING_WORKBENCH_H_
#include "../util/util.h"
#include "../util/config.h"
#include "../util/query_context.h"
#include "../geometry/geometry.h"
#include "../util/ThreadPool.h"
#include "../index/QTree.h"
#include "step_merge.h"
#include "../cuda/cuda_util.cuh"
#include <future>
#include <unordered_set>

typedef struct profiler{
	double copy_time = 0;
	double partition_time = 0;
	double filter_time = 0;
	double refine_time = 0;
	double meeting_identify_time = 0;
	double index_update_time = 0;
	uint rounds = 0;

	uint max_refine_size = 0;
	uint max_filter_size = 0;
	uint max_grid_size = 0;
	uint max_grid_num = 0;
	uint max_schema_num = 0;
	size_t max_bucket_num = 0;

	uint grid_count = 0;
	uint grid_overflow = 0;
	double grid_dev = 0.0;
	vector<double> grid_overflow_list;
	vector<double> grid_deviation_list;
	size_t num_pairs = 0;
	size_t num_meetings = 0;

    double cuda_sort_time = 0;
    double cuda_search_single_kv_time = 0;
    double cuda_search_multi_kv_time = 0;
    double bg_merge_time = 0;
    double load_keys_time = 0;


    double search_memtable_time = 0;
    double search_in_disk_time = 0;
    double sum_round_time = 0;
}profiler;

typedef struct checking_unit{
	uint pid;
	uint gid;
	unsigned short offset;
	unsigned short inside;
}checking_unit;

typedef struct meeting_unit{
    size_t key;
//    unsigned short start;       //2023.9.21
//    unsigned short end;
    uint start;       //2023.9.21
    uint end;
    //uint wid;           //where 2024.3.13
    //Point midpoint;            //2023.7.17
    box mbr;                     //7.24 7.26
    bool isEmpty(){
        return key == ULL_MAX;
    }
    void reset(){
        key = ULL_MAX;
    }
    uint get_pid1(){
        return ::InverseCantorPairing1(key).first;
    }
    uint get_pid2(){
        return ::InverseCantorPairing1(key).second;
    }
}meeting_unit;

typedef struct search_info_unit{
    uint pid,target,start,end;
    float low0,low1,high0,high1;
}search_info_unit;


typedef struct reach_unit{
	uint pid1;
	uint pid2;
}reach_unit;

// the workbench where stores the memory space
// used for processing

class workbench{
	pthread_mutex_t *insert_lk;
	void *data[500];
	size_t data_size[500];
	uint data_index = 0;
	void *allocate(size_t size);
public:
	profiler pro;
	uint test_counter = 0;
	configuration *config = NULL;
	uint cur_time = 0;
	box mbr;

	// the pool of maintaining objects assignment
	// each grid buffer: |point_id1...point_idn|
	uint *grids = NULL;
	uint grid_capacity = 0;
	uint *grid_counter = NULL;

	// the stack that keeps the available grids
	uint *grids_stack = NULL;
	uint grids_stack_capacity = 0;
	uint grids_stack_index = 0;

	// the QTree schema
	QTSchema *schema = NULL;
	// stack that keeps the available schema nodes
	uint *schema_stack = NULL;
	uint schema_stack_capacity = 0;
	uint schema_stack_index = 0;

	uint *part_counter;
	uint *schema_assigned;

	// the space for point-unit pairs
	checking_unit *grid_check = NULL;
	uint grid_check_capacity = 0;
	uint grid_check_counter = 0;


	// the space to store the point-node pairs for filtering
	uint *filter_list = NULL;
	uint filter_list_index = 0;
	uint filter_list_capacity = 0;

	// the space for the overall meeting information maintaining now
	meeting_unit * meeting_buckets = NULL;
    meeting_unit * d_meetings_ps = NULL;            //ps : per second
    meeting_unit * h_meetings_ps = NULL;
    uint h_meetings_count = 0;
    uint total_meetings_this100s = 0;
    uint * active_meeting_count_ps = 0;

	size_t num_taken_buckets = 0;
	uint num_active_meetings = 0;

//	// the space for the valid meeting information now
//	meeting_unit *meetings = NULL;
//	uint meeting_capacity = 0;
//	uint meeting_counter = 0;

    //for space for cuda sort
    __uint128_t *d_keys = NULL;
    //uint64_t *d_values = NULL;
    uint kv_count = 0;

    //space for MemTable
    __uint128_t **h_keys = NULL;
    //uint64_t **h_values = NULL;
    uint MemTable_count = 0;

    //Bloom filter
    unsigned char **pstFilter = NULL;
    //uint32_t *dwCount = NULL;             // Add()
    //uint8_t cInitFlag = 0;            //
    uint dwMaxItems = 0;          //n
    double dProbFalse = 0;      //p, false positive rate           0.0000002 => bits out of uint    0.0004
    uint dwFilterBits = 0;         //m = ceil((n * log(p)) / log(1.0 / (pow(2.0, log(2.0))))); - BloomFilter       m < uint32_t
    uint dwHashFuncs = 0;         // k = round(log(2.0) * m / n); -
    uint dwSeed = 0;              // seed of MurmurHash
    uint dwFilterSize = 0;        // dwFilterBits / BYTE_BITS, while BYTE_BITS==8
    unsigned char * d_pstFilter = NULL;

    //space for search list
    bool search_single = false;
    bool search_multi = false;
    search_info_unit *search_single_list = NULL;
    search_info_unit *search_multi_list = NULL;
    uint search_multi_length = 0;
    uint *search_multi_pid = NULL;
    uint search_single_pid = 0;
    bool do_some_search = false;
    //uint search_count = 0;
    uint single_find_count = 0;
    uint multi_find_count = 0;
    uint wid_filter_count = 0;
    uint id_find_count = 0;
    uint id_not_find_count = 0;
    uint mbr_find_count = 0;
    uint mbr_unique_find = 0;
    uint intersect_sst_count = 0;
    uint time_find_count = 0;
    uint meeting_cut_count = 0;
    uint larger_than_1000s = 0;
    uint larger_than_2000s = 0;
    uint larger_than_3000s = 0;
    uint larger_than_4000s = 0;

    float * h_longer_edges = NULL;
    float * d_longer_edges = NULL;
    uint long_meeting_count = 0;
    uint long_oid_count = 0;

    //space for spatial id
    uint64_t * mid_xys = NULL;

    float * x_axis_of_parts = NULL;
    float ** y_axis_of_parts = NULL;
    uint * same_pid_count = NULL;
    unsigned short * d_sids = NULL;
    unsigned short ** h_sids = NULL;
    f_box * kv_boxs = NULL;                           //real box, 1:1 kv
    uint_box * o_boxs = NULL;                              //merge box, 1:1 object
    uint oversize_oid_count = 0;
    oversize_buffer * h_oversize_buffers = NULL;
    unsigned char * d_bitmaps = NULL;                 //1024  256*256
    unsigned char ** h_bitmaps = NULL;
    uint bitmap_edge_length = 0;
    uint bit_count = 0;         //256*256=65536  SSTable_count bitmap
    uint bitmaps_size = 0;
    uint bit_find_count = 0;
    box * d_bitmap_mbrs = NULL;
    box ** h_bitmap_mbrs = NULL;
    uint * d_oids = NULL;
    uint sid_count = 0;
    uint * d_CTF_capacity = NULL;
    uint ** h_CTF_capacity = NULL;

    uint suspicious_pid = 0;

    pthread_t command_thread;
    pthread_mutex_t mutex_i;
    bool interrupted = false;
    uint valid_timestamp = 0;

    //big sorted run
    CTB *ctbs = NULL;
    uint ctb_count = 0;
    uint start_time_min = 0;
    uint start_time_max = 0;
    uint end_time_min = 0;
    uint end_time_max = 0;
    uint CTF_kv_capacity = 0;
    bool dumping = false;
    uint merge_sstable_count = 100;
    uint merge_kv_capacity = 0;

    //s
    //uint s_of_all_mbr = 0;

    bool crash_consistency = false;

	// the temporary space
	uint *tmp_space = NULL;
	uint tmp_space_capacity = 0;

	uint *merge_list = NULL;
	uint merge_list_index = 0;
	uint *split_list = NULL;
	uint split_list_index = 0;

	// external source
	Point *points = NULL;

	workbench(workbench *bench);
	workbench(configuration *conf);
	~workbench(){};
	void clear();

	// insert point pid to grid gid
	bool insert(uint gid, uint pid);
	bool batch_insert(uint gid, uint num_objects, uint *pids);

	// generate pid-gid-offset pairs for processing
	bool check(uint gid, uint pid);
	bool batch_check(checking_unit *cu, uint num);
	bool batch_meet(meeting_unit *m, uint num);

	void merge_node(uint cur_node);
	void split_node(uint cur_node);
	void filter();
	void update_schema();
	void reachability();

	void claim_space();
	size_t space_claimed(){
		size_t total = 0;
		for(int i=0;i<100;i++){
			total += data_size[i];
		}
		return total;
	}
	void reset(){
		// reset the number of objects in each grid
		for(int i=0;i<grids_stack_capacity;i++){
			grid_counter[i] = 0;
		}
		grid_check_counter = 0;
	}
	inline uint get_grid_size(uint gid){
		assert(gid<grids_stack_capacity);
		return min(grid_counter[gid],grid_capacity);
	}
	inline uint *get_grid(uint gid){
		assert(gid<grids_stack_capacity);
		return grids + gid*grid_capacity;
	}

	void update_meetings();

	void analyze_grids();
	void analyze_reaches();
	void print_profile();

	void lock(uint key = 0){
		pthread_mutex_lock(&insert_lk[key%MAX_LOCK_NUM]);
	}
	void unlock(uint key = 0){
		pthread_mutex_unlock(&insert_lk[key%MAX_LOCK_NUM]);
	}

    box make_bit_box(box b);

    bool search_memtable(uint64_t pid, vector<__uint128_t> & v_keys, vector<uint> & v_indices);
    //bool id_search_in_disk(uint pid, time_query * tq);
    //bool mbr_search_in_disk(box b, time_query * tq);
    uint search_time_in_disk(time_query * tq);
    //bool id_search_in_CTB(uint pid, uint CTB_id, time_query * tq);
    //bool mbr_search_in_CTB(box b, uint CTB_id, unordered_set<uint> &uni, time_query * tq);

    void load_CTF_keys(uint CTB_id, uint CTF_id);
    std::future<void> load_CTF_keys_async(uint CTB_id, uint CTF_id);
    void load_big_sorted_run(uint b);
    void clear_all_keys();
    //box parse_to_real_mbr(unsigned short first_low, unsigned short first_high, uint64_t value);

    void dump_meetings(uint st);
    void load_meetings(uint st);
    void dump_meta(const char *path);
    void dump_CTB_meta(const char *path, int i);
    void load_CTB_meta(const char *path, int i);
};
extern void lookup_rec(QTSchema *schema, Point *p, uint curnode, vector<uint> &gids, double max_dist, bool include_owner = false);
extern void lookup_stack(QTSchema *schema, Point *p, uint curnode, vector<uint> &gids, double max_dist, bool include_owner = false);

extern void clear_cache();
extern workbench * load_meta(const char *path);

class new_bench : public workbench{
public:
    vector<box_search_info> box_search_queue;
    vector<future<void>> futures;

    ThreadPool& pool = ThreadPool::getInstance();
    atomic<long long> search_count;
    //vector<id_search_info> id_search_queue;
    CTB * compacted_ctbs = nullptr;


    new_bench(configuration * config) : workbench(config){}
    bool mbr_search_in_CTB(box b, uint CTB_id, unordered_set<uint> &uni, time_query * tq);
    bool mbr_search_in_disk(box b, time_query * tq, uint CTB_id);
    bool id_search_in_CTB(uint pid, uint CTB_id, time_query * tq);
    bool id_search_in_disk(uint pid, time_query * tq);
};


#endif /* SRC_TRACING_WORKBENCH_H_ */
