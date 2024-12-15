/*
 * workbench.cpp
 *
 *  Created on: Feb 18, 2021
 *      Author: teng
 */

#include "workbench.h"
#include <sys/mman.h> // mmap, PROT_READ, MAP_PRIVATE
#include <fcntl.h>    // open, O_RDONLY
#include <unistd.h>   // close

#include <future>
#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

workbench::workbench(workbench *bench) : workbench(bench->config)
{
    grids_stack_index = bench->grids_stack_index;
    schema_stack_index = bench->schema_stack_index;
    cur_time = bench->cur_time;
    mbr = bench->mbr;

    if (config->bloom_filter)
    {
        dwMaxItems = bench->dwMaxItems;
        dProbFalse = bench->dProbFalse;
        dwFilterBits = bench->dwFilterBits;
        dwHashFuncs = bench->dwHashFuncs;
        dwSeed = bench->dwSeed;
        dwFilterSize = bench->dwFilterSize;
    }
    if (true)
    {
        bit_count = bench->bit_count;
        bitmaps_size = bench->bitmaps_size;
    }
}

workbench::workbench(configuration *conf)
{

    config = conf;

    // setup the capacity of each container
    grid_capacity = config->grid_amplify * config->grid_capacity;
    // each grid contains averagely grid_capacity/2 objects, times 4 for enough space
    grids_stack_capacity = 4 * max((uint)100000, config->num_objects / config->grid_capacity);

    // the number of all QTree Nodes
    schema_stack_capacity = 1.6 * grids_stack_capacity;

    tmp_space_capacity = config->num_objects;

    filter_list_capacity = config->num_objects;

    grid_check_capacity = config->refine_size * config->num_objects;

    // meeting_capacity = config->num_objects/4;
    // meeting_capacity = 100;

    insert_lk = new pthread_mutex_t[MAX_LOCK_NUM];
    for (int i = 0; i < MAX_LOCK_NUM; i++)
    {
        pthread_mutex_init(&insert_lk[i], NULL);
    }
    for (int i = 0; i < 100; i++)
    {
        data[i] = NULL;
        data_size[i] = 0;
    }
}

void workbench::clear()
{
    for (int i = 0; i < 500; i++)
    {
        if (data[i] != NULL)
        {
            free(data[i]);
            data[i] = NULL;
            data_size[i] = 0;
        }
    }
    data_index = 0;
    delete insert_lk;
    // ctbs.clear();
}

void *workbench::allocate(size_t size)
{
    lock();
    uint cur_idx = data_index++;
    unlock();
    data[cur_idx] = malloc(size);
    data_size[cur_idx] = size;
    return data[cur_idx];
}

void workbench::claim_space()
{
    size_t size = 0;

    size = grid_capacity * grids_stack_capacity * sizeof(uint);
    grids = (uint *)allocate(size);
    log("\t%.2f MB\tgrids", size / 1024.0 / 1024.0);

    size = grids_stack_capacity * sizeof(uint);
    grid_counter = (uint *)allocate(size);
    // log("\t%.2f MB\tgrid counter",size/1024.0/1024.0);

    size = grids_stack_capacity * sizeof(uint);
    grids_stack = (uint *)allocate(size);
    // log("\t%.2f MB\tgrids stack",size/1024.0/1024.0);
    for (int i = 0; i < grids_stack_capacity; i++)
    {
        grids_stack[i] = i;
    }

    size = schema_stack_capacity * sizeof(QTSchema);
    schema = (QTSchema *)allocate(size);
    log("\t%.2f MB\tschema", size / 1024.0 / 1024.0);

    size = schema_stack_capacity * sizeof(uint);
    schema_stack = (uint *)allocate(size);
    // log("\t%.2f MB\tschema stack",size/1024.0/1024.0);
    for (int i = 0; i < schema_stack_capacity; i++)
    {
        schema_stack[i] = i;
        schema[i].type = INVALID;
    }

    size = grid_check_capacity * sizeof(checking_unit);
    grid_check = (checking_unit *)allocate(size);
    log("\t%.2f MB\tchecking units", size / 1024.0 / 1024.0);

    size = config->num_meeting_buckets * sizeof(meeting_unit);
    meeting_buckets = (meeting_unit *)allocate(size);
    log("\t%.2f MB\tmeeting bucket space", size / 1024.0 / 1024.0);

    //	size = meeting_capacity*sizeof(meeting_unit);
    //	meetings = (meeting_unit *)allocate(size);
    //	log("\t%.2f MB\tmeeting space",size/1024.0/1024.0);
    //
    // #pragma omp parallel for
    //	for(size_t i=0;i<config->num_meeting_buckets;i++){
    //		meeting_buckets[i].key = ULL_MAX;
    //	}

    size = config->MemTable_capacity * sizeof(__uint128_t *); // sort
    h_keys = (__uint128_t **)allocate(size);
    // log("\t%.2f MB\tmeeting bucket space",size/1024.0/1024.0);

    for (int i = 0; i < config->MemTable_capacity; i++)
    {
        size = config->kv_capacity * sizeof(__uint128_t);
        h_keys[i] = (__uint128_t *)allocate(size);
        log("\t%.2f MB\ta element of h_keys", size / 1024.0 / 1024.0);
    }

    size = config->search_single_capacity * sizeof(search_info_unit); // search
    search_single_list = (search_info_unit *)allocate(size);
    size = config->search_single_capacity * sizeof(uint);
    search_multi_pid = (uint *)allocate(size);
    size = config->search_multi_capacity * sizeof(search_info_unit);
    search_multi_list = (search_info_unit *)allocate(size);

    if (config->bloom_filter)
    {
        size = config->MemTable_capacity * sizeof(unsigned char *); // bloom
        pstFilter = (unsigned char **)allocate(size);

#pragma omp parallel for
        for (int i = 0; i < config->MemTable_capacity; i++)
        {
            size = dwFilterSize;
            pstFilter[i] = (unsigned char *)allocate(size);
            log("\t%.2f MB\t pstFilter", size / 1024.0 / 1024.0);
            memset(pstFilter[i], 0, dwFilterSize);
        }
    }

    size = config->num_objects * sizeof(float);
    h_longer_edges = (float *)allocate(size);

    size = config->big_sorted_run_capacity * sizeof(CTB);
    ctbs = (CTB *)allocate(size);

    if (config->save_meetings_pers || config->load_meetings_pers)
    {
        size = config->num_objects * 10 * sizeof(meeting_unit); // 100 * size of d_meetings_ps
        h_meetings_ps = (meeting_unit *)allocate(size);
        log("\t%.2f MB\t h_meetings_ps", 1.0 * size / 1024 / 1024);

        size = 100 * sizeof(uint);
        active_meeting_count_ps = (uint *)allocate(size);
    }

    if (true)
    {
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

        for (int i = 0; i < config->MemTable_capacity; i++)
        {
            size = bitmaps_size;
            h_bitmaps[i] = (unsigned char *)allocate(size);
            log("\t%.2f MB\t h_bitmaps", size / 1024.0 / 1024.0);
            size = config->CTF_count * sizeof(box);
            h_bitmap_mbrs[i] = (box *)allocate(size);
            log("\t%.2f MB\t h_bitmap_mbrs", size / 1024.0 / 1024.0);
            size = config->num_objects * sizeof(unsigned short);
            h_sids[i] = (unsigned short *)allocate(size);
            log("\t%.2f MB\t h_sids", size / 1024.0 / 1024.0);
            size = config->CTF_count * sizeof(uint);
            h_CTF_capacity[i] = (uint *)allocate(size);
            log("\t%.2f MB\t h_CTF_capacity", size / 1024.0 / 1024.0);

            size = config->oversize_buffer_capacity * sizeof(__uint128_t);
            h_oversize_buffers[i].keys = (__uint128_t *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers keys", size / 1024.0 / 1024.0);
            size = config->oversize_buffer_capacity * sizeof(f_box);
            h_oversize_buffers[i].boxes = (f_box *)allocate(size);
            log("\t%.2f MB\t h_oversize_buffers boxes", size / 1024.0 / 1024.0);
        }
    }
}

box workbench::make_bit_box(box b)
{
    box new_b;
    new_b.low[0] = (b.low[0] - mbr.low[0]) / (mbr.high[0] - mbr.low[0]) * (1ULL << (SID_BIT / 2));
    new_b.low[1] = (b.low[1] - mbr.low[1]) / (mbr.high[1] - mbr.low[1]) * (1ULL << (SID_BIT / 2));
    new_b.high[0] = (b.high[0] - mbr.low[0]) / (mbr.high[0] - mbr.low[0]) * (1ULL << (SID_BIT / 2));
    new_b.high[1] = (b.high[1] - mbr.low[1]) / (mbr.high[1] - mbr.low[1]) * (1ULL << (SID_BIT / 2));
    return new_b;
}

struct load_args
{
    string path;
    __uint128_t *keys;
    uint SIZE;
};

void *parallel_load(void *arg)
{
    load_args *pargs = (load_args *)arg;
    ifstream read_ctf;
    read_ctf.open(pargs->path.c_str(), ios::in | ios::binary); // never trunc when multi thread, use app
    // cout << pargs->path << endl;
    assert(read_ctf.is_open());
    read_ctf.read((char *)pargs->keys, pargs->SIZE);
    read_ctf.close();
    return NULL;
}

void workbench::load_big_sorted_run(uint b)
{
    if (!ctbs[b].ctfs)
    {
        ctbs[b].ctfs = new CTF[config->CTF_count];
    }
    for (int i = 0; i < config->CTF_count; i++)
    {
        // if(!ctbs[b].ctfs[i].keys){
        ctbs[b].ctfs[i].keys = new __uint128_t[ctbs[b].CTF_capacity[i]];
    }

    struct timeval start_time = get_cur_time();
    load_args *pargs = new load_args[config->CTF_count];
    pthread_t threads[config->CTF_count];
    for (int i = 0; i < config->CTF_count; i++)
    {
        threads[i] = 0;
    }
    for (int i = 0; i < config->CTF_count; i++)
    {
        // if(!ctbs[b].ctfs[i].keys){
        ctbs[b].ctfs[i].keys = new __uint128_t[ctbs[b].CTF_capacity[i]];
        pargs[i].path = config->raid_path + to_string(i % 8) + "/SSTable_" + to_string(b) + "-" + to_string(i);
        pargs[i].SIZE = sizeof(__uint128_t) * ctbs[b].CTF_capacity[i];
        pargs[i].keys = ctbs[b].ctfs[i].keys;
        int ret = pthread_create(&threads[i], NULL, parallel_load, (void *)&pargs[i]);
        if (ret != 0)
        {
            std::cerr << "Failed to create thread " << i << " with error code: " << ret << std::endl;
            threads[i] = 0;
            continue;
        }
        //}
    }
    for (int i = 0; i < config->CTF_count; i++)
    {
        if (threads[i] != 0)
        {
            void *status;
            int ret = pthread_join(threads[i], &status);
            if (ret != 0)
            {
                std::cerr << "Error joining thread " << i << " with error code: " << ret << std::endl;
            }
        }
    }
    pro.load_keys_time += get_time_elapsed(start_time, false);
    logt("load CTB keys %d", start_time, b);
    delete[] pargs;
}

void workbench::clear_all_keys()
{
    for (int i = 0; i < ctb_count; i++)
    {
        for (int j = 0; j < config->CTF_count; j++)
        {
            if (ctbs[i].ctfs)
            {
                if (ctbs[i].ctfs[j].keys)
                {
                    delete[] ctbs[i].ctfs[j].keys;
                    ctbs[i].ctfs[j].keys = nullptr;
                }
            }
        }
    }
}

bool workbench::search_memtable(uint64_t pid, vector<__uint128_t> &v_keys, vector<uint> &v_indices)
{
    cout << "memtable search " << pid << endl;
    uint offset = 0;
    if (ctb_count % 2 == 1)
    {
        offset = config->MemTable_capacity / 2;
    }
    bool ret = false;
    for (int i = 0; i < MemTable_count; i++)
    { // i<MemTable_count
        uint64_t wp = ((uint64_t)h_sids[offset + i][pid] << OID_BIT) + pid;
        cout << "wp " << wp << endl;
        int find = -1;
        int low = 0;
        int high = config->kv_restriction - 1;
        int mid;
        uint64_t temp_wp;
        // box temp_box;
        while (low <= high)
        {
            mid = (low + high) / 2;
            temp_wp = (h_keys[offset + i][mid] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT));
            cout << "temp_wp" << temp_wp << endl;
            if (temp_wp == wp)
            {
                find = mid;
                ret = true;
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
            cout << "cannot find" << endl;
            break;
        }
        cout << "exactly find" << endl;
        uint cursor = find;
        while (temp_wp == wp && cursor >= 1)
        {
            ret = true;
            cursor--;
            temp_wp = (h_keys[offset + i][cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT));
        }
        if (temp_wp == wp && cursor == 0)
        {
            v_keys.push_back(h_keys[offset + i][cursor]);
            v_indices.push_back(cursor);
        }
        while (cursor + 1 < config->kv_restriction)
        {
            cursor++;
            temp_wp = (h_keys[offset + i][cursor] >> 23) & ((1ULL << 25) - 1);
            if (temp_wp == wp)
            {
                v_keys.push_back(h_keys[offset + i][cursor]);
                v_indices.push_back(cursor);
            }
        }
    }
    return ret;
}

// void workbench::load_CTF_keys(uint CTB_id, uint CTF_id)
// {
//     ifstream read_sst;
//     string filename = config->raid_path + to_string(CTF_id % 8) + "/SSTable_" + to_string(CTB_id) + "-" + to_string(CTF_id);
//     read_sst.open(filename, ios::in | ios::binary);
//     // int buffer_size = 2 * 1024 * 1024;
//     // read_sst.rdbuf()->pubsetbuf(new char[buffer_size], buffer_size);
//     assert(read_sst.is_open());
//     ctbs[CTB_id].ctfs[CTF_id].keys = new __uint128_t[ctbs[CTB_id].CTF_capacity[CTF_id]];
//     read_sst.read((char *)ctbs[CTB_id].ctfs[CTF_id].keys, sizeof(__uint128_t) * ctbs[CTB_id].CTF_capacity[CTF_id]);
//     read_sst.close();
// }

// void workbench::load_CTF_keys(uint CTB_id, uint CTF_id) {
//     std::string filename = config->raid_path + std::to_string(CTF_id % 8) + "/SSTable_" + std::to_string(CTB_id) + "-" + std::to_string(CTF_id);
//     FILE* file = fopen(filename.c_str(), "rb");  // 使用 "rb" 模式以二进制读取方式打开文件
//     assert(file != nullptr);
//     // 禁用与 std::ifstream 不同的 libc 缓冲区
//     // int buffer_size = 2 * 1024 * 1024;
//     // setvbuf(file, new char[buffer_size], _IOFBF, buffer_size);
//     size_t keys_size = sizeof(__uint128_t) * ctbs[CTB_id].CTF_capacity[CTF_id];
//     ctbs[CTB_id].ctfs[CTF_id].keys = new __uint128_t[ctbs[CTB_id].CTF_capacity[CTF_id]];
//     size_t read_count = fread((char *)ctbs[CTB_id].ctfs[CTF_id].keys, 1, keys_size, file);
//     assert(read_count == keys_size);
//     fclose(file);
// }

void workbench::load_CTF_keys(uint CTB_id, uint CTF_id)
{
    std::string filename = config->raid_path + std::to_string(CTF_id % 8) + "/SSTable_" + std::to_string(CTB_id) + "-" + std::to_string(CTF_id);
    int fd = open(filename.c_str(), O_RDONLY | O_DIRECT);
    assert(fd != -1);
    size_t keys_size = sizeof(__uint128_t) * ctbs[CTB_id].CTF_capacity[CTF_id];
    // ctbs[CTB_id].ctfs[CTF_id].keys = new __uint128_t[ctbs[CTB_id].CTF_capacity[CTF_id]];
    void *aligned_buffer = nullptr;
    const size_t block_size = 512;
    aligned_buffer = aligned_alloc(block_size, keys_size);
    ctbs[CTB_id].ctfs[CTF_id].keys = (__uint128_t *)aligned_buffer;
    // assert(aligned_buffer != nullptr);
    if (keys_size % 512 == 0)
    {
        read(fd, aligned_buffer, keys_size);
    }
    else
    {
        read(fd, aligned_buffer, keys_size / 512 * 512);
        ctbs[CTB_id].CTF_capacity[CTF_id] = keys_size / 512 * 512 / sizeof(__uint128_t);
        // close(fd);
        // fd = open(filename.c_str(), O_RDONLY);
        // if (lseek(fd, keys_size / 512 * 512, SEEK_SET) != (off_t)-1)
        // {
        //     read(fd, aligned_buffer + keys_size / 512 * 512, keys_size - keys_size / 512 * 512);
        // }
    }
    close(fd);

    // assert(read_count == static_cast<ssize_t>(keys_size));
    // if (aligned_buffer != ctbs[CTB_id].ctfs[CTF_id].keys) {
    //     memcpy(ctbs[CTB_id].ctfs[CTF_id].keys, aligned_buffer, keys_size);
    //     free(aligned_buffer);
    // }
}

// void workbench::load_CTF_keys(uint CTB_id, uint CTF_id)
// {
//     int fd;
//     std::string filename = config->raid_path + std::to_string(CTF_id % 8) + "/SSTable_" + std::to_string(CTB_id) + "-" + std::to_string(CTF_id);
//     // 使用O_DIRECT标志打开文件
//     if ((fd = open(filename.c_str(), O_RDONLY | O_DIRECT)) == -1) {
//         perror("Error opening file for direct I/O");
//         exit(EXIT_FAILURE);
//     }
//     size_t key_count = ctbs[CTB_id].CTF_capacity[CTF_id];
//     size_t total_size = sizeof(__uint128_t) * key_count;
//     // 确保缓冲区是物理页对齐的
//     void* buffer = aligned_alloc(512, total_size);  // 假设512字节对齐足够；根据实际情况调整
//     if (buffer == nullptr) {
//         perror("Failed to allocate aligned memory");
//         close(fd);
//         // exit(EXIT_FAILURE);
//     }
//     // 读取整个文件到缓冲区
//     ssize_t bytesRead = read(fd, buffer, total_size);
//     if (bytesRead != (ssize_t)total_size) {
//         perror("Error reading from file with direct I/O");
//         free(buffer);
//         close(fd);
//         exit(EXIT_FAILURE);
//     }
//     // 将读入的数据复制到ctfs结构中
//     __uint128_t* keys = new __uint128_t[key_count];
//     memcpy(keys, buffer, total_size);
//     ctbs[CTB_id].ctfs[CTF_id].keys = keys;
//     // 清理
//     free(buffer);
//     close(fd);
// }

std::future<void> workbench::load_CTF_keys_async(uint CTB_id, uint CTF_id)
{
    return std::async(std::launch::async, [this, CTB_id, CTF_id]()
                      {
        std::ifstream read_sst;
        string filename = config->raid_path + to_string(CTF_id % 8) + "/SSTable_" + to_string(CTB_id) + "-" + to_string(CTF_id);
        read_sst.open(filename, std::ios::in | std::ios::binary);
        
        // int buffer_size = 2 * 1024 * 1024;
        // read_sst.rdbuf()->pubsetbuf(new char[buffer_size], buffer_size);
        if(!read_sst.is_open()){
            // log("error in %s",filename);
            cout<<"error in" <<filename<<endl;
        }
        assert(read_sst.is_open());
        
        ctbs[CTB_id].ctfs[CTF_id].keys = new __uint128_t[ctbs[CTB_id].CTF_capacity[CTF_id]];
        read_sst.read((char *)ctbs[CTB_id].ctfs[CTF_id].keys, sizeof(__uint128_t) * ctbs[CTB_id].CTF_capacity[CTF_id]);
        
        read_sst.close(); });
}

// void workbench::load_CTF_keys(uint CTB_id, uint CTF_id)
// {
//     ifstream read_sst;
//     string filename = config->raid_path + to_string(CTF_id % 8) + "/SSTable_" + to_string(CTB_id) + "-" + to_string(CTF_id);
//     read_sst.open(filename, ios::in | ios::binary);
//     int fd = open(filename.c_str(), O_RDONLY);
//     __uint128_t *data = (__uint128_t *)mmap(NULL, ctbs[CTB_id].CTF_capacity[CTF_id] * sizeof(__uint128_t), PROT_READ, MAP_PRIVATE, fd, 0);
//     close(fd);
//     ctbs[CTB_id].ctfs[CTF_id].keys = data;
//     // assert(read_sst.is_open());
//     // ctbs[CTB_id].ctfs[CTF_id].keys = new __uint128_t[ctbs[CTB_id].CTF_capacity[CTF_id]];
//     // read_sst.read((char *)ctbs[CTB_id].ctfs[CTF_id].keys, sizeof(__uint128_t) * ctbs[CTB_id].CTF_capacity[CTF_id]);
//     // read_sst.close();
// }

uint workbench::search_time_in_disk(time_query *tq)
{ // not finish
    uint count = 0;
    for (int i = 0; i < ctb_count; i++)
    {
        if (tq->abandon || (ctbs[i].start_time_min < tq->t_end) && (tq->t_start < ctbs[i].end_time_max))
        { // intersect
            if ((tq->t_start < ctbs[i].end_time_min) && (ctbs[i].end_time_max < tq->t_end))
            { // all accepted
                for (int j = 0; j < config->CTF_count; j++)
                {
                    count += ctbs[i].CTF_capacity[j];
                }
            }
            time_query tq_temp;
            tq_temp.t_start = tq->t_start - ctbs[i].start_time_min;
            tq_temp.t_end = tq->t_start - ctbs[i].start_time_min;
            tq_temp.abandon = tq->abandon;
            if (!ctbs[i].ctfs)
            {
                ctbs[i].ctfs = new CTF[config->CTF_count];
            }
            for (int j = 0; j < config->CTF_count; j++)
            {
                if (!ctbs[i].ctfs[j].keys)
                {
                    load_CTF_keys(i, j);
                }
                for (uint q = 0; q < ctbs[i].CTF_capacity[j]; q++)
                {
                    if (tq_temp.check_key_time(ctbs[i].ctfs[j].keys[q]))
                    {
                        count++;
                    }
                }
            }
        }
    }
    return count;
}

bool new_bench::id_search_in_CTB(uint pid, uint CTB_id, time_query *tq)
{
    uint i = CTB_id;
    uint64_t wp = pid;
    time_query tq_temp;
    tq_temp.t_start = tq->t_start - ctbs[i].start_time_min;
    tq_temp.t_end = tq->t_start - ctbs[i].start_time_min;
    tq_temp.abandon = tq->abandon;
    if (ctbs[i].sids[pid] == 0)
    {
        wid_filter_count++;
        return false;
    }
    else if (ctbs[i].sids[pid] == 1)
    {
        // cout << "oid search buffer" << endl;
        uint buffer_find = ctbs[i].o_buffer.search_buffer(pid, &tq_temp, search_multi, search_count, search_multi_pid);
        // search_count.fetch_add(buffer_find, std::memory_order_relaxed);
    }
    else
    {
        wp += ((uint64_t)ctbs[i].sids[pid] << OID_BIT);
        // cout<<"wp: "<<wp<<endl;
    }
    if (!ctbs[i].ctfs)
    {
        // cout<<"new SSTables"<<endl;
        ctbs[i].ctfs = new CTF[config->CTF_count]; // maybe useful later, should not delete after this func , if(!NULL)
    }
    ifstream read_sst;

    // high level binary search
    int find = -1;
    int low = 0;
    int high = config->CTF_count - 1;
    int mid;
    while (low <= high)
    {
        mid = (low + high) / 2;
        // cout << bg_run[i].first_widpid[mid] << endl;
        if (ctbs[i].first_widpid[mid] == wp)
        {
            find = mid;
            break;
        }
        else if (ctbs[i].first_widpid[mid] > wp)
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
        //                cout<<"not find in first_widpid"<<endl;
        //                cout << low << "-" << bg_run[i].first_widpid[low] << endl;
        //                cout << mid << "-" << bg_run[i].first_widpid[mid] << endl;
        //                cout << high << "-" << bg_run[i].first_widpid[high] << endl;
        if (high < 0)
        {
            high = 0;
        }
        if (!ctbs[i].ctfs[high].keys)
        {
            load_CTF_keys(i, high);
        }
        uint target_count = ctbs[i].ctfs[high].search_SSTable(wp, tq, search_multi, ctbs[i].CTF_capacity[high], search_count, search_multi_pid);
        // search_count.fetch_add(target_count, std::memory_order_relaxed);
        delete[] ctbs[i].ctfs[high].keys;
        ctbs[i].ctfs[high].keys = nullptr;
        if (target_count)
        {
            return true;
        }
        else
        {
            id_not_find_count++;
            return false;
        }
    }
    // else  true
    if (!ctbs[i].ctfs[find].keys)
    {
        load_CTF_keys(i, find);
    }
    for (int cursor = 0; cursor < ctbs[i].CTF_capacity[find]; cursor++)
    {
        uint64_t temp_wp = ctbs[i].ctfs[high].keys[cursor] >> (OID_BIT + MBR_BIT + DURATION_BIT + END_BIT);
        if (temp_wp == wp)
        {
            uint old_search_count = search_count.fetch_add(1, std::memory_order_relaxed);
            ;
            if (search_multi)
            {
                search_multi_pid[old_search_count] = get_key_target(ctbs[i].ctfs[high].keys[cursor]);
            }
        }
        else
            break;
    }

    delete[] ctbs[i].ctfs[find].keys;
    ctbs[i].ctfs[find].keys = nullptr;
    return true;
}

bool new_bench::id_search_in_disk(uint pid, time_query *tq)
{
    // cout<<"oid disk search "<<pid<<endl;
    bool ret = false;
    for (int i = 0; i < ctb_count; i++)
    {
        if (tq->abandon || (ctbs[i].start_time_min < tq->t_end) && (tq->t_start < ctbs[i].end_time_max))
        {
            ret |= id_search_in_CTB(pid, i, tq);
        }
    }
    return ret;
}

void workbench::dump_meetings(uint st)
{
    struct timeval start_time = get_cur_time();
    string filename = config->trace_path + "meeting" + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ofstream wf(filename, ios::out | ios::binary | ios::trunc);
    wf.write((char *)active_meeting_count_ps, sizeof(uint) * 100);
    wf.write((char *)h_meetings_ps, sizeof(meeting_unit) * total_meetings_this100s);
    wf.close();
    total_meetings_this100s = 0;
    logt("dumped to %s", start_time, filename.c_str());
}

void workbench::load_meetings(uint st)
{
    log("loading meetings from %d to %d", st, st + 100);
    struct timeval start_time = get_cur_time();
    string filename = config->trace_path + "meeting" + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ifstream in(filename, ios::in | ios::binary);
    in.read((char *)active_meeting_count_ps, sizeof(uint) * 100);
    total_meetings_this100s = 0;
    for (uint i = 0; i < 100; i++)
    {
        total_meetings_this100s += active_meeting_count_ps[i];
    }
    in.read((char *)h_meetings_ps, sizeof(meeting_unit) * total_meetings_this100s);
    in.close();
    logt("loaded %d objects last for 100 seconds start from %d time from %s", start_time, config->num_objects, st, filename.c_str());
}

bool PolygonSearchCallback(short *i, box poly_mbr, void *arg)
{
    vector<pair<short, box>> *ret = (vector<pair<short, box>> *)arg;
    ret->push_back(make_pair(*i, poly_mbr));
    return true;
}

// bool workbench::mbr_search_in_CTB(box b, uint CTB_id, unordered_set<uint> &uni, time_query * tq){
//     uint i = CTB_id;
//     bool ret = false, find = false;
////    box bit_b = make_bit_box(b);
////    uint bit_pos = 0;
////    if(!ctbs[i].ctfs){
////        ctbs[i].ctfs = new CTF[config->CTF_count];
////    }
////    //cout << "in bg_run" << i << endl;
////
////    uint buffer_find = 0;
////    for(uint q = 0; q < ctbs[i].o_buffer.oversize_kv_count; q++){
////        if(ctbs[i].o_buffer.boxes[q].intersect(b)){
////            uni.insert(get_key_oid(ctbs[i].o_buffer.keys[i]));
////            buffer_find++;
////            mbr_find_count++;
////            //cout<<"box find!"<<endl;
////            //ctbs[i].o_buffer.boxes[q].print();
////        }
////    }
////
////    vector<pair<short, box>> intersect_mbrs;
////    ctbs[i].box_rtree->Search(b.low, b.high, PolygonSearchCallback, (void *)&intersect_mbrs);
////    intersect_sst_count += intersect_mbrs.size();
////    for (uint j = 0; j < intersect_mbrs.size(); j++) {
////        uint CTF_id = intersect_mbrs[j].first;
////        find = false;
////        for (uint p = bit_b.low[0]-1; (p <= bit_b.high[0]+1) && (!find); p++) {
////            for (uint q = bit_b.low[1]-1; (q <= bit_b.high[1]+1) && (!find); q++) {
////                bit_pos = xy2d(SID_BIT / 2, p, q);
////                if (ctbs[i].bitmaps[CTF_id * (bit_count / 8) + bit_pos / 8] & (1 << (bit_pos % 8))) {              //mbr intersect bitmap
////                    //cerr << "SSTable_" << CTF_id << "bit_pos" << bit_pos << endl;
////                    find = true;
////                    ret = true;
////                    break;
////                }
////            }
////        }
////        if(find){
////            bit_find_count++;
////            if(!ctbs[i].ctfs[CTF_id].keys){
////                load_CTF_keys(i, CTF_id);
////            }
////            uint this_find = 0;
////            time_query tq_temp;
////            tq_temp.t_start = tq->t_start - ctbs[i].start_time_min;
////            tq_temp.t_end = tq->t_start - ctbs[i].start_time_min;
////            tq_temp.abandon = tq->abandon;
////            box_search_info bs_uint;
////            bs_uint.ctb_id = i;
////            bs_uint.ctf_id = CTF_id;
////            bs_uint.bmap_mbr = &ctbs[i].bitmap_mbrs[CTF_id];
////            bs_uint.tq = tq_temp;
////            //box_search_queue.push_back(bs_uint);
////            //cerr<<this_find<<"finds in sst "<<CTF_id<<endl;
////        }
////    }
//    return ret;
//}

bool new_bench::mbr_search_in_CTB(box b, uint CTB_id, unordered_set<uint> &uni, time_query *tq)
{
    uint i = CTB_id;
    bool ret = false, find = false;
    box bit_b = make_bit_box(b);
    uint bit_pos = 0;
    if (!ctbs[i].ctfs)
    {
        ctbs[i].ctfs = new CTF[config->CTF_count];
    }
    // cout << "in bg_run" << i << endl;

    uint buffer_find = 0;
    for (uint q = 0; q < ctbs[i].o_buffer.oversize_kv_count; q++)
    {
        if (ctbs[i].o_buffer.boxes[q].intersect(b))
        {
            uni.insert(get_key_oid(ctbs[i].o_buffer.keys[i]));
            buffer_find++;
            // cout<<"box find!"<<endl;
            // ctbs[i].o_buffer.boxes[q].print();
        }
    }
    search_count.fetch_add(mbr_find_count, std::memory_order_relaxed);
    vector<pair<short, box>> intersect_mbrs;
    ctbs[i].box_rtree->Search(b.low, b.high, PolygonSearchCallback, (void *)&intersect_mbrs);
    intersect_sst_count += intersect_mbrs.size();
    for (uint j = 0; j < intersect_mbrs.size(); j++)
    {
        uint CTF_id = intersect_mbrs[j].first;
        find = false;
        for (uint p = bit_b.low[0] - 1; (p <= bit_b.high[0] + 1) && (!find); p++)
        {
            for (uint q = bit_b.low[1] - 1; (q <= bit_b.high[1] + 1) && (!find); q++)
            {
                bit_pos = xy2d(SID_BIT / 2, p, q);
                if (ctbs[i].bitmaps[CTF_id * (bit_count / 8) + bit_pos / 8] & (1 << (bit_pos % 8)))
                { // mbr intersect bitmap
                    // cerr << "SSTable_" << CTF_id << "bit_pos" << bit_pos << endl;
                    find = true;
                    ret = true;
                    break;
                }
            }
        }
        if (find)
        {
            bit_find_count++;
            uint this_find = 0;
            time_query tq_temp;
            tq_temp.t_start = tq->t_start - ctbs[i].start_time_min;
            tq_temp.t_end = tq->t_start - ctbs[i].start_time_min;
            tq_temp.abandon = tq->abandon;
            box_search_info bs_uint;
            bs_uint.ctb_id = i;
            bs_uint.ctf_id = CTF_id;
            bs_uint.bmap_mbr = &ctbs[i].bitmap_mbrs[CTF_id];
            bs_uint.tq = tq_temp;
            box_search_queue.push_back(bs_uint);
            // futures.push_back();
            // auto func = [&](workbench *bench, int ctb_id, int ctf_id)
            // { bench->load_CTF_keys(ctb_id, ctf_id); };
            // futures.push_back(std::move(pool.submit(func, this, bs_uint.ctb_id, bs_uint.ctf_id)));
            // cerr<<this_find<<"finds in sst "<<CTF_id<<endl;
        }
    }
    return ret;
}

bool new_bench::mbr_search_in_disk(box b, time_query *tq, uint CTB_id)
{
    uint i = CTB_id;
    // assert(mbr.contain(b));
    // cout << "mbr disk search" << endl;
    unordered_set<uint> uni;
    bool ret = false;
    if (tq->abandon || (ctbs[i].start_time_min < tq->t_end) && (tq->t_start < ctbs[i].end_time_max))
    {
        ret |= mbr_search_in_CTB(b, i, uni, tq);
    }
    mbr_unique_find += uni.size();
    uni.clear();
    return ret;
}

void workbench::dump_meta(const char *path)
{
    struct timeval start_time = get_cur_time();
    string bench_path = string(path) + "workbench";
    ofstream wf(bench_path, ios::out | ios::binary | ios::trunc);
    wf.write((char *)config, sizeof(generator_configuration)); // the config of pipeline is generator_configuration
    cout << "sizeof(*config)" << sizeof(*config) << endl;
    cout << "sizeof(configuration)" << sizeof(configuration) << endl;
    cout << "sizeof(generator_configuration)" << sizeof(generator_configuration) << endl;
    wf.write((char *)this, sizeof(workbench));
    for (int i = 0; i < ctb_count; i++)
    {
        if (ctbs[i].sids)
        {
            string CTB_path = string(path) + "CTB" + to_string(i);
            dump_CTB_meta(CTB_path.c_str(), i);
        }
    }
    wf.close();
    logt("bench meta dumped to %s", start_time, path);
}

void workbench::dump_CTB_meta(const char *path, int i)
{
    struct timeval start_time = get_cur_time();
    ofstream wf(path, ios::out | ios::binary | ios::trunc);
    wf.write((char *)&ctbs[i], sizeof(CTB));
    wf.write((char *)ctbs[i].first_widpid, config->CTF_count * sizeof(uint64_t));
    wf.write((char *)ctbs[i].sids, config->num_objects * sizeof(unsigned short));
    wf.write((char *)ctbs[i].bitmaps, bitmaps_size * sizeof(unsigned char));
    wf.write((char *)ctbs[i].bitmap_mbrs, config->CTF_count * sizeof(box));
    wf.write((char *)ctbs[i].CTF_capacity, config->CTF_count * sizeof(uint));
    wf.write((char *)ctbs[i].o_buffer.keys, ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t));
    wf.write((char *)ctbs[i].o_buffer.boxes, ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box));
    // RTree
    wf.close();
    // logt("CTB meta %d dump to %s",start_time, i, path);

    delete[] ctbs[i].sids;
    ctbs[i].sids = NULL;
}

double bytes_to_MB(size_t bytes)
{
    return bytes / (1024.0 * 1024.0);
}

void workbench::load_CTB_meta(const char *path, int i)
{
    struct timeval start_time = get_cur_time();
    ifstream in(path, ios::in | ios::binary);

    if (!in.is_open())
    {
        std::cerr << "Error opening file: " << path << std::endl;
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
    // RTree
    ctbs[i].box_rtree = new RTree<short *, double, 2, double>(); // size nearly equals to bitmap_mbrs
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

    uint byte_of_CTB_meta = config->CTF_count * sizeof(uint64_t) + config->num_objects * sizeof(unsigned short) + bitmaps_size * sizeof(unsigned char) + config->CTF_count * sizeof(box) * 2 + config->CTF_count * sizeof(uint) + ctbs[i].o_buffer.oversize_kv_count * sizeof(__uint128_t) + ctbs[i].o_buffer.oversize_kv_count * sizeof(f_box);
    cerr << "byte_of_CTB_meta: " << byte_of_CTB_meta << endl;
    for (uint j = 0; j < config->CTF_count; ++j)
    {
        ctbs[i].box_rtree->Insert(ctbs[i].bitmap_mbrs[j].low, ctbs[i].bitmap_mbrs[j].high, new short(j));
    }
    logt("CTB meta %d load from %s", start_time, i, path);
}
