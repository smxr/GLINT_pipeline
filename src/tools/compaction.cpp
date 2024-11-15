#include "../geometry/Map.h"
#include "../tracing/generator.h"
#include "../tracing/trace.h"

using namespace std;

#include "stdint.h"
#include <stdlib.h>

template <typename T, size_t alignment = alignof(T)>
struct AlignedInt
{
    static_assert(alignment >= sizeof(T), "Alignment must be greater than or equal to size of T.");
    union
    {
        char padding[alignment];
        T value = 0;
    };
};
using AlignedIntType = AlignedInt<int, 64>;

// constexpr size_t CACHE_LINE_SIZE = 64;
//
// struct PaddedAtomicBool {
//     std::atomic<bool> value;
//     char padding[CACHE_LINE_SIZE - sizeof(std::atomic<bool>)];
//
//     PaddedAtomicBool() : value(false) {}
// };

void a_clear_cache()
{
    string cmd = "sync; sudo sh -c 'echo 1 > /proc/sys/vm/drop_caches'"; // sudo!!!
    if (system(cmd.c_str()) != 0)
    {
        fprintf(stderr, "Error when disable buffer cache\n");
    }
    cout << "clear_cache" << endl;
}

workbench *a_load_meta(const char *path)
{
    log("loading meta from %s", path);
    string bench_path = string(path) + "workbench";
    struct timeval start_time = get_cur_time();
    ifstream in(bench_path, ios::in | ios::binary);
    if (!in.is_open())
    {
        log("%s cannot be opened", bench_path.c_str());
        exit(0);
    }
    generator_configuration *config = new generator_configuration();
    workbench *bench = new workbench(config);
    in.read((char *)config, sizeof(generator_configuration)); // also read meta
    in.read((char *)bench, sizeof(workbench));                // bench->config = NULL
    bench->config = config;
    bench->ctbs = new CTB[40];
    for (int i = 0; i < 40; i++)
    {
        // CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s", start_time, bench_path.c_str());
    return bench;
}

uint a_search_keys_by_pid(__uint128_t *keys, uint64_t wp, uint capacity, vector<__uint128_t> &v_keys, vector<uint> &v_indices)
{
    uint count = 0;
    // cout<<"into search_SSTable wp "<< wp <<endl;
    int find = -1;
    int low = 0;
    int high = capacity - 1;
    int mid;
    uint64_t temp_wp;
    while (low <= high)
    {
        mid = (low + high) / 2;
        temp_wp = keys[mid] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
        //        cout << get_key_wid(keys[mid]) <<'-' << get_key_pid(keys[mid]) << endl;
        //        cout << "temp_wp" << temp_wp << endl;
        if (temp_wp == wp)
        {
            find = mid;
            break;
        }
        else if (temp_wp > wp)
        {
            high = mid - 1;
        }
        else
        {
            low = mid + 1;
        }
    }
    if (find == -1)
    {
        // cout<<"cannot find"<<endl;
        return 0;
    }
    // cout<<"exactly find"<<endl;
    uint cursor = find;
    while (temp_wp == wp && cursor >= 1)
    {
        cursor--;
        temp_wp = keys[cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
    }
    if (temp_wp == wp && cursor == 0)
    {
        count++;
        v_keys.push_back(keys[cursor]);
        v_indices.push_back(cursor);
    }
    while (cursor + 1 < capacity)
    {
        cursor++;
        temp_wp = keys[cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
        if (temp_wp == wp)
        {
            count++;
            v_keys.push_back(keys[cursor]);
            v_indices.push_back(cursor);
        }
        else
            break;
    }
    // cout<<"find !"<<endl;
    return count;
}

struct dump_args
{
    string path;
    __uint128_t *keys;
    uint SIZE;
};

void *a_parallel_dump(void *arg)
{
    dump_args *pargs = (dump_args *)arg;
    ofstream SSTable_of;
    SSTable_of.open(pargs->path.c_str(), ios::out | ios::binary | ios::trunc);
    // cout << pargs->path << endl;
    if (!SSTable_of.is_open())
    {
        cout << pargs->path << endl;
    }
    assert(SSTable_of.is_open());
    SSTable_of.write((char *)pargs->keys, pargs->SIZE);
    SSTable_of.flush();
    SSTable_of.close();
    return NULL;
}

vector<pair<vector<__uint128_t>, vector<box>>> objects_map;
bool inited = false;

void *merge_dump(new_bench *bench, uint start_ctb, uint merge_ctb_count)
{
    if (inited)
    {
        for (size_t i = 0; i < objects_map.size(); i++)
        {
            objects_map[i].first.clear();
            objects_map[i].second.clear();
        }
        // objects_map.clear();
        objects_map.resize(bench->config->num_objects);
    }
    // uint start_ctb = 0;         //0~4   //compactiong_start_ctb
    // uint merge_ctb_count = 5;         //bench->config->MemTable_capacity/2
    struct timeval bg_start = get_cur_time();
    struct timeval bg_start1 = get_cur_time();

    uint c_ctb_id = start_ctb / merge_ctb_count;
    cout << "step into the sst_dump" << endl;
    // new_bench *bench = (new_bench *)arg;
    bench->compacted_ctbs[c_ctb_id].first_widpid = new uint64_t[bench->config->CTF_count];
    bench->compacted_ctbs[c_ctb_id].sids = new unsigned short[bench->config->num_objects];
    bench->compacted_ctbs[c_ctb_id].bitmaps = new unsigned char[bench->bitmaps_size];
    bench->compacted_ctbs[c_ctb_id].bitmap_mbrs = new box[bench->config->CTF_count];
    bench->compacted_ctbs[c_ctb_id].CTF_capacity = new uint[bench->config->CTF_count];
    bench->dumping = true;
    bench->compacted_ctbs[c_ctb_id].ctfs = NULL;
    cout << "sst_capacity:" << bench->config->kv_capacity << endl;
    bench->merge_kv_capacity = bench->config->kv_restriction * merge_ctb_count;

    vector<vector<__uint128_t>> keys_with_wid(bench->config->CTF_count);
    vector<vector<box>> mbrs_with_wid(bench->config->CTF_count);
    vector<vector<uint>> invert_index(bench->config->CTF_count);
    if (!inited)
    {
        objects_map.resize(bench->config->num_objects);
    }
    std::vector<int> oid2cnt(bench->config->num_objects);

    // 提前计算出所需要的大小
    int prealloc_size = 0;
    for (int j = 0; j < bench->config->CTF_count; j++)
    {
        invert_index[j].reserve(bench->ctbs[start_ctb].CTF_capacity[j]);
    }
    double init_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\tinit_time:\t%.2f\n", init_time);

    copy(bench->ctbs[start_ctb].sids, bench->ctbs[start_ctb].sids + bench->config->num_objects, bench->compacted_ctbs[c_ctb_id].sids);

    // 枚举所有待merge的ctb
    for (int i = 0; i < merge_ctb_count; i++)
    {
        // 枚举所有ctf
#pragma omp parallel for num_threads(bench->config->CTF_count)
        for (int j = 0; j < bench->config->CTF_count; j++)
        {
            // 枚举ctf的中的所有key
            for (int k = 0; k < bench->ctbs[start_ctb + i].CTF_capacity[j]; k++)
            {
                //
                uint oid = get_key_oid(bench->ctbs[start_ctb + i].ctfs[j].keys[k]);
                if (bench->ctbs[start_ctb + i].sids[oid] != 1)
                {
                    oid2cnt[oid]++;
                }
            }
        }
    }
    if (!inited)
    {
        inited = true;
        for (int i = 0; i < objects_map.size(); i++)
        {
            objects_map[i].first.reserve(oid2cnt[i]);
            objects_map[i].second.reserve(oid2cnt[i]);
            // objects_map[i].first.reserve(40);
            // objects_map[i].second.reserve(40);
        }
    }

    for (int i = 0; i < merge_ctb_count; i++)
    {
        // 枚举所有ctf
#pragma omp parallel for num_threads(64)
        for (int j = 0; j < bench->config->CTF_count; j++)
        {
            // 枚举ctf的中的所有key
            for (int k = 0; k < bench->ctbs[start_ctb + i].CTF_capacity[j]; k++)
            {
                //
                uint oid = get_key_oid(bench->ctbs[start_ctb + i].ctfs[j].keys[k]);
                if (bench->ctbs[start_ctb + i].sids[oid] == 1)
                {
                }
                else
                {
                    if (i == 0)
                    {
                        if (invert_index[j].empty() || invert_index[j].back() != oid)
                        {
                            // 将ctb中所有ctf中的oid放入invert_index中，构建一个反向的索引，通过i j直接获得oid
                            invert_index[j].push_back(oid);
                        }
                    }
                    box temp_mbr;
                    parse_mbr(bench->ctbs[start_ctb + i].ctfs[j].keys[k], temp_mbr,
                              bench->ctbs[start_ctb + i].bitmap_mbrs[j]);
                    // 将相同oid的mbr收集起来
                    objects_map[oid].first.push_back(bench->ctbs[start_ctb + i].ctfs[j].keys[k]);
                    objects_map[oid].second.push_back(temp_mbr);
                }
            }
        }
    }
    double invert_index_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\tinvert_index:\t%.2f\n", invert_index_time);

    // bg_start = get_cur_time();
#pragma omp parallel for num_threads(bench->config->CTF_count)
    for (int j = 0; j < bench->config->CTF_count; j++)
    {
        uint v_capacity = 0;
        // 计算同一oid的key的数量
        for (int k = 0; k < invert_index[j].size(); k++)
        {
            v_capacity += objects_map[invert_index[j][k]].first.size();
        }
        // 压缩后的大小
        bench->compacted_ctbs[c_ctb_id].CTF_capacity[j] = v_capacity;
        keys_with_wid[j].reserve(v_capacity);
        mbrs_with_wid[j].reserve(v_capacity);
        for (int k = 0; k < invert_index[j].size(); k++)
        {
            copy(objects_map[invert_index[j][k]].first.begin(), objects_map[invert_index[j][k]].first.end(),
                 std::back_inserter(keys_with_wid[j]));
            copy(objects_map[invert_index[j][k]].second.begin(), objects_map[invert_index[j][k]].second.end(),
                 std::back_inserter(mbrs_with_wid[j]));
            //            keys_with_wid[j].insert(keys_with_wid[j].end(),
            //                                    objects_map[invert_index[j][k]].first.begin(), objects_map[invert_index[j][k]].first.end());
            //            mbrs_with_wid[j].insert(mbrs_with_wid[j].end(),
            //                                    objects_map[invert_index[j][k]].second.begin(), objects_map[invert_index[j][k]].second.end());
        }
    }
    double expend_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\texpend_time:\t%.2f\n", expend_time);
    // invert_index.clear();
    //    invert_index.shrink_to_fit();
    // objects_map.clear();
    //    objects_map.shrink_to_fit();

    uint rest_total_count = 0;
    for (uint i = 0; i < bench->config->CTF_count; i++)
    {
        rest_total_count += bench->compacted_ctbs[c_ctb_id].CTF_capacity[i];
        bench->compacted_ctbs[c_ctb_id].first_widpid[i] = keys_with_wid[i][0] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
    }
    cout << "rest_total_count" << rest_total_count << endl;

    double other_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\tother_time:\t%.2f\n", other_time);

    // bitmap is not important, with low filter rate
    // copy(bench->ctbs[start_ctb].bitmaps, bench->ctbs[start_ctb].bitmaps + bench->bitmaps_size, bench->compacted_ctbs[c_ctb_id].bitmaps);

    // simple parallel with good performence
#pragma omp parallel for num_threads(bench->config->CTF_count)
    for (uint i = 0; i < bench->config->CTF_count; i++)
    {
        for (int j = 0; j < mbrs_with_wid[i].size(); j++)
        {
            uint low0 = (mbrs_with_wid[i][j].low[0] - bench->mbr.low[0]) / (bench->mbr.high[0] - bench->mbr.low[0]) *
                        (1ULL << (SID_BIT / 2));
            uint low1 = (mbrs_with_wid[i][j].low[1] - bench->mbr.low[1]) / (bench->mbr.high[1] - bench->mbr.low[1]) *
                        (1ULL << (SID_BIT / 2));
            uint high0 = (mbrs_with_wid[i][j].high[0] - bench->mbr.low[0]) / (bench->mbr.high[0] - bench->mbr.low[0]) *
                         (1ULL << (SID_BIT / 2));
            uint high1 = (mbrs_with_wid[i][j].high[1] - bench->mbr.low[1]) / (bench->mbr.high[1] - bench->mbr.low[1]) *
                         (1ULL << (SID_BIT / 2));
            for (uint m = low0; m <= high0; m++)
            {
                for (uint n = low1; n <= high1; n++)
                {
                    uint bit_pos = xy2d(SID_BIT / 2, m, n);
                    bench->compacted_ctbs[c_ctb_id].bitmaps[i * (bench->bit_count / 8) + bit_pos / 8] |= (1
                                                                                                          << (bit_pos % 8));
                }
            }
            // bench->bg_run[old_big].bitmap_mbrs[bitmap_id].update(temp_real_mbrs[i]);        //not use bitmap
        }
    }
    double write_bitmap_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\twrite_bitmap_time:\t%.2f\n", write_bitmap_time);

    // parallel but atomic
    //    vector<PaddedAtomicBool> atomic_bitmap(bench->bit_count);
    // #pragma omp parallel for num_threads(bench->config->num_threads)
    //    for (size_t i = 0; i < bench->bit_count; ++i) {                 //the only init method
    //        atomic_bitmap[i].value.store(false, std::memory_order_relaxed);
    //    }
    //    for(uint i = 0; i < bench->config->CTF_count; i++){
    // #pragma omp parallel for num_threads(bench->config->num_threads)
    //        for(int j = 0; j < mbrs_with_wid[i].size(); j++) {
    //            uint low0 = (mbrs_with_wid[i][j].low[0] - bench->mbr.low[0]) / (bench->mbr.high[0] - bench->mbr.low[0]) *
    //                        (1ULL << (SID_BIT / 2));
    //            uint low1 = (mbrs_with_wid[i][j].low[1] - bench->mbr.low[1]) / (bench->mbr.high[1] - bench->mbr.low[1]) *
    //                        (1ULL << (SID_BIT / 2));
    //            uint high0 = (mbrs_with_wid[i][j].high[0] - bench->mbr.low[0]) / (bench->mbr.high[0] - bench->mbr.low[0]) *
    //                         (1ULL << (SID_BIT / 2));
    //            uint high1 = (mbrs_with_wid[i][j].high[1] - bench->mbr.low[1]) / (bench->mbr.high[1] - bench->mbr.low[1]) *
    //                         (1ULL << (SID_BIT / 2));
    //            for (uint m = low0; m <= high0; m++) {
    //                for (uint n = low1; n <= high1; n++) {
    //                    uint bit_pos = xy2d(SID_BIT / 2, m, n);
    //                    if (!atomic_bitmap[bit_pos].value.load(std::memory_order_acquire)) {
    //                        atomic_bitmap[bit_pos].value.store(true, std::memory_order_release);
    //                    }
    //                    //atomic_bitmap[bit_pos].value.store(true, std::memory_order_relaxed);
    //                }
    //            }
    //        }
    //        //without parallel
    //        for (uint k = 0; k < bench->bit_count; k++) {
    //            if (atomic_bitmap[k].value) {     //if (atomic_bitmap[k].load(std::memory_order_relaxed))
    //                bench->compacted_ctbs[c_ctb_id].bitmaps[i * (bench->bit_count / 8) + k / 8] |= (1 << (k % 8));
    //                atomic_bitmap[k].value.store(false, std::memory_order_relaxed);           //init
    //            }
    //        }
    //    }
    //    double write_bitmap_time = get_time_elapsed(bg_start,true);
    //    fprintf(stdout,"\twrite_bitmap_time:\t%.2f\n",write_bitmap_time);
    //    atomic_bitmap.clear();
    //    atomic_bitmap.shrink_to_fit();
    //    //output bitmap
    //    cerr << "output picked bitmap" << endl;
    //    Point * bit_points = new Point[bench->bit_count];
    //    uint count_p;
    //    for(uint i = 0; i < bench->config->CTF_count; i++) {
    //        cerr << endl;
    //        count_p = 0;
    //        for (uint j = 0; j < bench->bit_count; j++) {
    //            if (bench->compacted_ctbs[c_ctb_id].bitmaps[i * (bench->bit_count / 8) + j / 8] & (1 << (j % 8))) {
    //                Point bit_p;
    //                uint x = 0, y = 0;
    //                d2xy(SID_BIT / 2, j, x, y);
    //                bit_p.x = (double) x / (1ULL << (SID_BIT/2)) * (bench->mbr.high[0] - bench->mbr.low[0]) +
    //                          bench->mbr.low[0];           //int low0 = (f_low0 - bench->mbr.low[0])/(bench->mbr.high[0] - bench->mbr.low[0]) * (pow(2,WID_BIT/2) - 1);
    //                bit_p.y = (double) y / (1ULL << (SID_BIT/2)) * (bench->mbr.high[1] - bench->mbr.low[1]) +
    //                          bench->mbr.low[1];               //int low1 = (f_low1 - bench->mbr.low[1])/(bench->mbr.high[1] - bench->mbr.low[1]) * (pow(2,WID_BIT/2) - 1);
    //                bit_points[count_p] = bit_p;
    //                count_p++;
    //            }
    //        }
    //        print_points(bit_points, count_p);
    //    }
    //    delete[] bit_points;
    //    double output_bitmap = get_time_elapsed(bg_start,true);
    //    fprintf(stdout,"\toutput_bitmap:\t%.2f\n",output_bitmap);

    // bitmap mbr
    // #pragma omp parallel for num_threads(bench->config->num_threads)
    for (uint i = 0; i < bench->config->CTF_count; i++)
    {
        box temp_bitbox;
        for (uint j = 0; j < bench->bit_count; j++)
        {
            if (bench->compacted_ctbs[c_ctb_id].bitmaps[i * (bench->bit_count / 8) + j / 8] & (1 << (j % 8)))
            {
                uint x, y;
                d2xy(SID_BIT / 2, j, x, y);
                Point temp_p(x, y);
                temp_bitbox.update(temp_p);
            }
        }
        bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i].low[0] = temp_bitbox.low[0] / (1ULL << (SID_BIT / 2)) * (bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
        bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i].low[1] = temp_bitbox.low[1] / (1ULL << (SID_BIT / 2)) * (bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
        bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i].high[0] = temp_bitbox.high[0] / (1ULL << (SID_BIT / 2)) * (bench->mbr.high[0] - bench->mbr.low[0]) + bench->mbr.low[0];
        bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i].high[1] = temp_bitbox.high[1] / (1ULL << (SID_BIT / 2)) * (bench->mbr.high[1] - bench->mbr.low[1]) + bench->mbr.low[1];
    }
    double bitmap_mbr_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\tbitmap_mbr_time:\t%.2f\n", bitmap_mbr_time);

    // for (uint i = 0; i < bench->config->CTF_count; i++)
    // {
    //     bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i].print();
    // }

    // write kv_mbr
    for (uint i = 0; i < bench->config->CTF_count; i++)
    {
#pragma omp parallel for num_threads(bench->config->num_threads)
        for (uint j = 0; j < mbrs_with_wid[i].size(); j++)
        {
            __uint128_t value_mbr = serialize_mbr(&mbrs_with_wid[i][j],
                                                  &bench->compacted_ctbs[c_ctb_id].bitmap_mbrs[i]);
            keys_with_wid[i][j] = (keys_with_wid[i][j] & ~((((__uint128_t)1 << MBR_BIT) - 1) << (DURATION_BIT + END_BIT))) + (value_mbr << (DURATION_BIT + END_BIT));
        }
    }
    double write_kv_mbr_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\twrite_kv_mbr_time:\t%.2f\n", write_kv_mbr_time);

    // dump
    string path = string(bench->config->CTB_meta_path) + "C_CTB" + to_string(c_ctb_id);
    bench->dump_CTB_meta(path.c_str(), c_ctb_id);
    dump_args *pargs = new dump_args[bench->config->CTF_count];
    pthread_t threads[bench->config->CTF_count]; // may be larger than config->num_threads
    for (uint i = 0; i < bench->config->CTF_count; i++)
    {
        pargs[i].path = bench->config->raid_path + to_string(i % 8) + "/C_SSTable_" + to_string(c_ctb_id) + "-" + to_string(i);
        pargs[i].SIZE = sizeof(__uint128_t) * keys_with_wid[i].size();
        pargs[i].keys = keys_with_wid[i].data();
        pthread_create(&threads[i], NULL, a_parallel_dump, (void *)&pargs[i]);
        // total_index += bench->h_CTF_capacity[offset][i];
    }
    for (int i = 0; i < bench->config->CTF_count; i++)
    {
        void *status;
        pthread_join(threads[i], &status);
    }

    for (int i = 0; i < merge_ctb_count; i++)
    {
        bench->compacted_ctbs[c_ctb_id].start_time_min = min(bench->compacted_ctbs[c_ctb_id].start_time_min, bench->ctbs[start_ctb + i].start_time_min);
        bench->compacted_ctbs[c_ctb_id].end_time_min = min(bench->compacted_ctbs[c_ctb_id].end_time_min, bench->ctbs[start_ctb + i].end_time_min);
        bench->compacted_ctbs[c_ctb_id].start_time_max = max(bench->compacted_ctbs[c_ctb_id].start_time_max, bench->ctbs[start_ctb + i].start_time_max);
        bench->compacted_ctbs[c_ctb_id].end_time_max = max(bench->compacted_ctbs[c_ctb_id].end_time_max, bench->ctbs[start_ctb + i].end_time_max);
    }

    //    fprintf(stdout,"\tmerge :\t%.2f\n",bench->pro.bg_merge_time);
    //    fprintf(stdout,"\tflush:\t%.2f\n",bench->pro.bg_flush_time);
    //    fprintf(stdout,"\topen:\t%.2f\n",bench->pro.bg_open_time);
    bench->compacted_ctbs[c_ctb_id].print_meta();

    bench->dumping = false;
    double dump_time = get_time_elapsed(bg_start, true);
    fprintf(stdout, "\tdump_time:\t%.2f\n", dump_time);

    double func_time = get_time_elapsed(bg_start1, true);
    fprintf(stdout, "\tfunc_time:\t%.2f\n", func_time);
    return NULL;
}

int main(int argc, char **argv)
{
    a_clear_cache();
    string path = "/data2/xiang/glint_new_pipeline/data/meta/";
    workbench *bench = a_load_meta(path.c_str());
    new_bench *nb = new new_bench(bench->config);
    memcpy(nb, bench, sizeof(workbench));
    nb->compacted_ctbs = new CTB[20];
    cout << nb->ctb_count << endl;
    cout << "search begin" << endl;

    uint merge_ctb_count = 5;
    for (uint i = 0; i < 20; i += merge_ctb_count)
    {
        for (uint j = i; j < i + merge_ctb_count; j++)
        {
            bench->load_big_sorted_run(j);
        }
        struct timeval bg_start = get_cur_time();
        merge_dump(nb, i, merge_ctb_count);
        double compaction_total = get_time_elapsed(bg_start, true);
        fprintf(stdout, "\tcompaction_total:\t%.2f\n", compaction_total);
    }
    return 0;
}
