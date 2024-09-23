/*
 * process.cpp
 *
 *  Created on: Feb 11, 2021
 *      Author: teng
 */

#include "trace.h"


/*
 * functions for tracer
 *
 * */



tracer::tracer(configuration *conf, box &b, Point *t, trace_generator *gen){
	trace = t;
	mbr = b;
	config = conf;
	part = new partitioner(mbr,config);
    generator = gen;
#ifdef USE_GPU
	if(config->gpu){
		vector<gpu_info *> gpus = get_gpus();
		if(gpus.size()==0){
			log("no GPU is found, use CPU mode");
			config->gpu = false;
		}else{
			assert(config->specific_gpu<gpus.size());
			gpu = gpus[config->specific_gpu];
			gpu->print();
			for(int i=0;i<gpus.size();i++){
				if(i!=config->specific_gpu){
					delete gpus[i];
				}
			}
			gpus.clear();
		}
	}
#endif
}
tracer::tracer(configuration *conf){
	config = conf;
	//loadMeta(config->trace_path.c_str());
	part = new partitioner(mbr,config);
#ifdef USE_GPU
	if(config->gpu){
		vector<gpu_info *> gpus = get_gpus();
		if(gpus.size()==0){
			log("not GPU is found, use CPU mode");
			config->gpu = false;
		}else{
			assert(config->specific_gpu<gpus.size());
			gpu = gpus[config->specific_gpu];
			gpu->print();
			for(int i=0;i<gpus.size();i++){
				if(i!=config->specific_gpu){
					delete gpus[i];
				}
			}
			gpus.clear();
		}
	}
#endif
}
tracer::~tracer(){
	if(part){
		delete part;
	}
	if(bench){
		delete bench;
	}
    if(trace){
        delete[] trace;
    }
    if(generator){
        delete generator;
    }
//    if(config){
//        delete config;
//    }
#ifdef USE_GPU
	if(gpu){
		delete gpu;
	}
#endif
}

void tracer::dumpTo(const char *path) {
	struct timeval start_time = get_cur_time();
	ofstream wf(path, ios::out|ios::binary|ios::trunc);
	wf.write((char *)&config->num_objects, sizeof(config->num_objects));
	wf.write((char *)&config->duration, sizeof(config->duration));
	wf.write((char *)&mbr, sizeof(mbr));
	size_t num_points = config->duration*config->num_objects;
    cout<<"config->duration:"<<config->duration<<endl;
	wf.write((char *)trace, sizeof(Point)*num_points);
	wf.close();
	logt("dumped to %s",start_time,path);
}

//void tracer::loadMeta(const char *path) {
//
//	uint true_num_objects;
//	uint true_duration;
//	ifstream in(path, ios::in | ios::binary);
//	if(!in.is_open()){
//		log("%s cannot be opened",path);
//		exit(0);
//	}
//	in.read((char *)&true_num_objects, sizeof(true_num_objects));
//	in.read((char *)&true_duration, sizeof(true_duration));
//	log("%d objects last for %d seconds in file",true_num_objects,true_duration);
//	in.read((char *)&mbr, sizeof(mbr));
//	mbr.to_squre(true);
//	mbr.print();
//	assert((size_t)config->num_objects*(config->start_time+config->duration)<=(size_t)true_num_objects*true_duration);
//	//assert(config->num_objects<=true_num_objects);
//	assert(config->start_time+config->duration<=true_duration);
//	in.close();
//}

void tracer::loadData(const char *path, int st) {
    log("loading locations from %d to %d",st, st + 100);
    struct timeval start_time = get_cur_time();
    string filename = path + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ifstream in(filename, ios::in | ios::binary);
    if(!in.is_open()){
        log("%s cannot be opened",filename.c_str());
        exit(0);
    }
    if(!trace){
        //trace = (Point *)malloc(min((uint)100,config->duration)*config->num_objects*sizeof(Point));
        trace = new Point[config->num_objects*100];
    }
    in.read((char *)trace, config->num_objects * 100 * sizeof(Point));
    in.close();
    logt("loaded %d objects last for 100 seconds start from %d time from %s",start_time, config->num_objects, st, filename.c_str());
}

void tracer::loadData(const char *path, int st, int duration) {

	log("loading locations from %d to %d",st,st+duration);
	assert(duration<=100);
	uint true_num_objects;
	uint true_duration;
	box mbr;
	struct timeval start_time = get_cur_time();
	ifstream in(path, ios::in | ios::binary);
	if(!in.is_open()){
		log("%s cannot be opened",path);
		exit(0);
	}
	in.read((char *)&true_num_objects, sizeof(true_num_objects));               //also read meta
	in.read((char *)&true_duration, sizeof(true_duration));
	in.read((char *)&mbr, sizeof(mbr));

	assert((size_t)config->num_objects*(st+duration)<=(size_t)true_num_objects*true_duration);
	//assert(config->num_objects<=true_num_objects);
	assert(st+duration<=true_duration);

	in.seekg(st*true_num_objects*sizeof(Point), ios_base::cur);
	if(!trace){
        //trace = (Point *)malloc(min((uint)100,config->duration)*config->num_objects*sizeof(Point));
        trace = new Point[config->num_objects*100];
	}

	uint loaded = 0;
	while(loaded<config->num_objects){
		uint cur_num_objects = min(true_num_objects,config->num_objects-loaded);
		for(int i=0;i<duration;i++){
			in.read((char *)(trace+i*config->num_objects+loaded), cur_num_objects*sizeof(Point));
			if(true_num_objects>cur_num_objects){
				in.seekg((true_num_objects-cur_num_objects)*sizeof(Point), ios_base::cur);
			}
		}
		loaded += cur_num_objects;
	}

	in.close();
	logt("loaded %d objects last for %d seconds start from %d time from %s",start_time, config->num_objects, duration, st, path);
}

void tracer::print(){
	print_points(trace,config->num_objects,min(config->num_objects,(uint)10000));
}

void tracer::print_trace(int oid){
	vector<Point *> points;
	for(int i=0;i<config->cur_duration;i++){
		points.push_back(trace+i*config->num_objects+oid);
	}
	print_points(points);
	points.clear();
}
void tracer::print_traces(){
	vector<Point *> points;
	for(int oid=0;oid<config->num_objects;oid++){
		for(int i=0;i<config->duration;i++){
			points.push_back(trace+i*config->num_objects+oid);
		}
	}
	print_points(points, 10000);
	points.clear();
}

inline bool BloomFilter_Check(workbench *bench, uint sst, uint pid){
    uint pdwHashPos;
    uint64_t hash1, hash2;
    int ret;
    for (int i = 0; i < bench->dwHashFuncs; i++){
        hash1 = MurmurHash2_x64((const void *)&pid, sizeof(uint), bench->dwSeed);
        hash2 = MurmurHash2_x64((const void *)&pid, sizeof(uint), MIX_UINT64(hash1));
        pdwHashPos = (hash1 + i*hash2) % bench->dwFilterBits;
        ret = bench->pstFilter[sst][pdwHashPos/8] & (1<<(pdwHashPos%8));        //0 1 ... 128 ...
        if(ret == 0)
            return false;
    }
    return true;
}

uint search_keys_by_pid(__uint128_t* keys, uint64_t wp, uint capacity, vector<__uint128_t> & v_keys, vector<uint> & v_indices){
    uint count = 0;
    //cout<<"into search_SSTable wp "<< wp <<endl;
    int find = -1;
    int low = 0;
    int high = capacity - 1;
    int mid;
    uint64_t temp_wp;
    while (low <= high) {
        mid = (low + high) / 2;
        temp_wp = keys[mid] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
//        cout << get_key_wid(keys[mid]) <<'-' << get_key_pid(keys[mid]) << endl;
//        cout << "temp_wp" << temp_wp << endl;
        if (temp_wp == wp){
            find = mid;
            break;
        }
        else if (temp_wp > wp){
            high = mid - 1;
        }
        else {
            low = mid + 1;
        }
    }
    if(find==-1){
        //cout<<"cannot find"<<endl;
        return 0;
    }
    //cout<<"exactly find"<<endl;
    uint cursor = find;
    while(temp_wp == wp && cursor >= 1){
        cursor--;
        temp_wp = keys[cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
    }
    if(temp_wp == wp && cursor == 0){
        count++;
        v_keys.push_back(keys[cursor]);
        v_indices.push_back(cursor);
    }
    while(cursor+1<capacity){
        cursor++;
        temp_wp = keys[cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
        if(temp_wp == wp){
            count++;
            v_keys.push_back(keys[cursor]);
            v_indices.push_back(cursor);
        }
        else break;
    }
    //cout<<"find !"<<endl;
    return count;
}

//void *merge_dump(void *arg){
//    cout<<"step into the sst_dump"<<endl;
//    workbench *bench = (workbench *)arg;
//    cout<<"cur_time: "<<bench->cur_time<<endl;
//    uint offset = 0;
//    assert(bench->config->MemTable_capacity%2==0);
//    uint old_big = bench->big_sorted_run_count;                 //atomic add
//    cout<<"old big_sorted_run_count: "<<old_big<<endl;
//
//    bench->dumping = true;
//    if(old_big%2==1){
//        offset = bench->config->MemTable_capacity/2;
//    }
//    bench->bg_run[old_big].sst = NULL;
//
//    //new
//
//    cout<<"sst_capacity:"<<bench->SSTable_kv_capacity<<endl;
//    bench->merge_kv_capacity = bench->config->kv_restriction * bench->config->MemTable_capacity/2;
//    __uint128_t *temp_keys = new __uint128_t[bench->merge_kv_capacity];
//    box *temp_real_mbrs = new box[bench->merge_kv_capacity];
//    bench->merge_sstable_count = bench->config->SSTable_count * bench->config->MemTable_capacity/2;
//    bench->bg_run[old_big].wids = new unsigned short[bench->config->num_objects];
//    bench->bg_run[old_big].bitmaps = new unsigned char[bench->bit_count/8*bench->merge_sstable_count];
//    bench->bg_run[old_big].bitmap_mbrs = new box[bench->merge_sstable_count];
//    bench->bg_run[old_big].first_widpid = new uint64_t[bench->merge_sstable_count];
//
//
//    vector< vector<__uint128_t> > keys_with_wid(bench->bit_count);
//    vector< vector<box> > mbrs_with_wid(bench->bit_count);
//    struct timeval bg_start = get_cur_time();
//    uint wrong_count = 0;
//    vector<__uint128_t> keys_with_this_pid;
//    vector<box> mbrs_with_this_pid;
//    vector<uint> indices_with_this_pid;
//    for(uint i = 0; i < bench->config->num_objects; i++){
//        //cout << "i " << i <<endl;
//        uint64_t wp = 0;
//        uint x = 0, y = 0, not_zero_count = 0;
//
//        //search and get
//        for(uint j = 0; j < bench->config->MemTable_capacity/2; j++){
//            if(bench->h_sids[offset+j][i] == 0){
//                continue;
//            }
//            uint temp_x, temp_y;
//            d2xy(SID_BIT / 2, bench->h_sids[offset + j][i], temp_x, temp_y);
//            x += temp_x;
//            y += temp_y;
//            not_zero_count++;
//
//            wp = ((uint64_t)bench->h_sids[offset+j][i] << OID_BIT) + i;
//            //cout << bench->h_sids[offset+j][i] <<'-'<< i << endl;
//            uint find_count = search_keys_by_pid(bench->h_keys[offset+j], wp, bench->config->kv_restriction, keys_with_this_pid, indices_with_this_pid);
//            uint old_keys_count = keys_with_this_pid.size() - indices_with_this_pid.size();
//            if(find_count){
//                //cout << " find !" << endl;
//                for(uint a = 0; a < indices_with_this_pid.size(); a++){
//                    uint bitmap_id = indices_with_this_pid[a]/bench->SSTable_kv_capacity;
//                    box temp_mbr;
//                    parse_mbr(keys_with_this_pid[old_keys_count+a], temp_mbr, bench->h_bitmap_mbrs[offset+j][bitmap_id]);
//                    mbrs_with_this_pid.push_back(temp_mbr);
//                }
//                indices_with_this_pid.clear();
//            }
//            else{
//                wrong_count++;
//            }
//        }
//        if(keys_with_this_pid.empty()){
//            continue;
//        }
//
//        //rewrite wid, bitmap and its mbr
//        x /= not_zero_count;
//        y /= not_zero_count;
//        bench->bg_run[old_big].wids[i] = xy2d(SID_BIT / 2, x, y);
//        if(bench->bg_run[old_big].wids[i]==0){
//            bench->bg_run[old_big].wids[i] = 1;
//        }
//        for(uint k = 0; k < keys_with_this_pid.size(); k++){
//            keys_with_this_pid[k] = (keys_with_this_pid[k] & (((__uint128_t)1 << (OID_BIT * 2 + MBR_BIT + DURATION_BIT + END_BIT)) - 1))
//                                 + ((__uint128_t)bench->bg_run[old_big].wids[i] << (OID_BIT * 2 + MBR_BIT + DURATION_BIT + END_BIT));
//            keys_with_wid[bench->bg_run[old_big].wids[i]].push_back(keys_with_this_pid[k]);
//            mbrs_with_wid[bench->bg_run[old_big].wids[i]].push_back(mbrs_with_this_pid[k]);
//        }
//        keys_with_this_pid.clear();
//        mbrs_with_this_pid.clear();
//    }
//
//    double form_vec_time = get_time_elapsed(bg_start,false);
//    fprintf(stdout,"\tform_vec_time:\t%.2f\n",form_vec_time);
//    //unroll
//    uint64_t total_index = 0;
//    for(uint i = 0; i < bench->bit_count; i++){
//        if(!keys_with_wid[i].empty()){
//            copy(keys_with_wid[i].begin(), keys_with_wid[i].end(), temp_keys + total_index);
//            copy(mbrs_with_wid[i].begin(), mbrs_with_wid[i].end(), temp_real_mbrs + total_index);
//            total_index += keys_with_wid[i].size();
//        }
//    }
//    bench->pro.bg_merge_time += get_time_elapsed(bg_start,true);
//    cout << "total_index " << total_index << " bench->merge_kv_capacity " << bench->merge_kv_capacity << endl;          //but why??? after this, still merge_kv_capacity
//    cout << "wrong_count" << wrong_count << endl;
//
////    for(uint i = 0; i < 100; i++){
////        print_parse_key(temp_keys[i]);
////    }
//
//    //especially slow wirte_bitmap
//#pragma omp parallel for num_threads(64)
//    for(uint i = 0; i < total_index; i++){             //i < bench->merge_kv_capacity
//        uint low0 = (temp_real_mbrs[i].low[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * ((1ULL << (SID_BIT / 2)) - 1);
//        uint low1 = (temp_real_mbrs[i].low[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * ((1ULL << (SID_BIT / 2)) - 1);
//        uint high0 = (temp_real_mbrs[i].high[0] - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * ((1ULL << (SID_BIT / 2)) - 1);
//        uint high1 = (temp_real_mbrs[i].high[1] - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * ((1ULL << (SID_BIT / 2)) - 1);
//        uint bitmap_id = i/bench->SSTable_kv_capacity;
//        for(uint m=low0;m<=high0;m++){
//            for(uint n=low1;n<=high1;n++){
//                uint bit_pos = xy2d(SID_BIT / 2, m, n);
//                bench->bg_run[old_big].bitmaps[bitmap_id*(bench->bit_count/8)+bit_pos/8] |= (1<<(bit_pos%8));
//            }
//        }
//        //bench->bg_run[old_big].bitmap_mbrs[bitmap_id].update(temp_real_mbrs[i]);        //not use bitmap
////        if(i%100000==0){
////            cout << i << endl;
////        }
//    }
//    double write_bitmap_time = get_time_elapsed(bg_start,true);
//    fprintf(stdout,"\twrite_bitmap_time:\t%.2f\n",write_bitmap_time);
//
//    //output bitmap
//    cerr << "output picked bitmap" << endl;
//    Point * bit_points = new Point[bench->bit_count];
//    uint count_p;
//    for(uint i = 0; i < bench->merge_sstable_count; i++) {
//        cerr << endl;
//        count_p = 0;
//        for (uint j = 0; j < bench->bit_count; j++) {
//            if (bench->bg_run[old_big].bitmaps[i * (bench->bit_count / 8) + j / 8] & (1 << (j % 8))) {
//                Point bit_p;
//                uint x = 0, y = 0;
//                d2xy(SID_BIT / 2, j, x, y);
//                bit_p.x = (double) x / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[0] - bench->mbr.low[0]) +
//                          bench->mbr.low[0];           //int low0 = (f_low0 - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * (pow(2,WID_BIT/2) - 1);
//                bit_p.y = (double) y / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[1] - bench->mbr.low[1]) +
//                          bench->mbr.low[1];               //int low1 = (f_low1 - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * (pow(2,WID_BIT/2) - 1);
//                bit_points[count_p] = bit_p;
//                count_p++;
//            }
//        }
//        cout << "bit_points.size():" << count_p << endl;
//        print_points(bit_points, count_p);
//    }
//    delete[] bit_points;
//    double output_bitmap = get_time_elapsed(bg_start,true);
//    fprintf(stdout,"\toutput_bitmap:\t%.2f\n",output_bitmap);
//
//    //bitmap mbr
////#pragma omp parallel for num_threads(bench->config->num_threads)
//    for(uint i = 0; i < bench->merge_sstable_count; i++){
//        box temp_bitbox;
//        for(uint j = 0; j < bench->bit_count; j++){
//            if(bench->bg_run[old_big].bitmaps[i*(bench->bit_count/8) + j/8] & (1<<(j%8)) ){
//                uint x, y;
//                d2xy(SID_BIT / 2, j, x, y);
//                Point temp_p(x, y);
//                temp_bitbox.update(temp_p);
//            }
//        }
//        bench->bg_run[old_big].bitmap_mbrs[i].low[0] = temp_bitbox.low[0] / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
//        bench->bg_run[old_big].bitmap_mbrs[i].low[1] = temp_bitbox.low[1] / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
//        bench->bg_run[old_big].bitmap_mbrs[i].high[0] = temp_bitbox.high[0] / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
//        bench->bg_run[old_big].bitmap_mbrs[i].high[1] = temp_bitbox.high[1] / ((1ULL << (SID_BIT / 2)) - 1) * (bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
//    }
//    double bitmap_mbr_time = get_time_elapsed(bg_start,true);
//    fprintf(stdout,"\tbitmap_mbr_time:\t%.2f\n",bitmap_mbr_time);
//
//    for(uint i = 0; i < bench->merge_sstable_count; i ++) {
//        bench->bg_run[old_big].bitmap_mbrs[i].print();
//    }
//    get_time_elapsed(bg_start,true);
//
//    //write kv_mbr
//    for(uint i = 0; i < bench->merge_kv_capacity; i++){
//        uint bitmap_id = i/bench->SSTable_kv_capacity;
//        __uint128_t value_mbr = serialize_mbr(&temp_real_mbrs[i], &bench->bg_run[old_big].bitmap_mbrs[bitmap_id]);
//        temp_keys[i] = (temp_keys[i] &~( ( ( (__uint128_t)1 << MBR_BIT) - 1) << (DURATION_BIT + END_BIT) ) )
//                                + (value_mbr << (DURATION_BIT + END_BIT));
//    }
//    double write_kv_mbr_time = get_time_elapsed(bg_start,true);
//    fprintf(stdout,"\twrite_kv_mbr_time:\t%.2f\n",write_kv_mbr_time);
//
//    //dump
//    ofstream SSTable_of;
//    for(uint i = 0; i < bench->merge_kv_capacity; i += bench->SSTable_kv_capacity) {
//        uint sst_id = i / bench->SSTable_kv_capacity;
//        bench->bg_run[old_big].first_widpid[sst_id] = temp_keys[i] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
//        //print_parse_key(temp_keys[i]);
//        SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-"+to_string(sst_id), ios::out|ios::binary|ios::trunc);
//        bench->pro.bg_open_time += get_time_elapsed(bg_start,true);
//        SSTable_of.write((char *)(temp_keys + i), sizeof(__uint128_t)*bench->SSTable_kv_capacity);
//        SSTable_of.flush();
//        SSTable_of.close();
//        bench->pro.bg_flush_time += get_time_elapsed(bg_start,true);
//    }
//
//    fprintf(stdout,"\tmerge :\t%.2f\n",bench->pro.bg_merge_time);
//    fprintf(stdout,"\tflush:\t%.2f\n",bench->pro.bg_flush_time);
//    fprintf(stdout,"\topen:\t%.2f\n",bench->pro.bg_open_time);
//
//    bench->bg_run[old_big].start_time_min = bench->start_time_min;
//    bench->bg_run[old_big].start_time_max = bench->start_time_max;
//    bench->bg_run[old_big].end_time_min = bench->end_time_min;
//    bench->bg_run[old_big].end_time_max = bench->end_time_max;
//    bench->end_time_min = bench->end_time_max;              //new min = old max
//    bench->start_time_min = (1ULL<<32) -1;
//    bench->start_time_max = 0;
//    delete[] temp_keys, temp_real_mbrs;
//    bench->bg_run[old_big].print_meta();
//    bench->dumping = false;
//    //logt("merge sort and flush", bg_start);
//    return NULL;
//}

//void *crash_merge_dump(void *arg){
//    cout<<"step into the sst_dump"<<endl;
//    workbench *bench = (workbench *)arg;
//    cout<<"cur_time: "<<bench->cur_time<<endl;
//    uint offset = 0;
//    assert(bench->config->MemTable_capacity%2==0);
//    uint old_big = bench->big_sorted_run_count;                 //atomic add
//    cout<<"old big_sorted_run_count: "<<old_big<<endl;
//    bench->big_sorted_run_count++;
//    if(old_big%2==1){
//        offset = bench->config->MemTable_capacity/2;
//    }
//    bench->bg_run[old_big].start_time_min = bench->start_time_min;
//    bench->bg_run[old_big].start_time_max = bench->start_time_max;
//    bench->bg_run[old_big].end_time_min = bench->end_time_min;
//    bench->bg_run[old_big].end_time_max = bench->end_time_max;
//    bench->end_time_min = bench->end_time_max;              //new min = old max
//    bench->bg_run[old_big].first_widpid = new uint64_t[bench->config->SSTable_count];
//
//    //merge sort
//    ofstream SSTable_of;
//    uint kv_count = 0;
//    uint sst_count = 0;
//    cout<<"sst_capacity:"<<bench->SSTable_kv_capacity<<endl;
//    key_value *temp_keys = new key_value[bench->SSTable_kv_capacity];
//    uint *key_index = new uint[bench->config->MemTable_capacity/2]{0};
//    int finish = 0;
//    uint64_t temp_key;
//    uint taken_id = 0;
//    struct timeval bg_start = get_cur_time();
//    while(finish<bench->MemTable_count){
//        if(kv_count==0){
//            SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-"+to_string(sst_count), ios::out|ios::binary|ios::trunc);
//            assert(SSTable_of.is_open());
//            bench->pro.bg_open_time += get_time_elapsed(bg_start,true);
//        }
//        finish = 0;
//        temp_key = UINT64_MAX;
//        taken_id = 0;
//        for(int i=0;i<bench->MemTable_count; i++){
//            if( bench->h_keys[offset+i][key_index[i]] == 0){              //empty kv
//                finish++;
//                continue;
//            }
////            if(key_index[i]>= bench->config->kv_restriction){              //empty kv
////                finish++;
////                continue;
////            }
//            if( temp_key > bench->h_keys[offset+i][key_index[i]] ){
//                temp_key = bench->h_keys[offset+i][key_index[i]];
//                taken_id = i;
//            }
//        }
//        if(finish<bench->MemTable_count){
//            temp_keys[kv_count].key = temp_key;
//            temp_keys[kv_count].value = bench->h_values[offset + taken_id][key_index[taken_id]];     //box
//            if(kv_count==0){
//                bench->bg_run[old_big].first_widpid[sst_count] = temp_key >> 23;
//            }
//            bench->h_keys[offset + taken_id][key_index[taken_id]] = 0;                                     //init
//            key_index[taken_id]++;                                                                  // one while, one kv
//            kv_count++;
//        }
//        if(kv_count==bench->SSTable_kv_capacity||finish==bench->MemTable_count){
//            bench->pro.bg_merge_time += get_time_elapsed(bg_start,true);
//            SSTable_of.write((char *)temp_keys, sizeof(key_value)*kv_count);
//            SSTable_of.flush();
//            SSTable_of.close();
//            bench->pro.bg_flush_time += get_time_elapsed(bg_start,true);
//            sst_count++;
//            kv_count = 0;
//        }
//    }
//    cout<<"sst_count :"<<sst_count<<" less than"<<1024<<endl;
//    for(int i=0;i<bench->MemTable_count; i++){
//        cout<<"key_index"<<key_index[i]<<endl;
//    }
//    delete[] key_index;
//    bench->bg_run[old_big].print_meta();
//    return NULL;
//}

void *parallel_load(void *arg){
    query_context *ctx = (query_context *)arg;
    ofstream SSTable_of;
    SSTable_of.open((char *)ctx->target[1] , ios::out|ios::binary|ios::trunc);
    __uint128_t * keys = (__uint128_t *)ctx->target[0];
    //print_128(keys[0]);
    SSTable_of.write((char *)keys, *(int *)ctx->target[2]);
    SSTable_of.flush();
    SSTable_of.close();
    return NULL;
}

void *straight_dump(void *arg){
    cout<<"step into the sst_dump"<<endl;
    workbench *bench = (workbench *)arg;
    cout<<"cur_time: "<<bench->cur_time<<endl;
    uint offset = 0;
    assert(bench->config->MemTable_capacity%2==0);
    uint old_big = bench->ctb_count;                 //atomic add
    cout<<"old big_sorted_run_count: "<<old_big<<endl;
    //bench->big_sorted_run_count++;
    bench->dumping = true;
    if(old_big%2==1){
        offset = bench->config->MemTable_capacity/2;
    }
    //CTB temp_ctb;
//    bench->ctbs.push_back(CTB());
    bench->ctbs[old_big].ctfs = NULL;
    bench->ctbs[old_big].start_time_min = bench->start_time_min;
    bench->ctbs[old_big].start_time_max = bench->start_time_max;
    bench->ctbs[old_big].end_time_min = bench->end_time_min;
    bench->ctbs[old_big].end_time_max = bench->end_time_max;
    bench->end_time_min = bench->end_time_max;              //new min = old max
    bench->ctbs[old_big].first_widpid = new uint64_t[bench->config->CTF_count];
    bench->ctbs[old_big].sids = new unsigned short[bench->config->num_objects];
    copy(bench->h_sids[offset], bench->h_sids[offset] + bench->config->num_objects, bench->ctbs[old_big].sids);
    bench->ctbs[old_big].bitmaps = new unsigned char[bench->bitmaps_size];
    copy(bench->h_bitmaps[offset], bench->h_bitmaps[offset] + bench->bitmaps_size, bench->ctbs[old_big].bitmaps);
    bench->ctbs[old_big].bitmap_mbrs = new box[bench->config->CTF_count];
    copy(bench->h_bitmap_mbrs[offset], bench->h_bitmap_mbrs[offset] + bench->config->CTF_count, bench->ctbs[old_big].bitmap_mbrs);
    bench->ctbs[old_big].CTF_capacity = new uint[bench->config->CTF_count];
    copy(bench->h_CTF_capacity[offset], bench->h_CTF_capacity[offset] + bench->config->CTF_count, bench->ctbs[old_big].CTF_capacity);
    bench->ctbs[old_big].o_buffer.oversize_kv_count = bench->h_oversize_buffers[offset].oversize_kv_count;
    bench->ctbs[old_big].o_buffer.keys = new __uint128_t[bench->ctbs[old_big].o_buffer.oversize_kv_count];
    copy(bench->h_oversize_buffers[offset].keys, bench->h_oversize_buffers[offset].keys + bench->ctbs[old_big].o_buffer.oversize_kv_count, bench->ctbs[old_big].o_buffer.keys);
    bench->ctbs[old_big].o_buffer.boxes = new f_box[bench->ctbs[old_big].o_buffer.oversize_kv_count];
    copy(bench->h_oversize_buffers[offset].boxes, bench->h_oversize_buffers[offset].boxes + bench->ctbs[old_big].o_buffer.oversize_kv_count, bench->ctbs[old_big].o_buffer.boxes);

    cout << "test oversize" << endl;
    for(uint i = 0; i < min((uint)100, bench->ctbs[old_big].o_buffer.oversize_kv_count); i++){
        print_parse_key(bench->ctbs[old_big].o_buffer.keys[i]);
            cout << bench->ctbs[old_big].o_buffer.boxes[i].low[0] << endl;
        cout << "sid" << bench->ctbs[old_big].sids[get_key_oid(bench->ctbs[old_big].o_buffer.keys[i]) ] << endl;
    }

    bench->ctbs[old_big].box_rtree = new RTree<short *, double, 2, double>();
    cout <<"before insert"<< endl;
    for(uint i = 0; i < bench->config->CTF_count; ++i){
        bench->ctbs[old_big].box_rtree->Insert(bench->h_bitmap_mbrs[offset][i].low, bench->h_bitmap_mbrs[offset][i].high, new short(i));
    }

    cout << "CTF_capacitys:" << endl;
    for(uint i = 0; i < bench->config->CTF_count; i++){
        cout << bench->ctbs[old_big].CTF_capacity[i] << endl;
    }
//    cerr << "all bitmap_mbrs" << endl;
//    for(uint i = 0; i < bench->config->SSTable_count; i++){
//        bench->bg_run[old_big].bitmap_mbrs[i].print();
//    }

    uint total_index = 0;
    uint sst_count = 0;
    struct timeval bg_start = get_cur_time();
    pthread_t threads[bench->config->CTF_count];        //may be larger than config->num_threads
    for(sst_count=0; sst_count<bench->config->CTF_count; sst_count++){
        bench->ctbs[old_big].first_widpid[sst_count] = bench->h_keys[offset][total_index] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
//        cout<<bench->bg_run[old_big].first_widpid[sst_count]<<endl;
//        cout<<get_key_wid(bench->h_keys[offset][total_index])<<endl;
//        cout<<get_key_pid(bench->h_keys[offset][total_index])<<endl<<endl;
        total_index += bench->h_CTF_capacity[offset][sst_count];
        //assert(total_index<=bench->config->kv_restriction);
        string keys_path = bench->config->raid_path +to_string(old_big)+"-"+to_string(sst_count);
        int SIZE = sizeof(__uint128_t)*bench->h_CTF_capacity[offset][sst_count];
        query_context tctx;
        tctx.target[0] = (void *)(bench->h_keys[offset] + total_index);
        tctx.target[1] = (void *)keys_path.c_str();
        tctx.target[2] = (void *)&SIZE;
        pthread_create(&threads[sst_count], NULL, parallel_load, (void *)&tctx);
    }
    //but, the last sst may not be full
    for(int i = 0; i < bench->config->CTF_count; i++ ){
        void *status;
        pthread_join(threads[i], &status);
    }
    bench->pro.bg_flush_time += get_time_elapsed(bg_start,true);

    fprintf(stdout,"\tmerge sort:\t%.2f\n",bench->pro.bg_merge_time);
    fprintf(stdout,"\tflush:\t%.2f\n",bench->pro.bg_flush_time);
    fprintf(stdout,"\topen:\t%.2f\n",bench->pro.bg_open_time);
    //cout<<"sst_count :"<<sst_count<<" less than"<<1024<<endl;

    bench->ctbs[old_big].print_meta();
    //logt("merge sort and flush", bg_start);
    //delete[] bit_points;
    bench->dumping = false;
    return NULL;
}

void* commandThreadFunction(void* arg) {
    workbench *bench = (workbench *)arg;
    cout << "bench->config->SSTable_count: " << bench->config->CTF_count << endl;
    pthread_mutex_init(&bench->mutex_i,NULL);
    while (true) {
        pthread_mutex_lock(&bench->mutex_i);
        std::string command;
        std::cout << "Enter command: ";
        std::cin >> command;
        if (command == "interrupt") {
            cout<<"will interrupt in next round"<<endl;
            bench->interrupted = true;
        } else if (command == "exit") {
            cout<<"will exit"<<endl;
            break;
        } else {
            std::cout << "Unknown command: " << command << std::endl;
            pthread_mutex_unlock(&bench->mutex_i);
        }
    }
    return NULL;
}

#ifdef USE_GPU
workbench *cuda_create_device_bench(workbench *bench, gpu_info *gpu);
void process_with_gpu(workbench *bench,workbench *d_bench, gpu_info *gpu);
#endif

void tracer::process(){
	struct timeval start = get_cur_time();
    std::cout << "Running main program..." << std::endl;
	for(int st=config->start_time;st<config->start_time+config->duration;st+=100){
        config->cur_duration = min((config->start_time+config->duration-st),(uint)100);
        if(config->load_data){
            //loadData(config->trace_path.c_str(),st,config->cur_duration);
            loadData(config->trace_path.c_str(),st);
        }
        else if(!config->load_meetings_pers){
//            cout << "config.cur_duration : "<< config->cur_duration <<endl;
//            generator->map->print_region();
//            sleep(2);
            if(true){
                cout << st << endl;
                generator->map->check_Streets();
            }
            generator->generate_trace(trace);
        }
		start = get_cur_time();
        if(!bench){
            bench = part->build_schema(trace, config->num_objects);
            bench->mbr = mbr;
            bench->end_time_min = config->start_time + config->min_meet_time;           //first min time
            bench->start_time_min = (1ULL<<32) -1;
            bench->start_time_max = 0;

//            //command
//            int ret1;
//            if ((ret1 = pthread_create(&bench->command_thread, NULL, commandThreadFunction, (void *) bench)) != 0) {
//                fprintf(stderr, "pthread_create:%s\n", strerror(ret1));
//            }
//            pthread_detach(bench->command_thread);
#ifdef USE_GPU
            if(config->gpu){
				d_bench = cuda_create_device_bench(bench, gpu);
			}
#endif
        }
        if(config->load_meetings_pers){
            bench->load_meetings(st);
        }

		for(int t=0;t<config->cur_duration;t++){
			log("");
			bench->reset();
			bench->points = trace+t*config->num_objects;
			bench->cur_time = st + t;
            if(bench->cur_time==config->start_time+config->duration-1){         //finish and all dump
                //pthread_cancel(bench->command_thread);
                //bench->crash_consistency = true;
            }
			// process the coordinate in this time point

            if(bench->interrupted){
                bench->search_single = true;
                cout<<"search pid: ";
                cin>>bench->search_single_pid;
                cout<<"valid_timestamp: ";
                cin>>bench->valid_timestamp;
                cout<<endl;
            }

			if(!config->gpu){
				struct timeval ct = get_cur_time();
				bench->filter();
				bench->pro.filter_time += get_time_elapsed(ct,true);
				bench->reachability();
				bench->pro.refine_time += get_time_elapsed(ct,true);
				//bench->update_meetings();
				//bench->pro.meeting_identify_time += get_time_elapsed(ct,true);
			}else{
#ifdef USE_GPU
                process_with_gpu(bench,d_bench,gpu);
#endif
			}
            if(bench->search_multi){
                bench->search_multi = false;
                cout<<"cuda multi search"<<endl;
                cout<<"cuda multi_find_count: "<<bench->multi_find_count<<endl;
                for(int i=0;i<bench->multi_find_count;i++){
                    cout << bench->search_multi_list[i].pid << "-" << bench->search_multi_list[i].target << "-"
                         << bench->search_multi_list[i].start << "-" << bench->search_multi_list[i].end << "-"
                         << bench->search_multi_list[i].low0 << "-" << bench->search_multi_list[i].low1 << "-"
                         << bench->search_multi_list[i].high0 << "-" << bench->search_multi_list[i].high1 << endl;
                }

                //search memtable
//                struct timeval newstart = get_cur_time();
//                for(int i=0;i<bench->search_multi_length;i++){
//                    bench->search_memtable(bench->search_multi_pid[i]);
//                }
//                bench->pro.search_memtable_time += get_time_elapsed(newstart,false);
//                logt("search memtable",newstart);
//
//                //search disk
//                for(int i=0;i<bench->search_multi_length;i++){
//                    bench->search_in_disk(bench->search_multi_pid[i], bench->valid_timestamp);
//                }
//                bench->pro.search_in_disk_time += get_time_elapsed(newstart,false);
//                logt("search in disk",newstart);
            }
            if(bench->search_single){                                   //search_single
                bench->interrupted = false;                             //reset
                bench->search_single = false;
                bench->search_multi = true;
                cout<<"cuda single search"<<endl;
                cout<<"single_find_count: "<<bench->single_find_count<<endl;
                bench->search_multi_length = bench->single_find_count;
                for(int i=0;i<bench->single_find_count;i++){
                    cout << bench->search_single_pid << "-" << bench->search_single_list[i].target << "-"
                        << bench->search_single_list[i].start << "-" << bench->search_single_list[i].end << "-"
                        << bench->search_single_list[i].low0 << "-" << bench->search_single_list[i].low1 << "-"
                        << bench->search_single_list[i].high0 << "-" << bench->search_single_list[i].high1 << endl;
                    bench->search_multi_pid[i] = bench->search_single_list[i].target;
                }

                struct timeval newstart = get_cur_time();
//                //search memtable
//                bench->search_memtable(bench->search_single_pid);
//                bench->pro.search_memtable_time += get_time_elapsed(newstart,false);
//                logt("search memtable",newstart);

                //search disk
                bench->search_in_disk(bench->search_single_pid, bench->valid_timestamp);
                bench->pro.search_in_disk_time += get_time_elapsed(newstart,false);
                logt("search in disk",newstart);
                pthread_mutex_unlock(&bench->mutex_i);
//                cout<<"final search_multi_length: "<<bench->search_multi_length<<endl;
            }

            if(bench->crash_consistency){
                uint offset = 0;
                if(bench->ctb_count % 2 == 1){
                    offset = bench->config->MemTable_capacity/2;
                }
                for(int i=0;i<bench->MemTable_count; i++){
                    for(int j=0;j<10;j++){
                        print_128(bench->h_keys[offset+i][j]);
                        cout<<(uint)(bench->h_keys[offset+i][j] >> 39)<<endl;
                        //print_128(bench->h_values[offset+i][j]);
                        cout<<endl;
                    }
                    cout<<endl;
                }
                cout<<"crash_consistency, 2 merge sort and dump"<<endl;
                cout << "crash dump begin time: " << bench->cur_time << endl;
                //crash_sst_dump((void *)bench);
                bench->clear();
            }
            else if(bench->MemTable_count==bench->config->MemTable_capacity/2) {    //0 <= MemTable_count <= MemTable_capacity/2
                uint offset = 0;
                if(bench->ctb_count % 2 == 1){
                    offset = bench->config->MemTable_capacity/2;
                }

                Point * bit_points = new Point[bench->bit_count];
                uint count_p;
                for(uint j = 0;j<bench->config->CTF_count; j++){
                    //cerr<<"bitmap"<<j<<endl;
                    cerr<<endl;
                    count_p = 0;
                    bool is_print = false;
                    for(uint i=0;i<bench->bit_count;i++){
                        if(bench->h_bitmaps[offset][j*(bench->bit_count/8) + i/8] & (1<<(i%8))){
                            if(!is_print){
                                cout<<i<<"in SST"<<j<<endl;
                                is_print = true;
                            }
                            Point bit_p;
                            uint x=0,y=0;
                            d2xy(SID_BIT/2,i,x,y);
                            bit_p.x = (double)x/(1ULL << (SID_BIT/2))*(bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];           //int low0 = (f_low0 - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * (pow(2,WID_BIT/2) - 1);
                            bit_p.y = (double)y/(1ULL << (SID_BIT/2))*(bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];               //int low1 = (f_low1 - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * (pow(2,WID_BIT/2) - 1);
                            bit_points[count_p] = bit_p;
                            count_p++;
                        }
                    }
                    cout<<"bit_points.size():"<<count_p<<endl;
                    print_points(bit_points,count_p);
                    //cerr << "process output bitmap finish" << endl;
                }
                delete[] bit_points;

                bench->end_time_max = bench->cur_time;              //old max
                cout<<"meeting_cut_count:"<<bench->meeting_cut_count<<endl;
                cout<<"start_time_min:"<<bench->start_time_min<<"start_time_max:"<<bench->start_time_max<<"bench->end_time_min:"<<bench->end_time_min<<"bench->end_time_max:"<<bench->end_time_max<<endl;

                cout << "dump begin time: " << bench->cur_time << endl;
//                pthread_t bg_thread;
//                int ret;
//                if ((ret = pthread_create(&bg_thread, NULL, merge_dump, (void *) bench)) != 0) {
//                    fprintf(stderr, "pthread_create:%s\n", strerror(ret));
//                }
//                pthread_detach(bg_thread);



//                ofstream p;
//                string filename = "longer_edges" + to_string(bench->ctb_count) + ".csv";
//                cout << filename << endl;
//                p.open(filename, ios::out|ios::binary|ios::trunc);
//                p << "percent(%)" << ',' << "edge_length" << endl;
//                int this_count = 0;
//                for(int i = 0 ; i < bench->config->num_objects; i += 20000){
//                    p << i/20000 << ',' << bench->h_longer_edges[i] << endl;
////                    if(i%1000000==0){
////                        cout << i/1000000 << " " << bench->h_longer_edges[i] << endl;
////                    }
//                }
//                p.close();


//                if(config->MemTable_capacity==2){
//                    straight_dump((void *)bench);
//                }
//                else{
//                    merge_dump((void *)bench);
//                }

                straight_dump((void *)bench);

                //init
                bench->ctb_count++;
                bench->MemTable_count = 0;
                bench->do_some_search = false;

                //bool findit = searchkv_in_all_place(bench, 2);
            }

//            if(!bench->do_some_search && bench->ctb_count == 1){            // !bench->do_some_search && bench->big_sorted_run_count == 1
//                bench->do_some_search = true;
//                while(bench->dumping){
//                    sleep(1);
//                }
//
////                for(uint i = 0 ; i < bench->ctbs[1].o_buffer.oversize_kv_count; i++){
////                    bench->search_in_disk( get_key_oid(bench->ctbs[1].o_buffer.keys[i]), 15);
////                }
//
//                uint question_count = 10000;
//                bench->wid_filter_count = 0;
//                bench->id_find_count = 0;
//                uint pid = 100000;
////                string cmd = "sync; sudo sh -c 'echo 1 > /proc/sys/vm/drop_caches'";        //sudo!!!
////                if(system(cmd.c_str())!=0){
////                    fprintf(stderr, "Error when disable buffer cache\n");
////                }
//                ofstream q;
//                q.open(to_string(config->MemTable_capacity/2)+"search_id.csv", ios::out|ios::binary|ios::trunc);
//                q << "question number" << ',' << "time_consume(ms)" << endl;
//                for(int i = 0; i < question_count; i++){
//                    struct timeval disk_search_time = get_cur_time();
////                    uint temp = bench->config->SSTable_count;
////                    bench->config->SSTable_count = bench->merge_sstable_count;
//                    bench->search_in_disk(pid, 15);
////                    bench->config->SSTable_count = temp;
//                    pid++;
//                    double time_consume = get_time_elapsed(disk_search_time);
//                    //printf("disk_search_time %.2f\n", time_consume);
//                    q << i << ',' << time_consume << endl;
//                }
//                q.close();
//                cout << "question_count:" << question_count << " id_find_count:" << bench->id_find_count <<" kv_restriction:"<< bench->config->kv_restriction << endl;
//                cout << "wid_filter_count:" << bench->wid_filter_count <<"id_not_find_count"<<bench->id_not_find_count<<endl;
//
////                double mid_x = -87.678503;
////                double mid_y = 41.856803;
////                Point the_mid(mid_x, mid_y);
////                the_mid.print();
//                double mid_x[10] = {-87.678503, -87.81683, -87.80959,-87.81004, -87.68706,-87.68616,-87.67892, -87.63235, -87.61381, -87.58352};
//                double mid_y[10] = {41.856803, 41.97466, 41.90729, 41.76984, 41.97556, 41.89960, 41.74859, 41.87157, 41.78340, 41.70744};
//                double base_edge_length = 0.01;
//                for(int i = 0; i < bench->ctb_count; i++){
//                    bench->load_big_sorted_run(i);
//                }
//                ofstream p;
//                p.open(to_string(config->MemTable_capacity/2)+"search_mbr.csv", ios::out|ios::binary|ios::trunc);        //config->SSTable_count/50
//                p << "search area" << ',' << "find_count" << ',' << "unique_find" << ',' << "intersect_sst_count" << ',' << "bit_find_count" << ',' << "time(ms)" << endl;
//                for(uint j = 0; j < 10; j++){
//                    for(int i = 0; i < 10 ; i++){
//                        //cout << fixed << setprecision(6) << mid_x - edge_length/2 <<","<<mid_y - edge_length/2 <<","<<mid_x + edge_length/2 <<","<<mid_y + edge_length/2 <<endl;
//                        double edge_length = base_edge_length * (i + 1);
//                        box search_area(mid_x[j] - edge_length/2, mid_y[j] - edge_length/2, mid_x[j] + edge_length/2, mid_y[j] + edge_length/2);
//                        search_area.print();
//                        struct timeval area_search_time = get_cur_time();
////                    uint temp = bench->config->SSTable_count;
////                    bench->config->SSTable_count = bench->merge_sstable_count;
//                        bench->mbr_search_in_disk(search_area, 5);
////                    bench->config->SSTable_count = temp;
//                        double time_consume = get_time_elapsed(area_search_time);
//                        //printf("area_search_time %.2f\n", time_consume);
//                        p << edge_length*edge_length << ',' << bench->mbr_find_count << ',' << bench->mbr_unique_find << ','
//                          << bench->intersect_sst_count <<',' << bench->bit_find_count << ',' << time_consume << endl;
//                        bench->mbr_find_count = 0;
//                        bench->mbr_unique_find = 0;
//                        bench->intersect_sst_count = 0;
//                        bench->bit_find_count = 0;
//                    }
//                    p << endl;
//                }
//                p.close();
//                //return;
//            }

			if(config->analyze_grid||config->profile){
				bench->analyze_grids();
			}
			if(config->analyze_reach){
				bench->analyze_reaches();
			}
			if(config->dynamic_schema&&!config->gpu){
				struct timeval ct = get_cur_time();
				bench->update_schema();
				bench->pro.index_update_time += get_time_elapsed(ct,true);
			}
            bench->pro.sum_round_time += get_time_elapsed(start,false);
            fprintf(stdout,"\tbench->pro.sum_round_time :\t%.2f\n",bench->pro.sum_round_time);
            //cout << "sum_round_time: "<< bench->pro.sum_round_time << endl;
			logt("round %d",start,st+t+1);
			bench->pro.rounds++;
			bench->pro.max_refine_size = max(bench->pro.max_refine_size, bench->grid_check_counter);
			bench->pro.max_filter_size = max(bench->pro.max_filter_size, bench->filter_list_index);
			bench->pro.max_bucket_num = max(bench->pro.max_bucket_num, bench->num_taken_buckets);
			bench->pro.num_pairs += bench->num_active_meetings;

            if(config->save_meetings_pers && t == 99){
                bench->dump_meetings(st);
            }

            if(st+t+1 == config->start_time+config->duration - 5){
                bench->dump_meta(config->CTB_meta_path);
            }
		}
	}
	bench->print_profile();
}










//void tracer::trace_process(){
//    struct timeval start = get_cur_time();
//    for(int st=config->start_time;st<config->start_time+config->duration;st+=100){
//        int cur_duration = min((config->start_time+config->duration-st),(uint)100);
//        loadData(config->trace_path.c_str(),st,cur_duration);
//        start = get_cur_time();
//        if(!bench){
//            bench = part->build_schema(trace, config->num_objects);
//            bench->mbr = mbr;
//
//#ifdef USE_GPU
//            if(config->gpu){
//				d_bench = cuda_create_device_bench(bench, gpu);
//			}
//#endif
//        }
//        for(int t=0;t<cur_duration;t++) {
//            log("");
//            bench->reset();
//            bench->points = trace + t * config->num_objects;
//            bench->cur_time = st + t;
//
//            bench->config->search_kv = true;                            //cuda search
//            if(bench->config->search_kv){
//                bench->search_count = 100;
//                for(int i=0;i<bench->search_count;i++){
//                    bench->search_list[i].pid = i;
//                    bench->search_list[i].target = 0;
//                    bench->search_list[i].start = 0;
//                    bench->search_list[i].end = 0;
//                }
//            }
//
//            // process the coordinate in this time point
//            if (!config->gpu) {
//                struct timeval ct = get_cur_time();
//                bench->filter();
//                bench->pro.filter_time += get_time_elapsed(ct, true);
//                bench->reachability();
//                bench->pro.refine_time += get_time_elapsed(ct, true);
////                bench->update_meetings();
////                bench->pro.meeting_identify_time += get_time_elapsed(ct, true);
//            } else {
//#ifdef USE_GPU
//                process_with_gpu(bench,d_bench,gpu);
//#endif
//            }
//
////            for(int i=0;i<bench->search_count;i++){
////                if(bench->search_list[i].target>0)
////                    cout<< bench->search_list[i].pid<<"-"<<bench->search_list[i].target<<"-"<<bench->search_list[i].start<<"-"<<bench->search_list[i].end<<endl;
////                if(bench->MemTable_count>0){
////                    if(BloomFilter_Check(bench, 0 ,bench->search_list[i].pid)){
////                        cout<< bench->search_list[i].pid <<"BloomFilter_Check :"<<endl;
////                    }
////                }
////            }
//
////            if(bench->MemTable_count==bench->config->MemTable_capacity){
////                cout<<"dump begin time: "<<bench->cur_time<<endl;
////                pthread_t bg_thread;
////                int ret;
////                if ((ret=pthread_create(&bg_thread,NULL,sst_dump,(void*)bench)) != 0){
////                    fprintf(stderr,"pthread_create:%s\n",strerror(ret));
////                    exit(1);
////                }
////            }
//
//
////            if(bench->MemTable_count==bench->config->MemTable_capacity){
////                //merge sort can be optimized, since they are always kv_restriction now.
////
////                ofstream SSTable_of;
////                SSTable_of.open("../store/SSTable_of" + to_string(t), ios::out|ios::binary|ios::trunc);           //config.DBPath
////                uint *key_index = new uint[bench->config->MemTable_capacity]{0};
////                int finish = 0;
////                clock_t time1,time2;
////                time1 = clock();
////                while(finish<bench->config->MemTable_capacity){
////                    finish = 0;
////                    __uint128_t temp_key = (__uint128_t)1<<126;
////                    box * temp_box;
////                    uint take_id =0;
////                    for(int i=0;i<bench->config->MemTable_capacity;i++){
////                        if( bench->h_keys[i][key_index[i]] == 0){              //empty kv
////                            finish++;
////                            continue;
////                        }
////                        if( temp_key > bench->h_keys[i][key_index[i]] ){
////                            temp_key = bench->h_keys[i][key_index[i]];
////                            temp_box = &bench->h_box_block[i][bench->h_values[i][key_index[i]]];               //bench->  i find the right 2G, then in box_block[ h_values ]
////                            take_id = i;
////                            bench->h_keys[i][key_index[i]] = 0;                 //init
////                        }
////                    }
////                    if(finish<bench->config->MemTable_capacity){
////                        key_index[take_id]++;
////                        print_128(temp_key);
////                        cout<< ": "<< temp_box->low[0] << endl;
////                        SSTable_of.write((char *)&temp_key, sizeof(__uint128_t));
////                        SSTable_of.write((char *) temp_box, sizeof(box));
////                    }
////                }
////
////                time2 = clock();
////                double this_time = (double)(time2-time1)/CLOCKS_PER_SEC;
////                cout<<"merge sort t: "<<bench->cur_time<<" time: "<< this_time <<std::endl;
////                for(int i=0;i<bench->config->MemTable_capacity;i++){
////                    cout<<"key_index"<<key_index[i]<<endl;
////                }
////                delete[] key_index;
////
////                SSTable_of.flush();
////                SSTable_of.close();
////
////                //init
////                bench->MemTable_count = 0;
////                for(uint i=0;i<bench->config->MemTable_capacity;i++){                   //useless
////                    for(uint j=bench->config->kv_restriction; j < bench->config->kv_capacity ; j++){
////                        bench->h_keys[i][j] = 0;
////                    }
////                }
////
//////                ifstream read_f;
//////                read_f.open("SSTable377");
//////                for(int i=0;i<100;i++){
//////                    __uint128_t first_widpid;
//////                    box first_box;
//////                    read_f.read((char *)&first_widpid, sizeof(__uint128_t));
//////                    read_f.read((char *)&first_box, sizeof(box));
//////                    print_128(first_widpid);
//////                    cout<< ": "<< first_box.low[0] << endl;
//////                }
////            }
//
//
//            if (config->analyze_grid || config->profile) {
//                bench->analyze_grids();
//            }
//            if (config->analyze_reach) {
//                bench->analyze_reaches();
//            }
//            if (config->dynamic_schema && !config->gpu) {
//                struct timeval ct = get_cur_time();
//                bench->update_schema();
//                bench->pro.index_update_time += get_time_elapsed(ct, true);
//            }
//            logt("round %d", start, st + t + 1);
//            bench->pro.rounds++;
//            bench->pro.max_refine_size = max(bench->pro.max_refine_size, bench->grid_check_counter);
//            bench->pro.max_filter_size = max(bench->pro.max_filter_size, bench->filter_list_index);
//            bench->pro.max_bucket_num = max(bench->pro.max_bucket_num, bench->num_taken_buckets);
//            bench->pro.num_pairs += bench->num_active_meetings;
//
//        }
//    }
//    //bench->print_profile();
//}


