#include "../geometry/Map.h"
#include "../tracing/generator.h"
#include "../tracing/trace.h"

using namespace std;

workbench * dump_load_meta(const char *path) {
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
    for(int i = 10; i < 11; i++){
        //CTB temp_ctb;
        string CTB_path = string(path) + "CTB" + to_string(i);
        bench->load_CTB_meta(CTB_path.c_str(), i);
    }
    logt("bench meta load from %s",start_time, bench_path.c_str());
    return bench;
}

struct dump_args {
    string path;
    __uint128_t * keys;
    uint SIZE;
};

void *parallel_dump(void *arg){
    dump_args *pargs = (dump_args *)arg;
    ofstream SSTable_of;
    SSTable_of.open(pargs->path.c_str() , ios::out|ios::binary|ios::trunc);
    //cout << pargs->path << endl;
    assert(SSTable_of.is_open());
    SSTable_of.write((char *)pargs->keys, pargs->SIZE);
    SSTable_of.flush();
    SSTable_of.close();
    return NULL;
}


int main(int argc, char **argv){
    string path = "../data/meta/";
    workbench * bench = dump_load_meta(path.c_str());
    new_bench * nb = new new_bench(bench->config);
    memcpy(nb, bench, sizeof(workbench));
    cout << nb->ctb_count << endl;

    for (int i = 0; i < nb->ctb_count; i++)
    {
        
    }
    //ctb_id can be 10, old_big == ctb_id
    uint sst_count = 0;
    uint total_index = 0;
    for(sst_count=0; sst_count<bench->config->CTF_count; sst_count++){
        std::string path = bench->config->raid_path + to_string(sst_count%8) + "/test_SSTable_"+to_string(old_big)+"-"+to_string(sst_count);
        int sz = sizeof(__uint128_t)*bench->h_CTF_capacity[offset][sst_count];
        char* keys = bench->h_keys[offset] + total_index;
        
        ofstream SSTable_of;
        SSTable_of.open(path , ios::out|ios::binary|ios::trunc);
        assert(SSTable_of.is_open());
        SSTable_of.write((char *)keys, sz);
        SSTable_of.flush();
        SSTable_of.close();
        
        total_index += bench->h_CTF_capacity[offset][sst_count];
    }
    return 0;
}
