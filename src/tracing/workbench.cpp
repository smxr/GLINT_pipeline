/*
 * workbench.cpp
 *
 *  Created on: Feb 18, 2021
 *      Author: teng
 */

#include "workbench.h"


workbench::workbench(workbench *bench):workbench(bench->config){
	grids_stack_index = bench->grids_stack_index;
	schema_stack_index = bench->schema_stack_index;
	cur_time = bench->cur_time;
	mbr = bench->mbr;

    if(config->bloom_filter) {
        dwMaxItems = bench->dwMaxItems;
        dProbFalse = bench->dProbFalse;
        dwFilterBits = bench->dwFilterBits;
        dwHashFuncs = bench->dwHashFuncs;
        dwSeed = bench->dwSeed;
        dwFilterSize = bench->dwFilterSize;
    }
    if(true){
        bit_count = bench->bit_count;
        bitmaps_size = bench->bitmaps_size;
    }
}

workbench::workbench(configuration *conf){

	config = conf;

	// setup the capacity of each container
	grid_capacity = config->grid_amplify*config->grid_capacity;
	// each grid contains averagely grid_capacity/2 objects, times 4 for enough space
	grids_stack_capacity = 4*max((uint)100000, config->num_objects/config->grid_capacity);

	// the number of all QTree Nodes
	schema_stack_capacity = 1.6*grids_stack_capacity;

	tmp_space_capacity = config->num_objects;

	filter_list_capacity = config->num_objects;

	grid_check_capacity = config->refine_size*config->num_objects;

	//meeting_capacity = config->num_objects/4;
	//meeting_capacity = 100;

	insert_lk = new pthread_mutex_t[MAX_LOCK_NUM];
	for(int i=0;i<MAX_LOCK_NUM;i++){
		pthread_mutex_init(&insert_lk[i],NULL);
	}
	for(int i=0;i<100;i++){
		data[i] = NULL;
		data_size[i] = 0;
	}
}

void workbench::clear(){
	for(int i=0;i<500;i++){
		if(data[i]!=NULL){
			free(data[i]);
			data[i] = NULL;
			data_size[i] = 0;
		}
	}
    data_index = 0;
	delete insert_lk;
    //ctbs.clear();
}

void *workbench::allocate(size_t size){
	lock();
	uint cur_idx = data_index++;
	unlock();
	data[cur_idx] = malloc(size);
	data_size[cur_idx] = size;
	return data[cur_idx];
}

void workbench::claim_space(){
	size_t size = 0;

	size = grid_capacity*grids_stack_capacity*sizeof(uint);
	grids = (uint *)allocate(size);
	log("\t%.2f MB\tgrids",size/1024.0/1024.0);

	size = grids_stack_capacity*sizeof(uint);
	grid_counter = (uint *)allocate(size);
	//log("\t%.2f MB\tgrid counter",size/1024.0/1024.0);

	size = grids_stack_capacity*sizeof(uint);
	grids_stack = (uint *)allocate(size);
	//log("\t%.2f MB\tgrids stack",size/1024.0/1024.0);
	for(int i=0;i<grids_stack_capacity;i++){
		grids_stack[i] = i;
	}

	size = schema_stack_capacity*sizeof(QTSchema);
	schema = (QTSchema*)allocate(size);
	log("\t%.2f MB\tschema",size/1024.0/1024.0);

	size = schema_stack_capacity*sizeof(uint);
	schema_stack = (uint *)allocate(size);
	//log("\t%.2f MB\tschema stack",size/1024.0/1024.0);
	for(int i=0;i<schema_stack_capacity;i++){
		schema_stack[i] = i;
		schema[i].type = INVALID;
	}

	size = grid_check_capacity*sizeof(checking_unit);
	grid_check = (checking_unit *)allocate(size);
	log("\t%.2f MB\tchecking units",size/1024.0/1024.0);

	size = config->num_meeting_buckets*sizeof(meeting_unit);
	meeting_buckets = (meeting_unit *)allocate(size);
	log("\t%.2f MB\tmeeting bucket space",size/1024.0/1024.0);

//	size = meeting_capacity*sizeof(meeting_unit);
//	meetings = (meeting_unit *)allocate(size);
//	log("\t%.2f MB\tmeeting space",size/1024.0/1024.0);
//
#pragma omp parallel for
	for(size_t i=0;i<config->num_meeting_buckets;i++){
		meeting_buckets[i].key = ULL_MAX;
	}

    size = config->MemTable_capacity * sizeof(__uint128_t *);                 //sort
    h_keys = (__uint128_t **)allocate(size);
    //log("\t%.2f MB\tmeeting bucket space",size/1024.0/1024.0);

    for(int i=0; i<config->MemTable_capacity; i++){
        size = config->kv_capacity*sizeof(__uint128_t);
        //h_keys[i] = (__uint128_t *)allocate(size);
        log("\t%.2f MB\ta element of h_keys",size/1024.0/1024.0);
    }

    size = config->search_single_capacity*sizeof(search_info_unit);                   //search
    search_single_list = (search_info_unit *)allocate(size);
    size = config->search_single_capacity*sizeof(uint);
    search_multi_pid = (uint *)allocate(size);
    size = config->search_multi_capacity*sizeof(search_info_unit);
    search_multi_list = (search_info_unit *)allocate(size);

    if(config->bloom_filter) {
        size = config->MemTable_capacity * sizeof(unsigned char *);                       //bloom
        pstFilter = (unsigned char **) allocate(size);

#pragma omp parallel for
        for (int i = 0; i < config->MemTable_capacity; i++) {
            size = dwFilterSize;
            pstFilter[i] = (unsigned char *) allocate(size);
            log("\t%.2f MB\t pstFilter", size / 1024.0 / 1024.0);
            memset(pstFilter[i], 0, dwFilterSize);
        }
    }

    size = config->num_objects* sizeof(float);
    h_longer_edges = (float *)allocate(size);

    size = config->big_sorted_run_capacity*sizeof(CTB);
    ctbs = (CTB *)allocate(size);

    if(config->save_meetings_pers || config->load_meetings_pers || !config->gpu){
        size = config->num_objects * 10 *sizeof(meeting_unit);              //100 * size of d_meetings_ps
        h_meetings_ps = (meeting_unit *) allocate(size);
        log("\t%.2f MB\t h_meetings_ps",1.0*size/1024/1024);

        size = 100 * sizeof(uint);
        active_meeting_count_ps = (uint *) allocate(size);
    }

    if(true){
        size = config->MemTable_capacity * sizeof(unsigned char *);
        h_bitmaps = (unsigned char **)allocate(size);
        size = config->MemTable_capacity * sizeof(box *);
        h_bitmap_mbrs = (box **)allocate(size);
        size = config->MemTable_capacity * sizeof(unsigned short *);
        h_sids = (unsigned short **)allocate(size);
        size = config->MemTable_capacity * sizeof(uint *);
        h_CTF_capacity = (uint **)allocate(size);
        size = config->MemTable_capacity * sizeof(oversize_buffer);
        h_oversize_buffers = (oversize_buffer *)allocate(size);
        size = config->MemTable_capacity * sizeof(CTF *);
        h_ctfs = (CTF **)allocate(size);

        for(int i=0;i<config->MemTable_capacity; i++){
            size = bitmaps_size;
            h_bitmaps[i] = (unsigned char *) allocate(size);
            log("\t%.2f MB\t h_bitmaps", size / 1024.0 / 1024.0);
            size = config->CTF_count * sizeof(box);
            h_bitmap_mbrs[i] = (box *)allocate(size);
            log("\t%.2f MB\t h_bitmap_mbrs", size / 1024.0 / 1024.0);
            size = config->num_objects * sizeof(unsigned short);
            h_sids[i] = (unsigned short *) allocate(size);
            log("\t%.2f MB\t h_sids", size / 1024.0 / 1024.0);
            size = config->CTF_count * sizeof(uint);
            h_CTF_capacity[i] = (uint *) allocate(size);
            log("\t%.2f MB\t h_CTF_capacity", size / 1024.0 / 1024.0);
            size = config->CTF_count*sizeof(CTF);
            h_ctfs[i] = (CTF *)allocate(size);
            log("\t%.2f MB\t h_ctfs",size/1024.0/1024.0);

            size = config->oversize_buffer_capacity*sizeof(__uint128_t);
            h_oversize_buffers[i].keys = (__uint128_t *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers keys",size/1024.0/1024.0);
            size = config->oversize_buffer_capacity*sizeof(f_box);
            h_oversize_buffers[i].boxes = (f_box *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers boxes",size/1024.0/1024.0);
            size = bit_count / 8;
            h_oversize_buffers[i].o_bitmaps = (unsigned char *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers o_bitmaps",size/1024.0/1024.0);
        }
    }
}

box workbench::make_bit_box(box b){
    box new_b;
//    new_b.low[0] = (b.low[0] - mbr.low[0])/(mbr.high[0] - mbr.low[0]) * (1ULL << (SID_BIT/2));
//    new_b.low[1] = (b.low[1] - mbr.low[1])/(mbr.high[1] - mbr.low[1]) * (1ULL << (SID_BIT/2));
//    new_b.high[0] = (b.high[0] - mbr.low[0])/(mbr.high[0] - mbr.low[0]) * (1ULL << (SID_BIT/2));
//    new_b.high[1] = (b.high[1] - mbr.low[1])/(mbr.high[1] - mbr.low[1]) * (1ULL << (SID_BIT/2));
    return new_b;
}

struct load_args {
    string path;
    __uint128_t * keys;
    uint SIZE;
};

void *parallel_load(void *arg){
    load_args *pargs = (load_args *)arg;
    ifstream read_ctf;
    read_ctf.open(pargs->path.c_str() , ios::in|ios::binary);           //never trunc when multi thread, use app
    //cout << pargs->path << endl;
    assert(read_ctf.is_open());
    read_ctf.read((char *)pargs->keys, pargs->SIZE);
    read_ctf.close();
    return NULL;
}

void workbench::load_big_sorted_run(uint b){
    if(!ctbs[b].ctfs){
        ctbs[b].ctfs = new CTF[config->CTF_count];
    }
    for(int i = 0; i < config->CTF_count; i++) {
        //if(!ctbs[b].ctfs[i].keys){
        ctbs[b].ctfs[i].keys = new __uint128_t[ctbs[b].CTF_capacity[i]];
    }

    struct timeval start_time = get_cur_time();
    load_args *pargs = new load_args[config->CTF_count];
    pthread_t threads[config->CTF_count];
    for(int i = 0; i < config->CTF_count; i++){
        threads[i] = 0;
    }
    for(int i = 0; i < config->CTF_count; i++){
        //if(!ctbs[b].ctfs[i].keys){
            ctbs[b].ctfs[i].keys = new __uint128_t[ctbs[b].CTF_capacity[i]];
            pargs[i].path = config->raid_path + to_string(i % 2) + "/SSTable_" + to_string(b) + "-" + to_string(i);
            pargs[i].SIZE = sizeof(__uint128_t) * ctbs[b].CTF_capacity[i];
            pargs[i].keys = ctbs[b].ctfs[i].keys;
            int ret = pthread_create(&threads[i], NULL, parallel_load, (void *)&pargs[i]);
            if(ret != 0) {
                std::cerr << "Failed to create thread " << i << " with error code: " << ret << std::endl;
                threads[i] = 0;
                continue;
            }
        //}
    }
    for(int i = 0; i < config->CTF_count; i++ ){
        if (threads[i] != 0) {
            void *status;
            int ret = pthread_join(threads[i], &status);
            if (ret != 0) {
                std::cerr << "Error joining thread " << i << " with error code: " << ret << std::endl;
            }
        }
    }
    pro.load_keys_time += get_time_elapsed(start_time,false);
    logt("load CTB keys %d", start_time, b);
    delete[] pargs;
}

void workbench::clear_all_keys(){
    for(int i = 0; i < ctb_count; i++){
        for(int j = 0; j < config->CTF_count; j++){
            if(ctbs[i].ctfs){
                if(ctbs[i].ctfs[j].keys){
                    delete []ctbs[i].ctfs[j].keys;
                    ctbs[i].ctfs[j].keys = nullptr;
                }
            }
        }
    }
}

void workbench::load_CTF_keys(uint CTB_id, uint CTF_id){
    ifstream read_sst;
    string filename = config->raid_path + to_string(CTF_id%2) + "/N_SSTable_"+to_string(CTB_id)+"-"+to_string(CTF_id);
    read_sst.open(filename, ios::in|ios::binary);
    if(!read_sst.is_open()){
        cout << filename << endl;
    }
    //assert(read_sst.is_open());
    CTF * ctf = &ctbs[CTB_id].ctfs[CTF_id];
    uint8_t * data = new uint8_t[ctf->CTF_kv_capacity * ctf->key_bit];
    read_sst.read((char *)data, ctf->CTF_kv_capacity * ctf->key_bit);
    read_sst.close();
    ctf->keys = reinterpret_cast<__uint128_t *>(data);
}

vector<Interval> query_intervals(const vector<Interval>& start_sorted, const vector<Interval>& end_sorted, int query_start, int query_end) {
    vector<Interval> result;
    unordered_set<const Interval*> excluded_intervals;

    // Exclude intervals with start > query_end
    auto it_start = upper_bound(start_sorted.begin(), start_sorted.end(), query_end,
                                [](int value, const Interval& interval) {
                                    return value < interval.start;
                                });

    for (auto it = it_start; it != start_sorted.end(); ++it) {
        excluded_intervals.insert(&(*it));
    }

    // Add intervals with start <= query_end and not excluded
    for (const auto& interval : start_sorted) {
        if (interval.start > query_end) break;
        if (interval.end >= query_start && excluded_intervals.find(&interval) == excluded_intervals.end()) {
            result.push_back(interval);
        }
    }

    return result;
}

uint workbench::search_time_in_disk(time_query * tq){          //not finish
    vector<Interval> result = query_intervals(start_sorted, end_sorted, tq->t_start, tq->t_end);
    cout << "result.size()" << result.size() << endl;
#pragma omp parallel for num_threads(config->num_threads)
    for(uint j = 0; j < result.size(); j++){
        //cout << "start " << result[j].start << " end " << result[j].end << " value " << result[j].value << endl;
        uint count = 0;
        if(result[j].value >= 0){
            uint ctb_id = result[j].value / config->CTF_count;
            if(ctb_id > ctb_count) continue;
            uint ctf_id = result[j].value % config->CTF_count;
            CTF * ctf = &ctbs[ctb_id].ctfs[ctf_id];
            if (!ctf->keys) {
                load_CTF_keys(ctb_id, ctf_id);
            }
            count += ctf->time_search(tq);
            delete []ctbs[ctb_id].ctfs[ctf_id].keys;
            ctbs[ctb_id].ctfs[ctf_id].keys = nullptr;
        }
        else{   //hit buffer
            int ctb_id = -1 - result[j].value;
            count += ctbs[ctb_id].o_buffer.o_time_search(tq);
        }
        search_count.fetch_add(count, std::memory_order_relaxed);
    }
    uint ret = search_count;
    search_count = 0;
    return ret;
}

bool workbench::id_search_in_CTB(uint pid, uint CTB_id, time_query * tq){
    uint i = CTB_id;
    time_query tq_temp;
    tq_temp.t_start = tq->t_start - ctbs[i].start_time_min;
    tq_temp.t_end = tq->t_end - ctbs[i].start_time_min;
    tq_temp.abandon = tq->abandon;
    if(ctbs[i].sids[pid] == 0){
        //wid_filter_count++;
        return false;
    }
    else if(ctbs[i].sids[pid] == 1){
        //cout << "oid search buffer" << endl;
        uint buffer_find = ctbs[i].o_buffer.search_buffer(pid, &tq_temp, search_multi, search_count, search_multi_pid);
        if(!search_multi)
            search_count.fetch_add(buffer_find, std::memory_order_relaxed);
        hit_buffer.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    uint ctf_id = ctbs[i].sids[pid] - 2;
    if(!ctbs[i].ctfs[ctf_id].keys) {
        load_CTF_keys(i, ctf_id);
    }
    uint target_count = ctbs[i].ctfs[ctf_id].search_SSTable(pid, tq, search_multi, search_count, search_multi_pid);
    if(!search_multi)
        search_count.fetch_add(target_count, std::memory_order_relaxed);
    hit_ctf.fetch_add(1, std::memory_order_relaxed);
//    delete[] ctbs[i].ctfs[ctf_id].keys;
//    ctbs[i].ctfs[ctf_id].keys = nullptr;
    if(target_count){
        return true;
    }
    else{
        //id_not_find_count++;
        return false;
    }
}

bool workbench::id_search_in_disk(uint pid, time_query * tq){
    //cout<<"oid disk search "<<pid<<endl;
    bool ret = false;
    for(int i=0; i < ctb_count; i++) {
        if (tq->abandon || (ctbs[i].start_time_min < tq->t_end) && (tq->t_start < ctbs[i].end_time_max)) {
            ret |= id_search_in_CTB(pid, i, tq);
        }
    }
    return ret;
}

void workbench::dump_meetings(uint st) {
    struct timeval start_time = get_cur_time();
    string filename = config->trace_path + "meeting" + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ofstream wf(filename, ios::out|ios::binary|ios::trunc);
    wf.write((char *)active_meeting_count_ps, sizeof(uint) * 100);
    wf.write((char *)h_meetings_ps, sizeof(meeting_unit) * total_meetings_this100s);
    wf.close();
    total_meetings_this100s = 0;
    logt("dumped to %s",start_time, filename.c_str());
}

void workbench::load_meetings(uint st) {
    log("loading meetings from %d to %d",st, st + 100);
    struct timeval start_time = get_cur_time();
    string filename = config->trace_path + "meeting" + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ifstream in(filename, ios::in | ios::binary);
    in.read((char *)active_meeting_count_ps, sizeof(uint) * 100);
    total_meetings_this100s = 0;
    for(uint i = 0; i < 100; i++){
        total_meetings_this100s += active_meeting_count_ps[i];
    }
    in.read((char *)h_meetings_ps, sizeof(meeting_unit) * total_meetings_this100s);
    in.close();
    logt("loaded %d objects last for 100 seconds start from %d time from %s",start_time, config->num_objects, st, filename.c_str());
}

bool PolygonSearchCallback(int * i, box poly_mbr,void* arg){
    vector<pair<int, box>> * ret = (vector<pair<int, box>> *)arg;
    ret->push_back(make_pair(*i, poly_mbr));
    return true;
}

bool workbench::mbr_search_in_obuffer(box b, uint CTB_id, time_query * tq){
    uint i = CTB_id;
    bool ret = false;
    if(!ctbs[i].ctfs){
        ctbs[i].ctfs = new CTF[config->CTF_count];
    }
    for(uint bid=0; bid < bit_count; bid++){
        if(ctbs[CTB_id].o_buffer.o_bitmaps[bid / 8] & (1 << (bid % 8))){
            Point bit_p;
            uint x=0,y=0;
            x = bid % DEFAULT_bitmap_edge;
            y = bid / DEFAULT_bitmap_edge;
            bit_p.x = (double)x/DEFAULT_bitmap_edge*(mbr.high[0] - mbr.low[0]) + mbr.low[0];
            bit_p.y = (double)y/DEFAULT_bitmap_edge*(mbr.high[1] - mbr.low[1]) + mbr.low[1];
            if(b.contain(bit_p)){
                ret = true;
                break;
            }
        }
    }
    if(ret){
        uint buffer_find = 0;
        for(uint q = 0; q < ctbs[i].o_buffer.oversize_kv_count; q++){
            if(ctbs[i].o_buffer.boxes[q].intersect(b)){
                buffer_find++;
            }
        }
        search_count.fetch_add(buffer_find, std::memory_order_relaxed);      //!!!!!!!!!
    }
    return ret;
}

bool workbench::mbr_search_in_disk(box b, time_query * tq){

    //assert(mbr.contain(b));
    //cout << "mbr disk search" << endl;

    bool ret = false;
    vector<pair<int, box>> intersect_mbrs;
    total_rtree->Search(b.low, b.high, PolygonSearchCallback, (void *)&intersect_mbrs);
    intersect_sst_count += intersect_mbrs.size();
    for (uint j = 0; j < intersect_mbrs.size(); j++) {
        uint CTB_id = intersect_mbrs[j].first / config->CTF_count;
        uint CTF_id = intersect_mbrs[j].first % config->CTF_count;
        CTF *ctf = &ctbs[CTB_id].ctfs[CTF_id];
        bool find = false;
        for(uint bid=0; bid < ctf->ctf_bitmap_size * 8; bid++){
            if(ctf->bitmap[bid / 8] & (1 << (bid % 8))){
                Point bit_p;
                uint x=0,y=0;
                x = bid % ctf->x_grid;
                y = bid / ctf->x_grid;
                bit_p.x = (double) x / ctf->x_grid * (ctf->ctf_mbr.high[0] - ctf->ctf_mbr.low[0]) +
                          ctf->ctf_mbr.low[0];
                bit_p.y = (double) y / ctf->y_grid * (ctf->ctf_mbr.high[1] - ctf->ctf_mbr.low[1]) +
                          ctf->ctf_mbr.low[1];
                if(b.contain(bit_p)){
                    find = true;
                    ret = true;
                    break;
                }

            }
        }

        if(find){
            bit_find_count++;
            uint this_find = 0;
            time_query tq_temp;
            tq_temp.t_start = tq->t_start - ctbs[CTB_id].start_time_min;
            tq_temp.t_end = tq->t_end - ctbs[CTB_id].start_time_min;
            tq_temp.abandon = tq->abandon;
            box_search_info bs_uint;
            bs_uint.ctb_id = CTB_id;
            bs_uint.ctf_id = CTF_id;
            box * temp_b = new box;
            temp_b->low[0] = ctf->ctf_mbr.low[0];
            temp_b->low[1] = ctf->ctf_mbr.low[1];
            temp_b->high[0] = ctf->ctf_mbr.high[0];
            temp_b->high[1] = ctf->ctf_mbr.high[1];
            bs_uint.bmap_mbr = temp_b;
            bs_uint.tq = tq_temp;
            box_search_queue.push_back(bs_uint);
            //cerr<<this_find<<"finds in sst "<<CTF_id<<endl;
        }
    }
    return ret;
}

void workbench::dump_meta(const char *path) {
    struct timeval start_time = get_cur_time();
    string bench_path = string(path) + "N_workbench";
    ofstream wf(bench_path, ios::out|ios::binary|ios::trunc);
    wf.write((char *)config, sizeof(generator_configuration));        //the config of pipeline is generator_configuration
    wf.write((char *)this, sizeof(workbench));
    wf.close();
#pragma omp parallel for num_threads(config->num_threads)
    for(int i = 0; i < ctb_count; i++){
        if(ctbs[i].sids){
            cout << "N_CTB" << i << endl;
            string CTB_path = string(path) + "N_CTB" + to_string(i);
            dump_CTB_meta(CTB_path.c_str(), i);
            for(uint j = 0; j < config->CTF_count; j++){
                string ctf_path = string(config->CTB_meta_path) + "N_STcL" + to_string(i)+"-"+to_string(j);
                ctbs[i].ctfs[j].dump_meta(ctf_path);
            }
        }
    }

    logt("bench meta dumped to %s",start_time,path);
}

void workbench::dump_CTB_meta(const char *path, int i) {
    ofstream wf(path, ios::out|ios::binary|ios::trunc);
    wf.write((char *)&ctbs[i], sizeof(CTB));
    //wf.write((char *)ctbs[i].first_widpid, config->CTF_count * sizeof(uint64_t));
    wf.write((char *)ctbs[i].sids, config->num_objects * sizeof(unsigned short));
    //wf.write((char *)ctbs[i].bitmaps, bitmaps_size * sizeof(unsigned char));
    //wf.write((char *)ctbs[i].bitmap_mbrs, config->CTF_count * sizeof(box));
    //wf.write((char *)ctbs[i].CTF_capacity, config->CTF_count * sizeof(uint));
    wf.write((char *)ctbs[i].o_buffer.keys, ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t));
    wf.write((char *)ctbs[i].o_buffer.boxes, ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box));
    wf.write((char *)ctbs[i].o_buffer.o_bitmaps, bit_count / 8 * sizeof(unsigned char));
    //ctbs[i].o_buffer.print_buffer();
    wf.close();
    //logt("CTB meta %d dump to %s",start_time, i, path);

    delete[] ctbs[i].sids;
    ctbs[i].sids = NULL;
}

void workbench::dump_CTB_meta(const char *path, CTB * ctb) {
    ofstream wf(path, ios::out|ios::binary|ios::trunc);
    wf.write((char *)ctb, sizeof(CTB));
    //wf.write((char *)ctb->first_widpid, config->CTF_count * sizeof(uint64_t));
    wf.write((char *)ctb->sids, config->num_objects * sizeof(unsigned short));
    //wf.write((char *)ctb->bitmaps, bitmaps_size * sizeof(unsigned char));
    //wf.write((char *)ctb->bitmap_mbrs, config->CTF_count * sizeof(box));
    //wf.write((char *)ctb->CTF_capacity, config->CTF_count * sizeof(uint));
    wf.write((char *)ctb->o_buffer.keys, ctb->o_buffer.oversize_kv_count * sizeof(__uint128_t));
    wf.write((char *)ctb->o_buffer.boxes, ctb->o_buffer.oversize_kv_count * sizeof(f_box));
    wf.write((char *)ctb->o_buffer.o_bitmaps, bit_count / 8 * sizeof(unsigned char));
    //ctb->o_buffer.print_buffer();
    wf.close();
    //logt("CTB meta %d dump to %s",start_time, i, path);

    delete[] ctb->sids;
    ctb->sids = NULL;
}

double bytes_to_MB(size_t bytes) {
    return bytes / (1024.0 * 1024.0);
}

void workbench::load_CTB_meta(const char *path, int i) {
    string CTB_path = string(path) + "N_CTB" + to_string(i);
    struct timeval start_time = get_cur_time();
    ifstream in(CTB_path.c_str(), ios::in | ios::binary);
    if (!in.is_open()) {
        std::cerr << "Error opening file: " << CTB_path << std::endl;
    }
    in.read((char *)&ctbs[i], sizeof(CTB));
    ctbs[i].sids = new unsigned short[config->num_objects];
    ctbs[i].o_buffer.keys = new __uint128_t[ctbs[i].o_buffer.oversize_kv_count];
    ctbs[i].o_buffer.boxes = new f_box[ctbs[i].o_buffer.oversize_kv_count];
    ctbs[i].o_buffer.o_bitmaps = new unsigned char[bit_count / 8 * sizeof(unsigned char)];
    in.read((char *)ctbs[i].sids, config->num_objects * sizeof(unsigned short));
    in.read((char *)ctbs[i].o_buffer.keys, ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t));
    in.read((char *)ctbs[i].o_buffer.boxes, ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box));
    in.read((char *)ctbs[i].o_buffer.o_bitmaps, bit_count / 8);
    in.close();

    std::cerr << "Size of ctbs[i].sids: "
              << bytes_to_MB(config->num_objects * sizeof(unsigned short)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].o_buffer.keys: "
              << bytes_to_MB(ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].o_buffer.boxes: "
              << bytes_to_MB(ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].o_buffer.o_bitmaps: "
              << bytes_to_MB(bit_count / 8) << " MB" << std::endl;

    uint byte_of_CTB_meta = config->num_objects * sizeof(unsigned short) +  ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t)
                            + ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box) + bit_count / 8;
    cerr << "byte_of_CTB_meta: " << byte_of_CTB_meta << endl;
    logt("CTB meta %d load from %s",start_time, i, path);

    //ctbs[i].ctfs is a pointer but no malloc
    ctbs[i].ctfs = new CTF[config->CTF_count];
    for(uint j = 0; j < 100; j++){
        string CTF_path = string(path) + "STcL" + to_string(i)+"-"+to_string(j);
        load_CTF_meta(CTF_path.c_str(), i, j);
    }
    logt("CTF meta %d load from %s",start_time, i, path);
}

void workbench::load_CTF_meta(const char *path, int i, int j) {
    //struct timeval start_time = get_cur_time();
    ifstream in(path, ios::in | ios::binary);
    if (!in.is_open()) {
        std::cerr << "Error opening file: " << path << std::endl;
    }
    CTF * ctf = &ctbs[i].ctfs[j];
    in.read((char *)ctf, sizeof(CTF));
    ctf->bitmap = new unsigned char[ctf->ctf_bitmap_size];
    in.read((char *)ctf->bitmap, ctf->ctf_bitmap_size);
    //logt("CTF meta load from %s",start_time, path);
}

void old_workbench::old_load_CTB_meta(const char *path, int i) {
    struct timeval start_time = get_cur_time();
    ifstream in(path, ios::in | ios::binary);

    if (!in.is_open()) {
        std::cerr << "Error opening file: " << path << std::endl;
    }
    in.read((char *)&ctbs[i], sizeof(CTB) - sizeof(oversize_buffer) - 8);
    in.read((char *)&ctbs[i].o_buffer, sizeof(old_oversize_buffer));
    in.read((char *)&ctbs[i].box_rtree, 8);
    ctbs[i].first_widpid = new uint64_t[config->CTF_count];
    ctbs[i].sids = new unsigned short[config->num_objects];
    ctbs[i].bitmaps = new unsigned char[bitmaps_size];
    ctbs[i].bitmap_mbrs = new box[config->CTF_count];
    ctbs[i].CTF_capacity = new uint[config->CTF_count];
    ctbs[i].o_buffer.keys = new __uint128_t[ctbs[i].o_buffer.oversize_kv_count];
    ctbs[i].o_buffer.boxes = new f_box[ctbs[i].o_buffer.oversize_kv_count];
    in.read((char *)ctbs[i].first_widpid, config->CTF_count * sizeof(uint64_t));
    in.read((char *)ctbs[i].sids, config->num_objects * sizeof(unsigned short));
    in.read((char *)ctbs[i].bitmaps, bitmaps_size * sizeof(unsigned char));
    in.read((char *)ctbs[i].bitmap_mbrs, config->CTF_count * sizeof(box));
    in.read((char *)ctbs[i].CTF_capacity, config->CTF_count * sizeof(uint));
    in.read((char *)ctbs[i].o_buffer.keys, ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t));
    in.read((char *)ctbs[i].o_buffer.boxes, ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box));
    in.close();
    //RTree
    ctbs[i].box_rtree = new RTree<short *, double, 2, double>();        //size nearly equals to bitmap_mbrs
    std::cerr << "Size of ctbs[i].first_widpid: "
              << bytes_to_MB(config->CTF_count * sizeof(uint64_t)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].sids: "
              << bytes_to_MB(config->num_objects * sizeof(unsigned short)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].bitmaps: "
              << bytes_to_MB(bitmaps_size * sizeof(unsigned char)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].bitmap_mbrs: "
              << bytes_to_MB(config->CTF_count * sizeof(box)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].CTF_capacity: "
              << bytes_to_MB(config->CTF_count * sizeof(uint)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].o_buffer.keys: "
              << bytes_to_MB(ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t)) << " MB" << std::endl;

    std::cerr << "Size of ctbs[i].o_buffer.boxes: "
              << bytes_to_MB(ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box)) << " MB" << std::endl;

    uint byte_of_CTB_meta = config->CTF_count * sizeof(uint64_t) + config->num_objects * sizeof(unsigned short) + bitmaps_size * sizeof(unsigned char)
                            + config->CTF_count * sizeof(box) * 2 + config->CTF_count * sizeof(uint) + ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t)
                            + ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box);
    cerr << "byte_of_CTB_meta: " << byte_of_CTB_meta << endl;
    for(uint j = 0; j < config->CTF_count; ++j){
        ctbs[i].box_rtree->Insert(ctbs[i].bitmap_mbrs[j].low, ctbs[i].bitmap_mbrs[j].high, new short(j));
    }
    logt("CTB meta %d load from %s",start_time, i, path);
}


void workbench::build_trees(uint max_ctb){
    total_rtree = new RTree<int *, double, 2, double>();
    for(uint i = 0; i < max_ctb; i++){
        for(uint j = 0; j < config->CTF_count; j++){
            f_box & m = ctbs[i].ctfs[j].ctf_mbr;
            box b;
            b.low[0] = m.low[0];
            b.low[1] = m.low[1];
            b.high[0] = m.high[0];
            b.high[1] = m.high[1];
            total_rtree->Insert(b.low, b.high, new int(i * config->CTF_count +j));
        }
    }
    //buffer mbr is map mbr, which is definitely searched

    vector<Interval> temp;
    temp.reserve(max_ctb * (config->CTF_count + 1));
    for(int i = 0; i < max_ctb; i++){
        for(int j = 0; j < config->CTF_count; j++) {
            Interval temp_in(ctbs[i].ctfs[j].start_time_min, ctbs[i].ctfs[j].end_time_max, (int)(i * config->CTF_count +j));
            temp.push_back(temp_in);
        }
        Interval temp_in(ctbs[i].o_buffer.start_time_min, ctbs[i].o_buffer.end_time_max, (int)(-1-i));
        temp.push_back(temp_in);
    }
    start_sorted.swap(temp);
    sort(start_sorted.begin(), start_sorted.end(), [](const Interval& a, const Interval& b) {
        return a.start < b.start;
    });
    end_sorted = start_sorted;
    sort(end_sorted.begin(), end_sorted.end(), [](const Interval& a, const Interval& b) {
        return a.end < b.end;
    });

//    total_btree = new BPlusTree<int>(4);
//    for(uint i = 0; i < max_ctb; i++){
//        for(uint j = 0; j < config->CTF_count; j++) {
//            total_btree->insert(Interval(ctbs[i].ctfs[j].start_time_min, ctbs[i].ctfs[j].end_time_max), i * config->CTF_count +j);
//        }
//        total_btree->insert(Interval(ctbs[i].o_buffer.start_time_min, ctbs[i].o_buffer.end_time_max), -i);
//    }
}

void workbench::make_new_ctf_with_old_ctb(uint max_ctb){
    for(uint i = 0; i < max_ctb; i++){
        load_big_sorted_run(i);
#pragma omp parallel for num_threads(config->CTF_count)
        for(uint j = 0; j < config->CTF_count; j++){
            ctbs[i].ctfs[j].ctf_mbr.get_fb(&ctbs[i].bitmap_mbrs[j]);
            //tbs[i].ctfs[j].ctf_mbr.print();
            //load keys
            //ctbs[i].ctfs[j].bitmap = &ctbs[i].bitmaps[j * (bit_count / 8)];
            ctbs[i].ctfs[j].CTF_kv_capacity = ctbs[i].CTF_capacity[j];
            ctbs[i].ctfs[j].end_time_min = ctbs[i].end_time_min;
            ctbs[i].ctfs[j].end_time_max = ctbs[i].end_time_max;
            ctbs[i].ctfs[j].get_ctf_bits(mbr, config);
            ctbs[i].ctfs[j].transfer_all_in_one();
            string sst_path = config->raid_path + to_string(j%2) + "/N_SSTable_"+to_string(i)+"-"+to_string(j);
            ofstream SSTable_of;
            SSTable_of.open(sst_path , ios::out|ios::binary|ios::trunc);
            assert(SSTable_of.is_open());
            SSTable_of.write((char *)ctbs[i].ctfs[j].keys, ctbs[i].ctfs[j].key_bit / 8 * ctbs[i].ctfs[j].CTF_kv_capacity);
            SSTable_of.close();
            delete[] ctbs[i].ctfs[j].keys;
        }
        ctbs[i].o_buffer.end_time_min = ctbs[i].end_time_min;
        ctbs[i].o_buffer.end_time_max = ctbs[i].end_time_max;
        ctbs[i].o_buffer.write_o_buffer(mbr, bit_count);
    }
}