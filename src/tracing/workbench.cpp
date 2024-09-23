/*
 * workbench.cpp
 *
 *  Created on: Feb 18, 2021
 *      Author: teng
 */

#include "workbench.h"
#include <unordered_set>


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
		data_index = 0;
	}
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
//#pragma omp parallel for
//	for(size_t i=0;i<config->num_meeting_buckets;i++){
//		meeting_buckets[i].key = ULL_MAX;
//	}

    size = config->MemTable_capacity * sizeof(__uint128_t *);                 //sort
    h_keys = (__uint128_t **)allocate(size);
    //log("\t%.2f MB\tmeeting bucket space",size/1024.0/1024.0);

    for(int i=0; i<config->MemTable_capacity; i++){
        size = config->kv_capacity*sizeof(__uint128_t);
        h_keys[i] = (__uint128_t *)allocate(size);
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

    if(config->save_meetings_pers || config->load_meetings_pers){
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

        size = config->MemTable_capacity * sizeof(oversize_buffer *);
        h_oversize_buffers = (oversize_buffer *)allocate(size);


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

            size = config->oversize_buffer_capacity*sizeof(__uint128_t);
            h_oversize_buffers[i].keys = (__uint128_t *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers keys",size/1024.0/1024.0);
            size = config->oversize_buffer_capacity*sizeof(f_box);
            h_oversize_buffers[i].boxes = (f_box *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers boxes",size/1024.0/1024.0);
        }
    }
}

box workbench::make_bit_box(box b){
    box new_b;
    new_b.low[0] = (b.low[0] - mbr.low[0])/(mbr.high[0] - mbr.low[0]) * ((1ULL << (SID_BIT / 2)) - 1);
    new_b.low[1] = (b.low[1] - mbr.low[1])/(mbr.high[1] - mbr.low[1]) * ((1ULL << (SID_BIT / 2)) - 1);
    new_b.high[0] = (b.high[0] - mbr.low[0])/(mbr.high[0] - mbr.low[0]) * ((1ULL << (SID_BIT / 2)) - 1);
    new_b.high[1] = (b.high[1] - mbr.low[1])/(mbr.high[1] - mbr.low[1]) * ((1ULL << (SID_BIT / 2)) - 1);
    return new_b;
}

void workbench::load_big_sorted_run(uint b){
    if(!ctbs[b].ctfs){
        ctbs[b].ctfs = new CTF[config->CTF_count];
    }
    ifstream read_sst;
    for(int i = 0; i < config->CTF_count; i++){
        if(!ctbs[b].ctfs[i].keys){
            string filename = config->raid_path + to_string(b)+"-"+to_string(i);
            //cout<<filename<<endl;
            read_sst.open(filename);
            assert(read_sst.is_open());
            ctbs[b].ctfs[i].keys = new __uint128_t[ctbs[b].CTF_capacity[i]];
            read_sst.read((char *)ctbs[b].ctfs[i].keys, sizeof(__uint128_t) * ctbs[b].CTF_capacity[i]);
            read_sst.close();
        }
    }
}

bool workbench::search_memtable(uint64_t pid, vector<__uint128_t> & v_keys, vector<uint> & v_indices){          //wid_pid       //for dump
    cout<<"memtable search "<<pid<<endl;
    uint offset = 0;
    if(ctb_count % 2 == 1){
        offset = config->MemTable_capacity/2;
    }
    bool ret = false;
    for(int i=0;i<MemTable_count;i++) {                                         //i<MemTable_count
        uint64_t wp = ((uint64_t)h_sids[offset+i][pid] << OID_BIT) + pid;
        cout << "wp " << wp << endl;
        int find = -1;
        int low = 0;
        int high = config->kv_restriction - 1;
        int mid;
        uint64_t temp_wp;
        //box temp_box;
        while (low <= high) {
            mid = (low + high) / 2;
            temp_wp = (h_keys[offset+i][mid] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT)) ;
            cout << "temp_wp" << temp_wp <<endl;
            if (temp_wp == wp) {
                find = mid;
                ret = true;
                break;
            } else if (temp_wp > wp) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        if (find == -1) {
            cout << "cannot find" << endl;
            break;
        }
        cout << "exactly find" << endl;
        uint cursor = find;
        while (temp_wp == wp && cursor >= 1) {
            ret = true;
            cursor--;
            temp_wp = (h_keys[offset+i][cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT)) ;
        }
        if (temp_wp == wp && cursor == 0) {
            v_keys.push_back(h_keys[offset+i][cursor]);
            v_indices.push_back(cursor);

        }
        while (cursor + 1 < config->kv_restriction) {
            cursor++;
            temp_wp = (h_keys[offset+i][cursor] >> 23) & ((1ULL << 25) - 1);
            if (temp_wp == wp) {
                v_keys.push_back(h_keys[offset+i][cursor]);
                v_indices.push_back(cursor);
            }
        }
    }
    return ret;
}

//bool workbench::search_timestamp(uint timestamp){
//    bool ret = false;
//    int count;
//    for(int i=0;i<big_sorted_run_count;i++) {
//        if ( (bg_run[i].start_time_min < timestamp)&&(timestamp < bg_run[i].end_time_max) ) {
//            int end_time
//        }
//    }
//}

bool workbench::search_in_disk(uint pid, uint timestamp){
    //cout<<"oid disk search "<<pid<<endl;
    bool ret = false;
    for(int i=0; i < ctb_count; i++) {
        if ((ctbs[i].start_time_min < timestamp) && (timestamp < ctbs[i].end_time_max) ) {
            //cout<<"big_sorted_run_num:"<<i<<endl;
            uint64_t wp = pid;
            if(ctbs[i].sids[pid] == 0){
                wid_filter_count++;
                continue;
            }
            else if(ctbs[i].sids[pid] == 1){
                //cout << "oid search buffer" << endl;
                id_find_count += ctbs[i].o_buffer.search_buffer(pid);
            }
            else{
                wp += ((uint64_t)ctbs[i].sids[pid] << OID_BIT);
                //cout<<"wp: "<<wp<<endl;
            }
            if(!ctbs[i].ctfs){
                //cout<<"new SSTables"<<endl;
                ctbs[i].ctfs = new CTF[config->CTF_count];                   //maybe useful later, should not delete after this func , if(!NULL)
            }
            ifstream read_sst;

            //high level binary search
            int find = -1;
            int low = 0;
            int high = config->CTF_count - 1;
            int mid;
            while (low <= high) {
                mid = (low + high) / 2;
                //cout << bg_run[i].first_widpid[mid] << endl;
                if (ctbs[i].first_widpid[mid] == wp){
                    find = mid;
                    break;
                }
                else if (ctbs[i].first_widpid[mid] > wp){
                    high = mid - 1;
                }
                else {
                    low = mid + 1;
                }
            }
            if(find==-1){
//                cout<<"not find in first_widpid"<<endl;
//                cout << low << "-" << bg_run[i].first_widpid[low] << endl;
//                cout << mid << "-" << bg_run[i].first_widpid[mid] << endl;
//                cout << high << "-" << bg_run[i].first_widpid[high] << endl;
                if(high<0){
                    high = 0;
                }
                if(!ctbs[i].ctfs[high].keys){
                    //cout<<"new SSTables keys"<<high<<endl;
                    string filename = config->raid_path + to_string(i)+"-"+to_string(high);
                    //cout<<filename<<endl;
                    read_sst.open(filename);                   //final place is not high+1, but high
                    assert(read_sst.is_open());
                    ctbs[i].ctfs[high].keys = new __uint128_t [ctbs[i].CTF_capacity[high]];
                    read_sst.read((char *)ctbs[i].ctfs[high].keys, sizeof(__uint128_t) * ctbs[i].CTF_capacity[high]);
                    read_sst.close();
                }
                uint target_count = ctbs[i].ctfs[high].search_SSTable(wp, search_multi, ctbs[i].CTF_capacity[high], search_multi_length, search_multi_pid);
                id_find_count += target_count;
                if(target_count){
                    ret = true;
                }
                else{
                    id_not_find_count++;
                }
                continue;
            }
/*
            //cout<<"high level binary search finish and find"<<endl;
            //for the case, there are many SSTables that first_widpid==wp
            //find start and end
            uint pid_start = find;
            while(pid_start>=1){
                pid_start--;
                if(ctbs[i].first_widpid[pid_start] != wp){
                    break;
                }
            }
            if(!ctbs[i].ctfs[pid_start].keys){
                read_sst.open("../store/SSTable_"+to_string(i)+"-"+to_string(pid_start));
                assert(read_sst.is_open());
                ctbs[i].ctfs[pid_start].keys = new __uint128_t[ctbs[i].CTF_capacity[pid_start]];
                read_sst.read((char *)ctbs[i].ctfs[pid_start].keys, sizeof(__uint128_t) * ctbs[i].CTF_capacity[pid_start]);
                read_sst.close();
            }
            ctbs[i].ctfs[pid_start].search_SSTable(wp, search_multi, ctbs[i].CTF_capacity[pid_start], search_multi_length, search_multi_pid);
            uint cursor = pid_start+1;
            uint temp_pid;
            while(true) {
                read_sst.open("../store/SSTable_" + to_string(i) + "-" + to_string(cursor));
                assert(read_sst.is_open());
                if(!ctbs[i].ctfs[cursor].keys){
                    ctbs[i].ctfs[cursor].keys = new __uint128_t[ctbs[i].CTF_capacity[cursor]];
                }
                read_sst.read((char *) ctbs[i].ctfs[cursor].keys, sizeof(__uint128_t) * ctbs[i].CTF_capacity[cursor]);
                read_sst.close();
                if (cursor + 1 < config->CTF_count) {
                    if (ctbs[i].first_widpid[cursor + 1] != wp) {               //must shut down in this cursor
                        cout<<"case 1"<<endl;
                        uint index = 0;
                        while (index <= ctbs[i].CTF_capacity[cursor] - 1) {
                            temp_pid = get_key_oid(ctbs[i].ctfs[cursor].keys[index]) ;
                            if (temp_pid == pid) {
                                id_find_count++;
                                //cout << bg_run[i].sst[cursor].keys[index] << endl;
                                if(search_multi){
                                    search_multi_pid[search_multi_length] = get_key_oid(
                                            ctbs[i].ctfs[cursor].keys[index]) ;
                                    search_multi_length++;
                                }
                            } else break;
                            index++;
                        }
                        break;
                    }
                    if (ctbs[i].first_widpid[cursor + 1] == wp) {               //mustn't shut down in this cursor
                        for (uint j = 0; j < ctbs[i].CTF_capacity[cursor]; j++) {
                            //cout << bg_run[i].sst[cursor].keys[j] << endl;
                            if(search_multi){
                                search_multi_pid[search_multi_length] = get_key_oid(ctbs[i].ctfs[cursor].keys[j]) ;
                                search_multi_length++;
                            }
                        }
                    }
                    cursor++;
                } else {                                           // cursor is the last one, same too bg_run->first_widpid[cursor+1]!=pid
                    uint index = 0;
                    while (index <= ctbs[i].CTF_capacity[cursor] - 1) {
                        temp_pid = get_key_oid(ctbs[i].ctfs[cursor].keys[index]);
                        cout<<"temp_pid: "<<temp_pid<<endl;
                        if (temp_pid == pid) {
                            //cout << bg_run[i].sst[cursor].keys[index] << endl;
                            if(search_multi){
                                search_multi_pid[search_multi_length] = get_key_oid(ctbs[i].ctfs[cursor].keys[index]);
                                search_multi_length++;
                            }
                        } else break;
                        index++;
                    }
                    break;
                }
            }
            ret = true;
*/
        }
    }
    //cout<<"finish disk search "<<pid<<endl;
    return ret;
}

box workbench::parse_to_real_mbr(unsigned short first_low, unsigned short first_high, uint64_t value) {
//    uint first_low0, first_low1, first_high0, first_high1;
//    d2xy(FIRST_HILBERT_BIT/2, first_low, first_low0, first_low1);
//    d2xy(FIRST_HILBERT_BIT/2, first_high, first_high0, first_high1);
//    double float_first_low0 = (double)first_low0/(pow(2,WID_BIT/2) - 1)*(mbr.high[0] - mbr.low[0]) + mbr.low[0];
//    double float_first_low1 = (double)first_low1/(pow(2,WID_BIT/2) - 1)*(mbr.high[1] - mbr.low[1]) + mbr.low[1];
//    double float_first_high0 = (double)first_high0/(pow(2,WID_BIT/2) - 1)*(mbr.high[0] - mbr.low[0]) + mbr.low[0];
//    double float_first_high1 = (double)first_high1/(pow(2,WID_BIT/2) - 1)*(mbr.high[1] - mbr.low[1]) + mbr.low[1];
//    box first(float_first_low0, float_first_low1, float_first_high0, float_first_high1);
//    cerr<<"first\n";
//    first.print();
//
//    uint second_low, second_high;
//    second_low = get_value_mbr_low(value);
//    second_high = get_value_mbr_high(value);
//    uint second_low0, second_low1, second_high0, second_high1;
//    d2xy(FIRST_HILBERT_BIT/2, second_low, second_low0, second_low1);
//    d2xy(FIRST_HILBERT_BIT/2, second_high, second_high0, second_high1);
//    box ret;
//    ret.low[0] = (double)second_low0/15*(float_first_high0 - float_first_low0) + float_first_low0;
//    ret.low[1] = (double)second_low1/15*(float_first_high1 - float_first_low1) + float_first_low1;
//    ret.high[0] = (double)second_high0/15*(float_first_high0 - float_first_low0) + float_first_low0;
//    ret.high[1] = (double)second_high1/15*(float_first_high1 - float_first_low1) + float_first_low1;
//    ret.print();
    box ret;
    return ret;
}

bool PolygonSearchCallback(short * i, box poly_mbr,void* arg){
    vector<pair<short, box>> * ret = (vector<pair<short, box>> *)arg;
    ret->push_back(make_pair(*i, poly_mbr));
    return true;
}

bool workbench::mbr_search_in_disk(box b, uint timestamp) {
    //assert(mbr.contain(b));
    //cout << "mbr disk search" << endl;
    unordered_set<uint> uni;
    bool ret = false, find = false;
    box bit_b = make_bit_box(b);
    uint bit_pos = 0;
    //for (int i = 0; i < ctb_count; i++) {
    for(int i = 0; i < ctb_count; i++){
        if ((ctbs[i].start_time_min < timestamp) && (timestamp < ctbs[i].end_time_max)) {
            if(!ctbs[i].ctfs){
                ctbs[i].ctfs = new CTF[config->CTF_count];
            }
            //cout << "in bg_run" << i << endl;

            uint buffer_find = 0;
            for(uint q = 0; q < ctbs[i].o_buffer.oversize_kv_count; q++){
                if(ctbs[i].o_buffer.boxes[q].intersect(b)){
                    uni.insert(get_key_oid(ctbs[i].o_buffer.keys[i]));
                    buffer_find++;
                    mbr_find_count++;
                    //cout<<"box find!"<<endl;
                    //ctbs[i].o_buffer.boxes[q].print();
                }
            }

            vector<pair<short, box>> intersect_mbrs;
            ctbs[i].box_rtree->Search(b.low, b.high, PolygonSearchCallback, (void *)&intersect_mbrs);
            intersect_sst_count += intersect_mbrs.size();
            for (uint j = 0; j < intersect_mbrs.size(); j++) {
                uint CTF_id = intersect_mbrs[j].first;
                find = false;
                for (uint p = bit_b.low[0]-1; (p <= bit_b.high[0]+1) && (!find); p++) {
                    for (uint q = bit_b.low[1]-1; (q <= bit_b.high[1]+1) && (!find); q++) {
                        bit_pos = xy2d(SID_BIT / 2, p, q);
                        if (ctbs[i].bitmaps[CTF_id * (bit_count / 8) + bit_pos / 8] & (1 << (bit_pos % 8))) {              //mbr intersect bitmap
                            //cerr << "SSTable_" << CTF_id << "bit_pos" << bit_pos << endl;
                            find = true;
                            ret = true;
                            break;
                        }
                    }
                }
                if(find){
                    bit_find_count++;
                    if(!ctbs[i].ctfs[CTF_id].keys){
                        ifstream read_sst;
                        string filename = config->raid_path + to_string(i)+"-"+to_string(CTF_id);
                        //cout<<filename<<endl;
                        read_sst.open(filename);                   //final place is not high+1, but high
                        assert(read_sst.is_open());
                        ctbs[i].ctfs[CTF_id].keys = new __uint128_t[ctbs[i].CTF_capacity[CTF_id]];
                        read_sst.read((char *)ctbs[i].ctfs[CTF_id].keys, sizeof(__uint128_t) * ctbs[i].CTF_capacity[CTF_id]);
                        //cout<<"read right"<<endl;
                        read_sst.close();
                    }
                    uint this_find = 0;
                    for(uint q = 0; q < ctbs[i].CTF_capacity[CTF_id]; q++){
                        uint pid = get_key_oid(ctbs[i].ctfs[CTF_id].keys[q]);
                        box key_box;
                        parse_mbr(ctbs[i].ctfs[CTF_id].keys[q], key_box, intersect_mbrs[j].second);
                        if(b.intersect(key_box)){
                            uni.insert(pid);
                            this_find++;
                            mbr_find_count++;
                            //cout<<"box find!"<<endl;
                            //key_box.print();

                        }
                    }
                    //cerr<<this_find<<"finds in sst "<<CTF_id<<endl;

                }

            }
        }
    }
    mbr_unique_find = uni.size();
    uni.clear();
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

void workbench::dump_meta(const char *path) {
    struct timeval start_time = get_cur_time();
    string bench_path = string(path) + "workbench";
    ofstream wf(bench_path, ios::out|ios::binary|ios::trunc);
    wf.write((char *)config, sizeof(generator_configuration));        //the config of pipeline is generator_configuration
    cout << "sizeof(*config)" << sizeof(*config) << endl;
    cout << "sizeof(configuration)" << sizeof(configuration) << endl;
    cout << "sizeof(generator_configuration)" << sizeof(generator_configuration) << endl;
    wf.write((char *)this, sizeof(workbench));
    for(int i = 0; i < ctb_count; i++){
        string CTB_path = string(path) + "CTB" + to_string(i);
        dump_CTB_meta(CTB_path.c_str(), i);
    }
    wf.close();
    logt("bench meta dumped to %s",start_time,path);
}

void workbench::dump_CTB_meta(const char *path, int i) {
    struct timeval start_time = get_cur_time();
    ofstream wf(path, ios::out|ios::binary|ios::trunc);
    wf.write((char *)&ctbs[i], sizeof(CTB));
    wf.write((char *)ctbs[i].first_widpid, config->CTF_count * sizeof(uint64_t));
    wf.write((char *)ctbs[i].sids, config->num_objects * sizeof(unsigned short));
    wf.write((char *)ctbs[i].bitmaps, bitmaps_size * sizeof(unsigned char));
    wf.write((char *)ctbs[i].bitmap_mbrs, config->CTF_count * sizeof(box));
    wf.write((char *)ctbs[i].CTF_capacity, config->CTF_count * sizeof(uint));
    wf.write((char *)ctbs[i].o_buffer.keys, ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t));
    wf.write((char *)ctbs[i].o_buffer.boxes, ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box));
    //RTree
    wf.close();
    logt("CTB meta %d dump to %s",start_time, i, path);
}

void workbench::load_CTB_meta(const char *path, int i) {
    struct timeval start_time = get_cur_time();
    ifstream in(path, ios::in | ios::binary);

    if (!in.is_open()) {
        std::cerr << "Error opening file: " << path << std::endl;
    } else {
        std::streamsize size = in.tellg();
        std::cout << "File size: " << size << std::endl;
    }

    in.read((char *)&ctbs[i], sizeof(CTB));
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
    ctbs[i].box_rtree = new RTree<short *, double, 2, double>();
    for(uint j = 0; j < config->CTF_count; ++j){
        ctbs[i].box_rtree->Insert(ctbs[i].bitmap_mbrs[j].low, ctbs[i].bitmap_mbrs[j].high, new short(j));
    }
    logt("CTB meta %d load from %s",start_time, i, path);
}


