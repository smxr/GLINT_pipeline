#include "../geometry/Map.h"
#include "../tracing/generator.h"
#include "../tracing/trace.h"
#include <sys/mman.h> // mmap, PROT_READ, MAP_PRIVATE
#include <fcntl.h>    // open, O_RDONLY
#include <unistd.h>   // close
#include "../util/ThreadPool.h"
#include <sys/shm.h>
#include <sys/wait.h>

using namespace std;

void clear_cache()
{
    string cmd = "sync; echo \"koiwithme\" | sudo -S sh -c 'echo 1 > /proc/sys/vm/drop_caches'"; // sudo!!!
    if (system(cmd.c_str()) != 0)
    {
        fprintf(stderr, "Error when disable buffer cache\n");
    }
    cout << "clear_cache" << endl;
}

workbench *load_meta(const char *path)
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
    bench->ctbs = new CTB[config->big_sorted_run_capacity];
    for (int i = 0; i < bench->ctb_count; i++)
    {
        // CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s", start_time, bench_path.c_str());
    return bench;
}

workbench *C_load_meta(const char *path)
{
    log("loading compaction meta from %s", path);
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
    bench->ctbs = new CTB[20];
    for (int i = 0; i < 5; i++)
    {
        // CTB temp_ctb;
        string CTB_path = string(path) + "C_CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s", start_time, bench_path.c_str());
    return bench;
}

void *id_search_unit(void *arg)
{
    query_context *ctx = (query_context *)arg;
    new_bench *bench = (new_bench *)ctx->target[0];
    //    uint * pid = (uint *)ctx->target[1];
    //    uint * j = (uint *)ctx->target[2];
    //    time_query * tq = (time_query *)ctx->target[3];
    bench->id_search_in_CTB(*(uint *)ctx->target[1], *(uint *)ctx->target[2], (time_query *)ctx->target[3]);
    return NULL;
}

// void single_experiment_twice(new_bench * bench){
//     bench->search_multi_pid = new uint[1000000];
//     bench->clear_all_keys();
//     clear_cache();
//     time_query tq;
//     tq.abandon = true;
//     ofstream q;
//     q.open("ex_search_id.csv", ios::out|ios::binary|ios::trunc);
//     q << "question number" << ',' << "time_consume(ms)" << ',' << "find_id_count" << ',' << "wid_filter_count" << endl;
//     for(int i = 0; i < 1; i++){
//         struct timeval prepare_start = get_cur_time();
//         bench->search_count = 0;
//         bench->wid_filter_count = 0;
//         uint pid = 10000;
//         bench->search_multi = true;
//         uint the_min = min((uint)1215, bench->ctb_count);
//         pthread_t threads[the_min];
//         query_context * tctx = new query_context[the_min];
//         for(uint j = 0; j < the_min; j++){
//             tctx[j].target[0] = (void *)bench;
//             tctx[j].target[1] = (void *)&pid;
//             tctx[j].target[2] = (void *)new int(j);
//             tctx[j].target[3] = (void *)&tq;
//             pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
//             //bench->id_search_in_CTB(pid, j, &tq);
//         }
//         delete[] tctx;
//         tctx = nullptr;
//         double prepare_consume = get_time_elapsed(prepare_start, true);
//         bench->search_multi = false;
//         q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
//     }
//     uint search_length = bench->search_count;
//     for(int i = 0; i < search_length; i++){
////        bench->clear_all_keys();
////        clear_cache();
//        struct timeval prepare_start = get_cur_time();
//        bench->search_count = 0;
//        bench->wid_filter_count = 0;
//        uint pid = bench->search_multi_pid[i];
//        bench->search_multi = false;
//        uint the_min = min((uint)1215, bench->ctb_count);
//        pthread_t threads[the_min];
//        query_context * tctx = new query_context[the_min];
//        for(uint j = 0; j < the_min; j++){
//            tctx[j].target[0] = (void *)bench;
//            tctx[j].target[1] = (void *)&pid;
//            tctx[j].target[2] = (void *)new int(j);
//            tctx[j].target[3] = (void *)&tq;
//            pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
//            //bench->id_search_in_CTB(pid, j, &tq);
//        }
//        for(int j = 0; j < the_min; j++ ){
//            void *status;
//            pthread_join(threads[j], &status);
//        }
//        delete[] tctx;
//        tctx = nullptr;
//        double prepare_consume = get_time_elapsed(prepare_start, true);
//        bench->search_multi = false;
//        q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
//    }
//    q.close();
//    delete[] bench->search_multi_pid;
//};

void experiment_twice(new_bench *bench)
{
    bench->search_multi_pid = new uint[1000000];
    bench->clear_all_keys();
    clear_cache();
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_id.csv", ios::out | ios::binary | ios::trunc);
    q << "question number" << ',' << "time_consume(ms)" << ',' << "find_id_count" << ',' << "wid_filter_count" << endl;
    for (int i = 0; i < 1; i++)
    {
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->wid_filter_count = 0;
        uint pid = 10000;
        bench->search_multi = true;
        uint the_min = min((uint)1215, bench->ctb_count);
        pthread_t threads[the_min];
        query_context *tctx = new query_context[the_min];
        for (uint j = 0; j < the_min; j++)
        {
            tctx[j].target[0] = (void *)bench;
            tctx[j].target[1] = (void *)&pid;
            tctx[j].target[2] = (void *)new int(j);
            tctx[j].target[3] = (void *)&tq;
            pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
            // bench->id_search_in_CTB(pid, j, &tq);
        }
        for (int j = 0; j < the_min; j++)
        {
            void *status;
            pthread_join(threads[j], &status);
        }
        delete[] tctx;
        tctx = nullptr;
        double prepare_consume = get_time_elapsed(prepare_start, true);
        bench->search_multi = false;
        q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
    }
    uint search_length = bench->search_count;
    for (int i = 0; i < search_length; i++)
    {
        //        bench->clear_all_keys();
        //        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->wid_filter_count = 0;
        uint pid = bench->search_multi_pid[i];
        bench->search_multi = false;
        uint the_min = min((uint)1215, bench->ctb_count);
        pthread_t threads[the_min];
        query_context *tctx = new query_context[the_min];
        for (uint j = 0; j < the_min; j++)
        {
            tctx[j].target[0] = (void *)bench;
            tctx[j].target[1] = (void *)&pid;
            tctx[j].target[2] = (void *)new int(j);
            tctx[j].target[3] = (void *)&tq;
            pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
            // bench->id_search_in_CTB(pid, j, &tq);
        }
        for (int j = 0; j < the_min; j++)
        {
            void *status;
            pthread_join(threads[j], &status);
        }
        delete[] tctx;
        tctx = nullptr;
        double prepare_consume = get_time_elapsed(prepare_start, true);
        bench->search_multi = false;
        q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
    }
    q.close();
    delete[] bench->search_multi_pid;
}

void exp4_search_oid_single(new_bench *bench)
{
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_id.csv", ios::out | ios::binary | ios::trunc);
    q << "question number" << ',' << "time_consume(ms)" << ',' << "find_id_count" << ',' << "wid_filter_count" << endl;
    for (int i = 0; i < 100; i++)
    {
        bench->clear_all_keys();
        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->wid_filter_count = 0;
        uint pid = get_rand_number(bench->config->num_objects);
        bench->search_multi = false;
        uint the_min = min((uint)1215, bench->ctb_count);
        // pthread_t threads[the_min];
        query_context *tctx = new query_context[the_min];
        for (uint j = 0; j < the_min; j++)
        {
            tctx[j].target[0] = (void *)bench;
            tctx[j].target[1] = (void *)&pid;
            tctx[j].target[2] = (void *)new int(j);
            tctx[j].target[3] = (void *)&tq;
            id_search_unit((void *)&tctx[j]);
            // pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
            // bench->id_search_in_CTB(pid, j, &tq);
        }
        //        for(int j = 0; j < the_min; j++ ){
        //            void *status;
        //            pthread_join(threads[j], &status);
        //        }
        delete[] tctx;
        tctx = nullptr;
        double prepare_consume = get_time_elapsed(prepare_start, true);
        bench->search_multi = false;
        q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
    }
    q.close();
}

void experiment_search_oid(new_bench *bench)
{
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_id.csv", ios::out | ios::binary | ios::trunc);
    q << "question number" << ',' << "time_consume(ms)" << ',' << "find_id_count" << ',' << "wid_filter_count" << endl;
    for (int i = 0; i < 100; i++)
    {
        bench->clear_all_keys();
        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->wid_filter_count = 0;
        uint pid = get_rand_number(bench->config->num_objects);
        bench->search_multi = false;
        uint the_min = min((uint)1215, bench->ctb_count);
        pthread_t threads[the_min];
        query_context *tctx = new query_context[the_min];
        for (uint j = 0; j < the_min; j++)
        {
            tctx[j].target[0] = (void *)bench;
            tctx[j].target[1] = (void *)&pid;
            tctx[j].target[2] = (void *)new int(j);
            tctx[j].target[3] = (void *)&tq;
            pthread_create(&threads[j], NULL, id_search_unit, (void *)&tctx[j]);
            // bench->id_search_in_CTB(pid, j, &tq);
        }
        for (int j = 0; j < the_min; j++)
        {
            void *status;
            pthread_join(threads[j], &status);
        }
        delete[] tctx;
        tctx = nullptr;
        double prepare_consume = get_time_elapsed(prepare_start, true);
        bench->search_multi = false;
        q << pid << ',' << prepare_consume << ',' << bench->search_count << ',' << bench->wid_filter_count << endl;
    }
    q.close();
}

void *box_search_unit(void *arg)
{
    query_context *ctx = (query_context *)arg;
    new_bench *bench = (new_bench *)ctx->target[0];
    box *b = (box *)ctx->target[1];
    uint mbr_find_count = 0;
    while (true)
    {
        size_t start = 0;
        size_t end = 0;
        if (!ctx->next_batch(start, end))
        {
            break;
        }
        for (int obj = start; obj < end; obj++)
        {
            box_search_info info = bench->box_search_queue[obj];
            if (!bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys)
            {
                bench->load_CTF_keys(info.ctb_id, info.ctf_id);
            }
            for (uint q = 0; q < bench->ctbs[info.ctb_id].CTF_capacity[info.ctf_id]; q++)
            {
                __uint128_t temp_key = bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys[q];
                if (info.tq.abandon || info.tq.check_key_time(temp_key))
                {
                    uint pid = get_key_oid(temp_key);
                    box key_box;
                    parse_mbr(temp_key, key_box, *info.bmap_mbr);
                    if (b->intersect(key_box))
                    {
                        mbr_find_count++;
                        // cout<<"box find!"<<endl;
                        // key_box.print();
                    }
                }
            }
            delete[] bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys;
            bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys = nullptr;
        }
    }
    bench->search_count.fetch_add(mbr_find_count, std::memory_order_relaxed);
    return NULL;
}

void exp4_search_box_single(new_bench *bench)
{
    bench->box_search_queue.reserve(bench->ctb_count * bench->config->CTF_count / 20);
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_box.csv", ios::out | ios::binary | ios::trunc);
    if (!q.is_open())
    {
        log("%s cannot be opened", "ex_search_box.csv");
        exit(0);
    }
    q << "search area" << ',' << "find_count" << ',' << "multi_thread_consume" << ',' << "intersect_mbr_count" << ',' << "bitmap_find_count" << ',' << "prepare_time(ms)" << endl;
    for (int i = 0; i < 100; i++)
    {
        bench->clear_all_keys();
        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->mbr_find_count = 0;
        bench->intersect_sst_count = 0;
        bench->bit_find_count = 0;
        double edge_length = 0.01;
        Point mid;
        mid.x = -87.9 + 0.4 * get_rand_double();
        mid.y = 41.6 + 0.4 * get_rand_double();
        box search_area(mid.x - edge_length / 2, mid.y - edge_length / 2, mid.x + edge_length / 2, mid.y + edge_length / 2);
        search_area.print();
        for (uint j = 0; j < min((uint)1215, bench->ctb_count); j++)
        {
            bench->mbr_search_in_disk(search_area, &tq, j);
        }
        cout << "before multi" << endl;
        double prepare_consume = get_time_elapsed(prepare_start, true);
        struct timeval multi_thread_start = get_cur_time();
        pthread_t threads[1];
        query_context tctx;
        tctx.target[0] = (void *)bench;
        tctx.target[1] = (void *)&search_area;
        tctx.num_units = bench->box_search_queue.size();
        tctx.report_gap = 1;
        tctx.num_batchs = 1;
        for (int j = 0; j < 1; j++)
        {
            pthread_create(&threads[j], NULL, box_search_unit, (void *)&tctx);
        }
        for (int j = 0; j < 1; j++)
        {
            void *status;
            pthread_join(threads[j], &status);
        }
        double multi_thread_consume = get_time_elapsed(multi_thread_start, true);
        cout << "multi_thread_consume" << multi_thread_consume << endl;
        q << edge_length * edge_length << ',' << bench->search_count << ',' << multi_thread_consume << ','
          << bench->intersect_sst_count << ',' << bench->bit_find_count << ',' << prepare_consume << endl;
        bench->box_search_queue.clear();
        bench->search_count = 0;
    }
    q.close();
}

void experiment_search_box(new_bench *bench)
{
    bench->box_search_queue.reserve(bench->ctb_count * bench->config->CTF_count / 20);
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_box.csv", ios::out | ios::binary | ios::trunc);
    q << "search area" << ',' << "find_count" << ',' << "multi_thread_consume" << ',' << "intersect_mbr_count" << ',' << "bitmap_find_count" << ',' << "prepare_time(ms)" << endl;
    for (int i = 0; i < 10; i++)
    {
        bench->clear_all_keys();
        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->mbr_find_count = 0;
        bench->intersect_sst_count = 0;
        bench->bit_find_count = 0;
        double edge_length = 0.01;
        Point mid;
        mid.x = -87.9 + 0.2;
        mid.y = 41.6 + 0.2;
        box search_area(mid.x - edge_length / 2, mid.y - edge_length / 2, mid.x + edge_length / 2, mid.y + edge_length / 2);
        search_area.print();
        for (uint j = 0; j < min((uint)1215, bench->ctb_count); j++)
        {
            bench->mbr_search_in_disk(search_area, &tq, j);
        }
        double prepare_consume = get_time_elapsed(prepare_start, true);
        struct timeval multi_thread_start = get_cur_time();
        pthread_t threads[bench->config->num_threads];
        query_context tctx;
        tctx.target[0] = (void *)bench;
        tctx.target[1] = (void *)&search_area;
        tctx.num_units = bench->box_search_queue.size();
        tctx.report_gap = 1;
        tctx.num_batchs = 10000;
        for (int j = 0; j < bench->config->num_threads; j++)
        {
            pthread_create(&threads[j], NULL, box_search_unit, (void *)&tctx);
        }
        for (int j = 0; j < bench->config->num_threads; j++)
        {
            void *status;
            pthread_join(threads[j], &status);
        }
        double multi_thread_consume = get_time_elapsed(multi_thread_start, true);
        cout << "multi_thread_consume" << multi_thread_consume << endl;
        q << edge_length * edge_length << ',' << bench->search_count << ',' << multi_thread_consume << ','
          << bench->intersect_sst_count << ',' << bench->bit_find_count << ',' << prepare_consume << endl;
        bench->box_search_queue.clear();
        bench->search_count = 0;
    }
    q.close();
}

void experiment_search_time(workbench *bench)
{
    time_query tq;
    tq.abandon = false;
    tq.t_start = 3600;
    tq.t_end = 4200;
    bench->clear_all_keys();
    clear_cache();
    struct timeval disk_search_time = get_cur_time();
    uint find_count = bench->search_time_in_disk(&tq);
    double time_consume = get_time_elapsed(disk_search_time);
    cout << "find_count" << find_count << "time_consume" << time_consume << endl;
}

void query_search_id(workbench *bench)
{
    //    uint question_count = 10000;
    //    bench->wid_filter_count = 0;
    //    bench->id_find_count = 0;
    //    uint pid = 100000;
    //    ofstream q;
    //    q.open(to_string(bench->config->MemTable_capacity/2)+"search_id.csv", ios::out|ios::binary|ios::trunc);
    //    q << "question number" << ',' << "time_consume(ms)" << endl;
    //    for(int i = 0; i < question_count; i++){
    //        struct timeval disk_search_time = get_cur_time();
    //        bench->id_search_in_disk(pid, 15);
    //        for(int j = 0; j < bench->ctb_count; j++){
    //            bench->id_search_in_CTB(pid, j, );
    //        }
    //        pid++;
    //        double time_consume = get_time_elapsed(disk_search_time);
    //        //printf("disk_search_time %.2f\n", time_consume);
    //        q << i << ',' << time_consume << endl;
    //    }
    //    q.close();
    //    cout << "question_count:" << question_count << " id_find_count:" << bench->id_find_count <<" kv_restriction:"<< bench->config->kv_restriction << endl;
    //    cout << "wid_filter_count:" << bench->wid_filter_count << "id_not_find_count" << bench->id_not_find_count << endl;
}

void query_search_box(workbench *bench)
{
    //    //                double mid_x = -87.678503;
    ////                double mid_y = 41.856803;
    ////                Point the_mid(mid_x, mid_y);
    ////                the_mid.print();
    //    double mid_x[10] = {-87.678503, -87.81683, -87.80959,-87.81004, -87.68706,-87.68616,-87.67892, -87.63235, -87.61381, -87.58352};
    //    double mid_y[10] = {41.856803, 41.97466, 41.90729, 41.76984, 41.97556, 41.89960, 41.74859, 41.87157, 41.78340, 41.70744};
    //    double base_edge_length = 0.01;
    ////        for(int i = 0; i < bench->ctb_count; i++){
    ////            bench->load_big_sorted_run(i);
    ////        }
    //    ofstream p;
    //    p.open(to_string(bench->config->MemTable_capacity/2)+"search_mbr.csv", ios::out|ios::binary|ios::trunc);        //config->SSTable_count/50
    //    p << "search area" << ',' << "find_count" << ',' << "unique_find" << ',' << "intersect_sst_count" << ',' << "bit_find_count" << ',' << "time(ms)" << endl;
    //    for(uint j = 0; j < 10; j++){
    //        for(int i = 0; i < 10 ; i++){
    //            //cout << fixed << setprecision(6) << mid_x - edge_length/2 <<","<<mid_y - edge_length/2 <<","<<mid_x + edge_length/2 <<","<<mid_y + edge_length/2 <<endl;
    //            double edge_length = base_edge_length * (i + 1);
    //            box search_area(mid_x[j] - edge_length/2, mid_y[j] - edge_length/2, mid_x[j] + edge_length/2, mid_y[j] + edge_length/2);
    //            search_area.print();
    //            struct timeval area_search_time = get_cur_time();
    ////                    uint temp = bench->config->SSTable_count;
    ////                    bench->config->SSTable_count = bench->merge_sstable_count;
    //            bench->mbr_search_in_disk(search_area, 5);
    ////                    bench->config->SSTable_count = temp;
    //            double time_consume = get_time_elapsed(area_search_time);
    //            //printf("area_search_time %.2f\n", time_consume);
    //            p << edge_length*edge_length << ',' << bench->mbr_find_count << ',' << bench->mbr_unique_find << ','
    //              << bench->intersect_sst_count <<',' << bench->bit_find_count << ',' << time_consume << endl;
    //            bench->mbr_find_count = 0;
    //            bench->mbr_unique_find = 0;
    //            bench->intersect_sst_count = 0;
    //            bench->bit_find_count = 0;
    //        }
    //        p << endl;
    //    }
    //    p.close();
}

// new_bench* create_shared_bench(int shmid, size_t size) {
//     void* shared_memory = shmat(shmid, nullptr, 0);
//     new_bench* bench = new (shared_memory) new_bench();
//     // 初始化Bench对象，例如填充box_search_queue
//     return bench;
// }

void experiment_box_openmp(new_bench *bench)
{
    bench->box_search_queue.reserve(bench->ctb_count * bench->config->CTF_count / 20);
    bench->futures.reserve(bench->ctb_count * bench->config->CTF_count / 20);
    time_query tq;
    tq.abandon = true;
    ofstream q;
    q.open("ex_search_box_omp.csv", ios::out | ios::binary | ios::trunc);
    q << "search area" << ',' << "find_count" << ',' << "multi_thread_consume" << ',' << "intersect_mbr_count" << ',' << "bitmap_find_count" << ',' << "prepare_time(ms)" << endl;
    for (int i = 0; i < 1; i++)
    {
        bench->clear_all_keys();
        clear_cache();
        struct timeval prepare_start = get_cur_time();
        bench->search_count = 0;
        bench->mbr_find_count = 0;
        bench->intersect_sst_count = 0;
        bench->bit_find_count = 0;
        double edge_length = 0.01;

        Point mid;
        mid.x = -87.9 + 0.2;
        mid.y = 41.6 + 0.2;
        box search_area(mid.x - edge_length / 2, mid.y - edge_length / 2, mid.x + edge_length / 2, mid.y + edge_length / 2);
        search_area.print();
        for (uint j = 0; j < min((uint)1215, bench->ctb_count); j++)
        {
            bench->mbr_search_in_disk(search_area, &tq, j);
        }
        double prepare_consume = get_time_elapsed(prepare_start, true);
        struct timeval multi_thread_start = get_cur_time();

        // for (auto &fut : bench->futures)
        // {
        //     fut.get();
        // }
        struct timeval bg_start = get_cur_time();
        double load_time = get_time_elapsed(bg_start, true);
        fprintf(stdout, "\tload_time:\t%.2f\n", load_time);
// #pragma omp parallel for num_threads(bench->config->num_threads)
#pragma omp parallel for num_threads(128) //schedule(dynamic)
        for (int j = 0; j < bench->box_search_queue.size(); j++)
        {
            auto &info = bench->box_search_queue[j];
            uint mbr_find_count = 0;
            if (!bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys)
            {
                bench->load_CTF_keys(info.ctb_id, info.ctf_id);
            }

            // bench->futures[j].get();
            for (uint q = 0; q < bench->ctbs[info.ctb_id].CTF_capacity[info.ctf_id]; q++)
            {
                __uint128_t temp_key = bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys[q];
                if (info.tq.abandon || info.tq.check_key_time(temp_key))
                {
                    uint pid = get_key_oid(temp_key);
                    box key_box;
                    parse_mbr(temp_key, key_box, *info.bmap_mbr);
                    if (search_area.intersect(key_box))
                    {
                        mbr_find_count++;
                        // cout<<"box find!"<<endl;
                        // key_box.print();
                    }
                }
            }
            delete[] bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys;
            // munmap(bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys, bench->ctbs[info.ctb_id].CTF_capacity[info.ctf_id] * sizeof(__uint128_t));
            bench->ctbs[info.ctb_id].ctfs[info.ctf_id].keys = nullptr;
            bench->search_count.fetch_add(mbr_find_count, std::memory_order_relaxed);
        }
        double calculate_time = get_time_elapsed(bg_start, true);
        fprintf(stdout, "\tsearch_time:\t%.2f\n", calculate_time);
        double multi_thread_consume = get_time_elapsed(multi_thread_start, true);
        q << edge_length * edge_length << ',' << bench->search_count << ',' << multi_thread_consume << ','
          << bench->intersect_sst_count << ',' << bench->bit_find_count << ',' << prepare_consume << endl;
        bench->box_search_queue.clear();
    }
    q.close();
}

int main(int argc, char **argv)
{
    clear_cache();
    string path = "/data2/xiang/glint_new_pipeline/data/meta/";
    // workbench * bench = C_load_meta(path.c_str());
    workbench *bench = load_meta(path.c_str());
    new_bench *nb = new new_bench(bench->config);
    memcpy(nb, bench, sizeof(workbench));
    cout << nb->ctb_count << endl;
    // nb->ctb_count = 10;
    // nb->ctbs[0].ctfs = NULL;
    // nb->ctbs[1].ctfs = NULL;
    // nb->ctbs[2].ctfs = NULL;
    // nb->ctbs[3].ctfs = NULL;
    // nb->ctbs[4].ctfs = NULL;
    // nb->ctbs[5].ctfs = NULL;
    // nb->ctbs[6].ctfs = NULL;
    // nb->ctbs[7].ctfs = NULL;
    // nb->ctbs[8].ctfs = NULL;
    // nb->ctbs[9].ctfs = NULL;
    cout << "search begin" << endl;

    //        for(int i = 0; i < bench->ctb_count; i++){
    //            bench->load_big_sorted_run(i);
    //        }
    //        fprintf(stderr,"\ttotal load keys:\t%.2f\n", bench->pro.load_keys_time);

    clear_cache();
    nb->clear_all_keys();
    // experiment_twice(nb);
    // exp4_search_oid_single(nb);
    // experiment_search_oid(nb);
    // exp4_search_box_single(nb);
    // experiment_search_box(nb);
    experiment_box_openmp(nb);
    // experiment_search_time(bench);
    // query_search_id(bench);
    // query_search_box(bench);

    return 0;
}
