#include "../geometry/Map.h"
#include "../tracing/generator.h"
#include "../tracing/trace.h"

using namespace std;

workbench * load_meta(const char *path) {
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
    bench->ctbs = new CTB[config->big_sorted_run_capacity];
    for(int i = 0; i < bench->ctb_count; i++){
        //CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s",start_time, bench_path.c_str());
    return bench;
}

int main(int argc, char **argv){
    string path = "../data/meta/";
    workbench * bench = load_meta(path.c_str());

    cout << "search begin" <<endl;
    if(true){            // !bench->do_some_search && bench->big_sorted_run_count == 1
        bench->do_some_search = true;

//                for(uint i = 0 ; i < bench->ctbs[1].o_buffer.oversize_kv_count; i++){
//                    bench->search_in_disk( get_key_oid(bench->ctbs[1].o_buffer.keys[i]), 15);
//                }

        uint question_count = 10000;
        bench->wid_filter_count = 0;
        bench->id_find_count = 0;
        uint pid = 100000;
//                string cmd = "sync; sudo sh -c 'echo 1 > /proc/sys/vm/drop_caches'";        //sudo!!!
//                if(system(cmd.c_str())!=0){
//                    fprintf(stderr, "Error when disable buffer cache\n");
//                }
        ofstream q;
        q.open(to_string(bench->config->MemTable_capacity/2)+"search_id.csv", ios::out|ios::binary|ios::trunc);
        q << "question number" << ',' << "time_consume(ms)" << endl;
        for(int i = 0; i < question_count; i++){
            struct timeval disk_search_time = get_cur_time();
//                    uint temp = bench->config->SSTable_count;
//                    bench->config->SSTable_count = bench->merge_sstable_count;
            bench->search_in_disk(pid, 15);
//                    bench->config->SSTable_count = temp;
            pid++;
            double time_consume = get_time_elapsed(disk_search_time);
            //printf("disk_search_time %.2f\n", time_consume);
            q << i << ',' << time_consume << endl;
        }
        q.close();
        cout << "question_count:" << question_count << " id_find_count:" << bench->id_find_count <<" kv_restriction:"<< bench->config->kv_restriction << endl;
        cout << "wid_filter_count:" << bench->wid_filter_count <<"id_not_find_count"<<bench->id_not_find_count<<endl;

//                double mid_x = -87.678503;
//                double mid_y = 41.856803;
//                Point the_mid(mid_x, mid_y);
//                the_mid.print();
        double mid_x[10] = {-87.678503, -87.81683, -87.80959,-87.81004, -87.68706,-87.68616,-87.67892, -87.63235, -87.61381, -87.58352};
        double mid_y[10] = {41.856803, 41.97466, 41.90729, 41.76984, 41.97556, 41.89960, 41.74859, 41.87157, 41.78340, 41.70744};
        double base_edge_length = 0.01;
        for(int i = 0; i < bench->ctb_count; i++){
            bench->load_big_sorted_run(i);
        }
        ofstream p;
        p.open(to_string(bench->config->MemTable_capacity/2)+"search_mbr.csv", ios::out|ios::binary|ios::trunc);        //config->SSTable_count/50
        p << "search area" << ',' << "find_count" << ',' << "unique_find" << ',' << "intersect_sst_count" << ',' << "bit_find_count" << ',' << "time(ms)" << endl;
        for(uint j = 0; j < 10; j++){
            for(int i = 0; i < 10 ; i++){
                //cout << fixed << setprecision(6) << mid_x - edge_length/2 <<","<<mid_y - edge_length/2 <<","<<mid_x + edge_length/2 <<","<<mid_y + edge_length/2 <<endl;
                double edge_length = base_edge_length * (i + 1);
                box search_area(mid_x[j] - edge_length/2, mid_y[j] - edge_length/2, mid_x[j] + edge_length/2, mid_y[j] + edge_length/2);
                search_area.print();
                struct timeval area_search_time = get_cur_time();
//                    uint temp = bench->config->SSTable_count;
//                    bench->config->SSTable_count = bench->merge_sstable_count;
                bench->mbr_search_in_disk(search_area, 5);
//                    bench->config->SSTable_count = temp;
                double time_consume = get_time_elapsed(area_search_time);
                //printf("area_search_time %.2f\n", time_consume);
                p << edge_length*edge_length << ',' << bench->mbr_find_count << ',' << bench->mbr_unique_find << ','
                  << bench->intersect_sst_count <<',' << bench->bit_find_count << ',' << time_consume << endl;
                bench->mbr_find_count = 0;
                bench->mbr_unique_find = 0;
                bench->intersect_sst_count = 0;
                bench->bit_find_count = 0;
            }
            p << endl;
        }
        p.close();
        //return;
    }
}
