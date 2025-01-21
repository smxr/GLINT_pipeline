#include "../tracing/workbench.h"

using namespace std;

//constexpr size_t CACHE_LINE_SIZE = 64;
//
//struct PaddedAtomicBool {
//    atomic<bool> value;
//    char padding[CACHE_LINE_SIZE - sizeof(atomic<bool>)];
//
//    PaddedAtomicBool() : value(false) {}
//};

void a_clear_cache(){
    string cmd = "sync; sudo sh -c 'echo 1 > /proc/sys/vm/drop_caches'";        //sudo!!!
    if(system(cmd.c_str())!=0){
        fprintf(stderr, "Error when disable buffer cache\n");
    }
    cout << "clear_cache" << endl;
}

workbench * a_load_meta(const char *path) {
    log("loading meta from %s", path);
    string bench_path = string(path) + "workbench";
    struct timeval start_time = get_cur_time();
    ifstream in(bench_path, ios::in | ios::binary);
    if(!in.is_open()){
        log("%s cannot be opened",bench_path.c_str());
        exit(0);
    }
    generator_configuration * config = new generator_configuration();
    workbench * bench = new workbench(config);
    in.read((char *)config, sizeof(generator_configuration));               //also read meta
    in.read((char *)bench, sizeof(workbench));      //bench->config = NULL
    bench->config = config;
    bench->ctbs = new CTB[40];
    for(int i = 0; i < 40; i++){
        //CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s",start_time, bench_path.c_str());
    return bench;
}

uint a_search_keys_by_pid(__uint128_t* keys, uint64_t wp, uint capacity, vector<__uint128_t> & v_keys, vector<uint> & v_indices){
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

struct dump_args {
    string path;
    __uint128_t * keys;
    uint SIZE;
};

bool cmp(pair<key_info, f_box> a, pair<key_info, f_box> b){
    if(a.first.oid == b.first.oid){
        return a.first.target < b.first.target;
    }
    return a.first.oid < b.first.oid;
}

void cpu_sort_by_key(key_info* keys, f_box* values, uint n) {
    vector<pair<key_info, f_box>> key_value_pairs(n);

    // Pack keys and values
#pragma omp parallel for num_threads(128)
    for (size_t i = 0; i < n; ++i) {
        key_value_pairs[i] = {keys[i], values[i]};
    }

    // Sort by keys
    sort(key_value_pairs.begin(), key_value_pairs.end(), cmp);

    // Unpack sorted keys and values
    for (size_t i = 0; i < n; ++i) {
        keys[i] = key_value_pairs[i].first;
        values[i] = key_value_pairs[i].second;
    }
}

void * merge_dump(workbench * bench, uint start_ctb, uint merge_ctb_count, vector< vector<key_info> > &keys_with_sid, vector< vector<f_box> > &mbrs_with_sid,
                  vector< vector<uint> > &invert_index, vector< pair< vector<key_info>, vector<f_box> > > &objects_map){
    uint c_ctb_id = start_ctb / merge_ctb_count;
    cout<<"step into the sst_dump"<<endl;
    //new_bench *bench = (new_bench *)arg;
    CTB C_ctb;
    C_ctb.sids = new unsigned short[bench->config->num_objects];
    copy(bench->ctbs[start_ctb].sids, bench->ctbs[start_ctb].sids + bench->config->num_objects, C_ctb.sids);
    //find all oversize sid
    for(int i = 1; i < merge_ctb_count; i++){
        for(int j = 0; j < bench->config->num_objects; j++){
            if(bench->ctbs[start_ctb + i].sids[j] == 1){
                C_ctb.sids[j] = 1;
            }
        }
    }
    key_info * another_o_buffer_k = new key_info[1024*1024*1024/16];        //1G
    f_box * another_o_buffer_b = new f_box[1024*1024*1024/16];
    atomic<int> another_o_count = 0;

    C_ctb.ctfs = new CTF[bench->config->CTF_count];

    cout<<"sst_capacity:"<<bench->config->kv_capacity<<endl;
    bench->merge_kv_capacity = bench->config->kv_restriction * merge_ctb_count;         //less than that

    struct timeval bg_start = get_cur_time();
    for(int i = 0; i < merge_ctb_count; i++){
#pragma omp parallel for num_threads(bench->config->CTF_count)
        for (int j = 0; j < bench->config->CTF_count; j++) {
            CTF * ctf = &bench->ctbs[start_ctb + i].ctfs[j];
            uint8_t * data = reinterpret_cast<uint8_t *>(ctf->keys);
            for (int k = 0; k < bench->ctbs[start_ctb + i].ctfs[j].CTF_kv_capacity; k++) {
                key_info temp_ki;
                __uint128_t temp_128 = 0;
                memcpy(&temp_128, data + k * ctf->key_bit / 8, ctf->key_bit / 8);
                uint64_t value_mbr = 0;
                ctf->parse_key(temp_128, temp_ki, value_mbr);
                temp_ki.end += ctf->end_time_min;                                           //real end
                f_box temp_mbr = ctf->new_parse_mbr_f_box(value_mbr);
                if (bench->ctbs[start_ctb + i].sids[temp_ki.oid] == 1) {                    //get all oversized keys
                    int pos = another_o_count.fetch_add(1, memory_order_seq_cst);
                    another_o_buffer_k[pos] = temp_ki;
                    another_o_buffer_b[pos] = temp_mbr;
                } else {
                    if(i == 0){
                        if(invert_index[j].empty() || invert_index[j].back() != temp_ki.oid){
                            invert_index[j].push_back(temp_ki.oid);
                        }
                    }
                    objects_map[temp_ki.oid].first.push_back(temp_ki);
                    objects_map[temp_ki.oid].second.push_back(temp_mbr);
                }
            }
        }
    }
    double invert_index_time = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tinvert_index:\t%.2f\n",invert_index_time);

    //bg_start = get_cur_time();
#pragma omp parallel for num_threads(bench->config->CTF_count)
    for (int j = 0; j < bench->config->CTF_count; j++) {
        uint v_capacity = 0;
        for (int k = 0; k < invert_index[j].size(); k++) {
            v_capacity += objects_map[invert_index[j][k]].first.size();
        }
        C_ctb.ctfs[j].CTF_kv_capacity = v_capacity;
        keys_with_sid[j].reserve(v_capacity);
        mbrs_with_sid[j].reserve(v_capacity);
        for (int k = 0; k < invert_index[j].size(); k++) {
            copy(objects_map[invert_index[j][k]].first.begin(),objects_map[invert_index[j][k]].first.end(),
                 back_inserter(keys_with_sid[j]) );
            copy(objects_map[invert_index[j][k]].second.begin(),objects_map[invert_index[j][k]].second.end(),
                 back_inserter(mbrs_with_sid[j]) );
//            keys_with_wid[j].insert(keys_with_wid[j].end(),
//                                    objects_map[invert_index[j][k]].first.begin(), objects_map[invert_index[j][k]].first.end());
//            mbrs_with_wid[j].insert(mbrs_with_wid[j].end(),
//                                    objects_map[invert_index[j][k]].second.begin(), objects_map[invert_index[j][k]].second.end());
        }
    }
    double expend_time = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\texpend_time:\t%.2f\n",expend_time);

    uint rest_total_count = 0;
    for(uint i = 0; i < bench->config->CTF_count; i++){
        rest_total_count += C_ctb.ctfs[i].CTF_kv_capacity;
        //C_ctb.first_widpid[i] = keys_with_wid[i][0] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
    }
    cout << "rest_total_count" << rest_total_count << endl;

    double analyze_time = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tanalyze_time:\t%.2f\n",analyze_time);

    //get limit abd bits
    C_ctb.end_time_min = 1 << 30;
    C_ctb.end_time_max = 0;
    for(int i = 0; i < merge_ctb_count; i++){
        C_ctb.end_time_min = min(C_ctb.end_time_min, bench->ctbs[start_ctb + i].end_time_min);
        C_ctb.end_time_max = max(C_ctb.end_time_max, bench->ctbs[start_ctb + i].end_time_max);
    }
#pragma omp parallel for num_threads(bench->config->CTF_count)
    for(uint i = 0; i < bench->config->CTF_count; i++){
        float local_low[2] = {100000.0,100000.0};
        float local_high[2] = {-100000.0,-100000.0};
        uint local_start_time_min = 1 << 30;
        uint local_start_time_max = 0;
        for(uint j = 0; j < C_ctb.ctfs[i].CTF_kv_capacity; j++){
            local_low[0] = min(local_low[0], mbrs_with_sid[i][j].low[0]);
            local_low[1] = min(local_low[1], mbrs_with_sid[i][j].low[1]);
            local_high[0] = max(local_high[0], mbrs_with_sid[i][j].high[0]);
            local_high[1] = max(local_high[1], mbrs_with_sid[i][j].high[1]);

            uint this_start = keys_with_sid[i][j].end - keys_with_sid[i][j].duration;
            local_start_time_min = min(local_start_time_min, this_start);
            local_start_time_max = max(local_start_time_max, this_start);
        }
        C_ctb.ctfs[i].ctf_mbr.low[0] = local_low[0];
        C_ctb.ctfs[i].ctf_mbr.low[1] = local_low[1];
        C_ctb.ctfs[i].ctf_mbr.high[0] = local_high[0];
        C_ctb.ctfs[i].ctf_mbr.high[1] = local_high[1];
        C_ctb.ctfs[i].start_time_min = local_start_time_min;
        C_ctb.ctfs[i].start_time_max = local_start_time_max;
        C_ctb.ctfs[i].end_time_min = C_ctb.end_time_min;
        C_ctb.ctfs[i].end_time_max = C_ctb.end_time_max;
        C_ctb.ctfs[i].get_ctf_bits(bench->mbr, bench->config);
    }
    double get_limit = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tget_limit:\t%.2f\n",get_limit);

#pragma omp parallel for num_threads(bench->config->CTF_count)
    for(uint i = 0; i < bench->config->CTF_count; i++){
        C_ctb.ctfs[i].bitmap = new uint8_t[C_ctb.ctfs[i].ctf_bitmap_size];
        f_box * CTF_mbr = &C_ctb.ctfs[i].ctf_mbr;
        for(int j = 0; j < C_ctb.ctfs[i].CTF_kv_capacity; j+=10) {
            uint low0 = (mbrs_with_sid[i][j].low[0] - CTF_mbr->low[0])/(CTF_mbr->high[0] - CTF_mbr->low[0]) * C_ctb.ctfs[i].x_grid;
            uint low1 = (mbrs_with_sid[i][j].low[1] - CTF_mbr->low[1])/(CTF_mbr->high[1] - CTF_mbr->low[1]) * C_ctb.ctfs[i].y_grid;
            uint high0 = (mbrs_with_sid[i][j].high[0] - CTF_mbr->low[0])/(CTF_mbr->high[0] - CTF_mbr->low[0]) * C_ctb.ctfs[i].x_grid;
            uint high1 = (mbrs_with_sid[i][j].high[1] - CTF_mbr->low[1])/(CTF_mbr->high[1] - CTF_mbr->low[1]) * C_ctb.ctfs[i].y_grid;
            uint bit_pos = 0;
            for (uint m = low0; m <= high0 && m < C_ctb.ctfs[i].x_grid; m++) {
                for (uint n = low1; n <= high1 && n < C_ctb.ctfs[i].y_grid; n++) {
                    bit_pos = m + n * C_ctb.ctfs[i].x_grid;
                    C_ctb.ctfs[i].bitmap[bit_pos / 8] |= (1 << (bit_pos % 8));
                }
            }
        }
    }
    double write_bitmap_time = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\twrite_bitmap_time:\t%.2f\n",write_bitmap_time);

//            //    //output bitmap
//        CUDA_SAFE_CALL(cudaMemcpy(bench->h_bitmaps[offset], h_bench.d_bitmaps, bench->bitmaps_size, cudaMemcpyDeviceToHost));
//        cerr << "output picked bitmap" << endl;
//        Point * bit_points = new Point[bench->bit_count];
//        uint count_p;
//        for(uint j = 0;j<bench->config->CTF_count; j++){
//            //cerr<<"bitmap"<<j<<endl;
//            cerr<<endl;
//            CTF * temp_ctf = &bench->h_ctfs[offset][j];
//            count_p = 0;
//            bool is_print = false;
//            for(uint i=0;i<bench->bit_count;i++){
//                if(bench->h_bitmaps[offset][j*(bench->bit_count/8) + i/8] & (1<<(i%8))){
//                    if(!is_print){
//                        cout<<i<<"in SST"<<j<<endl;
//                        is_print = true;
//                    }
//                    Point bit_p;
//                    uint x=0,y=0;
//                    x = i % temp_ctf->x_grid;
//                    y = i / temp_ctf->x_grid;
//                    bit_p.x = (double)x/temp_ctf->x_grid*(temp_ctf->ctf_mbr.high[0] - temp_ctf->ctf_mbr.low[0]) + temp_ctf->ctf_mbr.low[0];
//                    bit_p.y = (double)y/temp_ctf->y_grid*(temp_ctf->ctf_mbr.high[1] - temp_ctf->ctf_mbr.low[1]) + temp_ctf->ctf_mbr.low[1];
//                    bit_points[count_p] = bit_p;
//                    count_p++;
//                }
//            }
//            cout<<"bit_points.size():"<<count_p<<endl;
//            print_points(bit_points,count_p);
//            //cerr << "process output bitmap finish" << endl;
//        }
//        delete[] bit_points;

#pragma omp parallel for num_threads(bench->config->CTF_count)
    for(uint i = 0; i < bench->config->CTF_count; i++) {
        CTF * ctf = &C_ctb.ctfs[i];
        uint key_Bytes = ctf->key_bit / 8;
        ctf->keys = new __uint128_t[ctf->CTF_kv_capacity * key_Bytes / sizeof(__uint128_t) + 1];
        uint8_t * data = reinterpret_cast<uint8_t *>(ctf->keys);
        for (int j = 0; j < C_ctb.ctfs[i].CTF_kv_capacity; j++) {
            __uint128_t value_mbr = serialize_mbr(&mbrs_with_sid[i][j], &ctf->ctf_mbr, ctf);
            keys_with_sid[i][j].end += C_ctb.ctfs[i].end_time_min;
            // key =  oid target duration end box
            __uint128_t temp_key =
                    ((__uint128_t) keys_with_sid[i][j].oid << (ctf->id_bit + ctf->duration_bit + ctf->end_bit + ctf->mbr_bit)) +
                    ((__uint128_t) keys_with_sid[i][j].target << (ctf->duration_bit + ctf->end_bit + ctf->mbr_bit)) +
                    ((__uint128_t) keys_with_sid[i][j].duration << (ctf->end_bit + ctf->mbr_bit)) +
                    ((__uint128_t) keys_with_sid[i][j].end << (ctf->mbr_bit)) + value_mbr;
            //uint ave_ctf_size = bench->config->kv_restriction / bench->config->CTF_count * sizeof(__uint128_t);
            memcpy(&data[j * key_Bytes], &temp_key, key_Bytes);
        }
    }
    double key_info_to_new_key = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tkey_info_to_new_key:\t%.2f\n",key_info_to_new_key);

//        for(uint i = 0; i < 100; i++){
//            bench->h_ctfs[offset][0].print_key(bench->h_keys[offset][i]);
//        }

    cpu_sort_by_key(another_o_buffer_k, another_o_buffer_b, another_o_count);
    double new_buffer_sort = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tnew_buffer_sort:\t%.2f\n",new_buffer_sort);

    uint o_count = merge_ctb_count + 1;
    oversize_buffer * ob = new oversize_buffer[o_count];
    for(uint i = 0; i < merge_ctb_count; i++){
        ob[i] = bench->ctbs[start_ctb + i].o_buffer;
    }
    ob[o_count - 1].oversize_kv_count = another_o_count;
    ob[o_count - 1].keys = new __uint128_t[another_o_count];
    ob[o_count - 1].boxes = another_o_buffer_b;
#pragma omp parallel for num_threads(bench->config->num_threads)
    for(uint i = 0; i < another_o_count; i++){
        ob[o_count - 1].keys[i] = ((__uint128_t)another_o_buffer_k[i].oid << (OID_BIT + DURATION_BIT + END_BIT)) +
                                    ((__uint128_t)another_o_buffer_k[i].target << (DURATION_BIT + END_BIT)) +
                                    ((__uint128_t)another_o_buffer_k[i].duration << END_BIT) +
                                    (__uint128_t)(another_o_buffer_k[i].end);
        //ob[o_count - 1].write_o_buffer(ob[o_count - 1].boxes[i], bench->bit_count);       //useless
    }
    double key_info_to_buffer_key = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tkey_info_to_buffer_key:\t%.2f\n",key_info_to_buffer_key);

    oversize_buffer merged_buffer;
    merged_buffer.oversize_kv_count += another_o_count;
    merged_buffer.o_bitmaps = new uint8_t[bench->bitmaps_size];
    for(int i = 0; i < merge_ctb_count; i++){
        merged_buffer.oversize_kv_count += ob[i].oversize_kv_count;
        merged_buffer.start_time_min = min(merged_buffer.start_time_min, ob[i].start_time_min);
        merged_buffer.start_time_max = max(merged_buffer.start_time_max, ob[i].start_time_max);
        merged_buffer.end_time_min = min(merged_buffer.end_time_min, ob[i].end_time_min);
        merged_buffer.end_time_max = max(merged_buffer.end_time_max, ob[i].end_time_max);
#pragma omp parallel for num_threads(bench->config->num_threads)
        for(uint j = 0; j < bench->bitmaps_size; j++) {
            merged_buffer.o_bitmaps[j] |= ob[i].o_bitmaps[j];
        }
    }
    double merged_buffer_bitmap = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tmerged_buffer_bitmap:\t%.2f\n",merged_buffer_bitmap);

    cout << "merged_buffer.oversize_kv_count " << merged_buffer.oversize_kv_count << endl;
    merged_buffer.keys = new __uint128_t[merged_buffer.oversize_kv_count];
    merged_buffer.boxes = new f_box[merged_buffer.oversize_kv_count];
    uint *key_index = new uint[o_count];        //0
    uint taken_id = 0;
    uint kv_count = 0;
    while(kv_count < merged_buffer.oversize_kv_count) {
        __uint128_t temp_key = (__uint128_t) 1 << 126;
        taken_id = 0;
        for (int i = 0; i < o_count; i++) {
            if (key_index[i] < ob[i].oversize_kv_count && temp_key > ob[i].keys[key_index[i]]){
                temp_key = ob[i].keys[key_index[i]];
                taken_id = i;
            }
        }
        merged_buffer.keys[kv_count] = temp_key;
        merged_buffer.boxes[kv_count] = ob[taken_id].boxes[key_index[taken_id]];
        key_index[taken_id]++;
        kv_count++;
    }
    double ordered_Multiway_Merge = get_time_elapsed(bg_start,true);
    fprintf(stdout,"\tordered_Multiway_Merge:\t%.2f\n",ordered_Multiway_Merge);

//    for(int i=0;i<bench->MemTable_count; i++){
//        cout<<"key_index"<<key_index[i]<<endl;
//    }

    string CTB_path = string(bench->config->CTB_meta_path) + "C_CTB" + to_string(c_ctb_id);
    bench->dump_CTB_meta(CTB_path.c_str(), &C_ctb);
    logt("dumped meta for CTB %d",bg_start, c_ctb_id);

#pragma omp parallel for num_threads(bench->config->CTF_count)
    for(uint i = 0; i < bench->config->CTF_count; i++){
        string ctf_path = string(bench->config->CTB_meta_path) + "C_STcL" + to_string(c_ctb_id)+"-"+to_string(i);
        C_ctb.ctfs[i].dump_meta(ctf_path);
    }
    logt("dumped meta for CTF",bg_start);
    C_ctb.print_meta();
    return NULL;
}

int main(int argc, char **argv){
    clear_cache();
    string path = "../data/meta/";
    //workbench * bench = C_load_meta(path.c_str());
    uint max_ctb = 5;
    workbench * bench = load_meta(path.c_str(), max_ctb);
    cout << "bench->ctb_count " << bench->ctb_count << endl;
    cout << "max_ctb " << max_ctb << endl;
    bench->ctb_count = max_ctb;

    for(int i = 0; i < bench->ctb_count; i++) {
        for (int j = 0; j < bench->config->CTF_count; j++) {
            bench->ctbs[i].ctfs[j].keys = nullptr;
        }
    }

    vector< vector<key_info> > keys_with_sid(bench->config->CTF_count);
    vector< vector<f_box> > mbrs_with_sid(bench->config->CTF_count);
    vector< vector<uint> > invert_index(bench->config->CTF_count);
    for (auto& vec : invert_index) {
        vec.reserve(200000);        //20000000 / 100
    }
    vector< pair< vector<key_info>, vector<f_box> > > objects_map(bench->config->num_objects);

    uint merge_ctb_count = 5;
    for(uint i = 0; i < 5; i += merge_ctb_count){
        for(uint j = i; j < i + merge_ctb_count; j++){
            for(uint k = 0; k < bench->config->CTF_count; k++){
                bench->load_CTF_keys(j, k);
            }
        }
        struct timeval bg_start = get_cur_time();
        merge_dump(bench, i, merge_ctb_count, keys_with_sid, mbrs_with_sid, invert_index, objects_map);
        double compaction_total = get_time_elapsed(bg_start,true);
        fprintf(stdout,"\tcompaction_total:\t%.2f\n",compaction_total);

        //init
        for(uint j = i; j < i + merge_ctb_count; j++){
            for(uint k = 0; k < bench->config->kv_capacity; k++){
                delete []bench->ctbs[j].ctfs[k].keys;
            }
        }
        for(uint j = 0; j < bench->config->CTF_count; j++){
            keys_with_sid[j].clear();
            mbrs_with_sid[j].clear();
            invert_index[j].clear();
        }
        for(auto p : objects_map){
            p.first.clear();
            p.second.clear();
        }
    }
    return 0;
}
