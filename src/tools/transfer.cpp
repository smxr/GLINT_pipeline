//#include "../geometry/Map.h"
//#include "../tracing/generator.h"
//#include "../tracing/trace.h"
#include "../tracing/workbench.h"

using namespace std;

old_workbench * old_load_meta(const char *path, uint max_ctb) {
    log("loading meta from %s", path);
    string bench_path = string(path) + "workbench";
    struct timeval start_time = get_cur_time();
    ifstream in(bench_path, ios::in | ios::binary);
    if(!in.is_open()){
        log("%s cannot be opened",bench_path.c_str());
        exit(0);
    }
    generator_configuration * config = new generator_configuration();
    old_workbench * bench = new old_workbench();
    bench->config = config;
    in.read((char *)config, sizeof(generator_configuration));               //also read meta
    char new_raid[24] = "/data3/raid0_num";
    memcpy(config->raid_path, new_raid, sizeof(config->raid_path));
    //std::copy(new_raid, new_raid + 24, config->raid_path);
    in.read((char *)bench, sizeof(old_workbench));      //bench->config = NULL
    bench->config = config;
    bench->ctbs = new CTB[config->big_sorted_run_capacity];
#pragma omp parallel for num_threads(bench->config->num_threads)
    for(int i = 0; i < min(max_ctb, bench->ctb_count); i++){
        //CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->old_load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s",start_time, bench_path.c_str());
    return bench;
}

workbench * bench_transfer(old_workbench * old_bench){
    workbench * new_bench = new workbench(old_bench->config);
    new_bench->mbr = old_bench->mbr;
    new_bench->bit_count = old_bench->bit_count;
    new_bench->bitmaps_size = old_bench->bitmaps_size;
    new_bench->ctbs = old_bench->ctbs;
    new_bench->ctb_count = old_bench->ctb_count;
    return new_bench;
}

int main(){
    string path = "../data/meta/";
    //workbench * bench = C_load_meta(path.c_str());
    uint max_ctb = 1215;
    old_workbench * ob = old_load_meta(path.c_str(), max_ctb);
    workbench * bench = bench_transfer(ob);
//    for(uint i = 0; i < max_ctb; i++){
//        bench->load_big_sorted_run(i);
//    }
    struct timeval start_time = get_cur_time();
    bench->make_new_ctf_with_old_ctb(max_ctb);
    logt("make_new_ctf_with_old_ctb ",start_time);
    cout << "search begin" << endl;
    bench->dump_meta(bench->config->CTB_meta_path);
    return 0;
}


