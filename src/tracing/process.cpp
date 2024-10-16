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
	loadMeta(config->trace_path.c_str());
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
	if(owned_trace){
		free(trace);
	}
	if(part){
		delete part;
	}
	if(bench){
		delete bench;
	}
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
	wf.write((char *)trace, sizeof(Point)*num_points);
	wf.close();
	logt("dumped to %s",start_time,path);
}

void tracer::loadMeta(const char *path) {

	uint true_num_objects;
	uint true_duration;
	ifstream in(path, ios::in | ios::binary);
	if(!in.is_open()){
		log("%s cannot be opened",path);
		exit(0);
	}
	in.read((char *)&true_num_objects, sizeof(true_num_objects));
	in.read((char *)&true_duration, sizeof(true_duration));
	log("%d objects last for %d seconds in file",true_num_objects,true_duration);
	in.read((char *)&mbr, sizeof(mbr));
	mbr.to_squre(true);
	mbr.print();
	assert((size_t)config->num_objects*(config->start_time+config->duration)<=(size_t)true_num_objects*true_duration);
	//assert(config->num_objects<=true_num_objects);
	assert(config->start_time+config->duration<=true_duration);
	in.close();
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
	in.read((char *)&true_num_objects, sizeof(true_num_objects));
	in.read((char *)&true_duration, sizeof(true_duration));
	in.read((char *)&mbr, sizeof(mbr));

	assert((size_t)config->num_objects*(st+duration)<=(size_t)true_num_objects*true_duration);
	//assert(config->num_objects<=true_num_objects);
	assert(st+duration<=true_duration);

	in.seekg(st*true_num_objects*sizeof(Point), ios_base::cur);
	if(!trace){
		trace = (Point *)malloc(min((uint)100,config->duration)*config->num_objects*sizeof(Point));
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
	owned_trace = true;
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

void *sst_dump(void *arg){
    cout<<"step into the sst_dump"<<endl;
    workbench *bench = (workbench *)arg;
    cout<<"cur_time: "<<bench->cur_time<<endl;
    uint offset = 0;
    assert(bench->config->MemTable_capacity%2==0);
    uint old_big = bench->big_sorted_run_count;                 //atomic add
    cout<<"old big_sorted_run_count: "<<old_big<<endl;
    bench->big_sorted_run_count++;
    if(old_big%2==1){
        offset = bench->config->MemTable_capacity/2;
    }
    bench->bg_run[old_big].timestamp_min = bench->end_time_min;
    bench->bg_run[old_big].timestamp_max = bench->end_time_max;
    bench->end_time_min = bench->end_time_max;              //new min = old max
    bench->bg_run[old_big].first_pid = new uint[bench->bg_run[old_big].SSTable_count];

    //merge sort
    ofstream SSTable_of;
    //bool of_open = false;
    uint kv_count = 0;
    uint sst_count = 0;
    cout<<"sst_capacity:"<<bench->SSTable_kv_capacity<<endl;
    key_value *temp_kvs = new key_value[bench->SSTable_kv_capacity];
    uint *key_index = new uint[bench->config->MemTable_capacity/2]{0};
    int finish = 0;
    uint64_t temp_key;
    uint taken_id = 0;
    struct timeval bg_start = get_cur_time();
    while(finish<bench->config->MemTable_capacity/2){
        if(kv_count==0){
            SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-"+to_string(sst_count), ios::out | ios::trunc);
            assert(SSTable_of.is_open());
            bench->pro.bg_open_time += get_time_elapsed(bg_start,true);
        }
        finish = 0;
        temp_key = UINT64_MAX;
        taken_id = 0;
        for(int i=0;i<bench->config->MemTable_capacity/2; i++){
            if(key_index[i]>= bench->config->kv_restriction){              //empty kv
                finish++;
                continue;
            }
            if( temp_key > bench->h_keys[offset+i][key_index[i]] ){
                temp_key = bench->h_keys[offset+i][key_index[i]];
                taken_id = i;
            }
        }
        if(finish<bench->config->MemTable_capacity/2){
            temp_kvs[kv_count].key = temp_key;
            temp_kvs[kv_count].value = bench->h_values[offset + taken_id][key_index[taken_id]];     //box
            if(kv_count==0){
                bench->bg_run[old_big].first_pid[sst_count] = temp_key >> 39;
            }
            bench->h_keys[offset + taken_id][key_index[taken_id]] = 0;                                     //init
            key_index[taken_id]++;                                                                  // one while, one kv
            kv_count++;
        }
        if(kv_count==bench->SSTable_kv_capacity||finish==bench->config->MemTable_capacity/2){
            bench->pro.bg_merge_time += get_time_elapsed(bg_start,true);
            SSTable_of.write((char *)temp_kvs, sizeof(key_value)*kv_count);
            SSTable_of.flush();
            SSTable_of.close();
            bench->pro.bg_flush_time += get_time_elapsed(bg_start,true);
            sst_count++;
            kv_count = 0;
        }
    }
    fprintf(stdout,"\tmerge sort:\t%.2f\n",bench->pro.bg_merge_time);
    fprintf(stdout,"\tflush:\t%.2f\n",bench->pro.bg_flush_time);
    fprintf(stdout,"\topen:\t%.2f\n",bench->pro.bg_open_time);
    cout<<"sst_count :"<<sst_count<<" less than"<<1024<<endl;
    for(int i=0;i<bench->config->MemTable_capacity/2; i++){
        cout<<"key_index"<<key_index[i]<<endl;
    }
    delete[] key_index;
    bench->bg_run[old_big].print_meta();
    //logt("merge sort and flush", bg_start);

    //dump meta
    SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-meta", ios::out | ios::trunc);
    SSTable_of.write((char *)&bench->bg_run[old_big].timestamp_min, sizeof(uint));
    SSTable_of.write((char *)&bench->bg_run[old_big].timestamp_max, sizeof(uint));
    SSTable_of.write((char *)&bench->bg_run[old_big].SSTable_count, sizeof(uint));
    SSTable_of.write((char *)bench->bg_run[old_big].first_pid, sizeof(uint)*bench->bg_run[old_big].SSTable_count);
    SSTable_of.flush();
    SSTable_of.close();
    return NULL;
}

void *crash_sst_dump(void *arg){
    cout<<"step into the sst_dump"<<endl;
    workbench *bench = (workbench *)arg;
    cout<<"cur_time: "<<bench->cur_time<<endl;
    uint offset = 0;
    assert(bench->config->MemTable_capacity%2==0);
    uint old_big = bench->big_sorted_run_count;                 //atomic add
    cout<<"old big_sorted_run_count: "<<old_big<<endl;
    bench->big_sorted_run_count++;
    if(old_big%2==1){
        offset = bench->config->MemTable_capacity/2;
    }
    bench->bg_run[old_big].timestamp_min = bench->end_time_min;
    bench->bg_run[old_big].timestamp_max = bench->end_time_max;
    bench->end_time_min = bench->end_time_max;              //new min = old max
    bench->bg_run[old_big].first_pid = new uint[bench->bg_run[old_big].SSTable_count];

    //merge sort
    ofstream SSTable_of;
    //bool of_open = false;
    uint kv_count = 0;
    uint sst_count = 0;
    cout<<"sst_capacity:"<<bench->SSTable_kv_capacity<<endl;
    key_value *temp_kvs = new key_value[bench->SSTable_kv_capacity];
    uint *key_index = new uint[bench->config->MemTable_capacity/2]{0};
    int finish = 0;
    uint64_t temp_key;
    uint taken_id = 0;
    struct timeval bg_start = get_cur_time();
    while(finish<bench->MemTable_count){
        if(kv_count==0){
            SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-"+to_string(sst_count), ios::out | ios::trunc);
            assert(SSTable_of.is_open());
            bench->pro.bg_open_time += get_time_elapsed(bg_start,true);
        }
        finish = 0;
        temp_key = UINT64_MAX;
        taken_id = 0;
        for(int i=0;i<bench->MemTable_count; i++){
            if( bench->h_keys[offset+i][key_index[i]] == 0){              //empty kv
                finish++;
                continue;
            }
//            if(key_index[i]>= bench->config->kv_restriction){              //empty kv
//                finish++;
//                continue;
//            }
            if( temp_key > bench->h_keys[offset+i][key_index[i]] ){
                temp_key = bench->h_keys[offset+i][key_index[i]];
                taken_id = i;
            }
        }
        if(finish<bench->MemTable_count){
            temp_kvs[kv_count].key = temp_key;
            temp_kvs[kv_count].value = bench->h_values[offset + taken_id][key_index[taken_id]];     //box
            if(kv_count==0){
                bench->bg_run[old_big].first_pid[sst_count] = temp_key >> 39;
            }
            bench->h_keys[offset + taken_id][key_index[taken_id]] = 0;                                     //init
            key_index[taken_id]++;                                                                  // one while, one kv
            kv_count++;
        }
        if(kv_count==bench->SSTable_kv_capacity||finish==bench->MemTable_count){
            bench->pro.bg_merge_time += get_time_elapsed(bg_start,true);
            SSTable_of.write((char *)temp_kvs, sizeof(key_value)*kv_count);
            SSTable_of.flush();
            SSTable_of.close();
            bench->pro.bg_flush_time += get_time_elapsed(bg_start,true);
            sst_count++;
            kv_count = 0;
        }
    }
    cout<<"sst_count :"<<sst_count<<" less than"<<1024<<endl;
    for(int i=0;i<bench->MemTable_count; i++){
        cout<<"key_index"<<key_index[i]<<endl;
    }
    delete[] key_index;
    bench->bg_run[old_big].print_meta();

    //dump meta
    SSTable_of.open("../store/SSTable_"+to_string(old_big)+"-meta", ios::out | ios::trunc);
    SSTable_of.write((char *)&bench->bg_run[old_big].timestamp_min, sizeof(uint));
    SSTable_of.write((char *)&bench->bg_run[old_big].timestamp_max, sizeof(uint));
    SSTable_of.write((char *)&bench->bg_run[old_big].SSTable_count, sizeof(uint));
    SSTable_of.write((char *)&bench->bg_run[old_big].first_pid, sizeof(uint)*bench->bg_run[old_big].SSTable_count);
    SSTable_of.flush();
    SSTable_of.close();
    return NULL;
}

void* commandThreadFunction(void* arg) {
    workbench *bench = (workbench *)arg;
    cout<<"bench->config->SSTable_count: "<<bench->config->SSTable_count<<endl;
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
    cout<<"memTable_capacity"<<config->MemTable_capacity<<endl;
	struct timeval start = get_cur_time();
    std::cout << "Running main program..." << std::endl;
	for(int st=config->start_time;st<config->start_time+config->duration;st+=100){
        config->cur_duration = min((config->start_time+config->duration-st),(uint)100);
        if(config->load_data){
            loadData(config->trace_path.c_str(),st,config->cur_duration);
        }
        else{
            generator->generate_trace(trace);
        }
		start = get_cur_time();
		if(!bench){
			bench = part->build_schema(trace, config->num_objects);
			bench->mbr = mbr;
            bench->end_time_min = config->start_time + config->min_meet_time;           //first min time

            //command
            pthread_t command_thread;
            int ret1;
            if ((ret1 = pthread_create(&command_thread, NULL, commandThreadFunction, (void *) bench)) != 0) {
                fprintf(stderr, "pthread_create:%s\n", strerror(ret1));
            }
            pthread_detach(command_thread);
#ifdef USE_GPU
			if(config->gpu){
				d_bench = cuda_create_device_bench(bench, gpu);
			}
#endif
		}
		for(int t=0;t<config->cur_duration;t++){
			log("");
			bench->reset();
			bench->points = trace+t*config->num_objects;
			bench->cur_time = st + t;
            if(bench->cur_time==config->start_time+config->duration-1){         //finish and all dump
                bench->crash_consistency = true;
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
                    cout << bench->search_multi_list[i].pid << "-" << bench->search_multi_list[i].end << "-"
                         << bench->search_multi_list[i].target << endl;
                    print_128(bench->search_multi_list[i].value);
                    cout<<endl;
                }

                //search memtable
                struct timeval newstart = get_cur_time();
                for(int i=0;i<bench->search_multi_length;i++){
                    bench->search_memtable(bench->search_multi_pid[i]);
                }
                bench->pro.search_memtable_time += get_time_elapsed(newstart,false);
                logt("search memtable",newstart);

                //search disk
                for(int i=0;i<bench->search_multi_length;i++){
                    bench->search_in_disk(bench->search_multi_pid[i], bench->valid_timestamp);
                }
                bench->pro.search_in_disk_time += get_time_elapsed(newstart,false);
                logt("search in disk",newstart);
            }
            if(bench->search_single){                                   //search_single
                bench->interrupted = false;                             //reset
                bench->search_single = false;
                bench->search_multi = true;
                cout<<"cuda single search"<<endl;
                cout<<"single_find_count: "<<bench->single_find_count<<endl;
                bench->search_multi_length = bench->single_find_count;
                for(int i=0;i<bench->single_find_count;i++){
                    cout << bench->search_single_pid << "-" << bench->search_single_list[i].end << "-"
                         << bench->search_single_list[i].target << endl;
                    print_128(bench->search_single_list[i].value);
                    cout<<endl;
                    box temp_box(bench->search_single_list[i].value);
                    temp_box.print();
                    bench->search_multi_pid[i] = bench->search_single_list[i].target;
                }

                //search memtable
                struct timeval newstart = get_cur_time();
                bench->search_memtable(bench->search_single_pid);
                bench->pro.search_memtable_time += get_time_elapsed(newstart,false);
                logt("search memtable",newstart);

                //search disk
                bench->search_in_disk(bench->search_single_pid, bench->valid_timestamp);
                bench->pro.search_in_disk_time += get_time_elapsed(newstart,false);
                logt("search in disk",newstart);
                pthread_mutex_unlock(&bench->mutex_i);
                cout<<"final search_multi_length: "<<bench->search_multi_length<<endl;
            }

//            if(bench->MemTable_count>0){
//                bool *check_2G = new bool[10000000];
//                int count = 0;
//                for(int i=0;i<bench->config->kv_restriction;i++){
//                    uint pid = bench->h_keys[0][i]/100000000 / 100000000 / 100000000;
//                    if(!check_2G[pid]){
//                        check_2G[pid] = true;
//                        count++;
//                    }
//                }
//                cout<<count<<" in "<<10000000<<endl;
//                assert(0);
//            }

//            if(bench->MemTable_count==bench->config->MemTable_capacity){              //check 10G
//                bool *check_2G = new bool[10000000];
//                int unique_count = 0;
//                for(int j=0;j<bench->MemTable_count;j++){
//                    for(int i=0;i<bench->config->kv_restriction;i++){
//                        uint pid = bench->h_keys[j][i]/100000000 / 100000000 / 100000000;
//                        if(!check_2G[pid]){
//                            check_2G[pid] = true;
//                            unique_count++;
//                        }
//                    }
//                }
//                cout<<unique_count<<" in "<<10000000<<endl;
//                assert(0);
//            }

            if(bench->crash_consistency){
                uint offset = 0;
                if(bench->big_sorted_run_count%2==1){
                    offset = bench->config->MemTable_capacity/2;
                }
                for(int i=0;i<bench->MemTable_count; i++){
                    for(int j=0;j<10;j++){
                        cout<<bench->h_keys[offset+i][j]<<endl;
                        cout<<(uint)(bench->h_keys[offset+i][j] >> 39)<<endl;
                        print_128(bench->h_values[offset+i][j]);
                        cout<<endl;
                    }
                    cout<<endl;
                }
                cout<<"crash_consistency, 2 merge sort and dump"<<endl;
                cout << "crash dump begin time: " << bench->cur_time << endl;
                crash_sst_dump((void *)bench);
            }
            else if(bench->MemTable_count==bench->config->MemTable_capacity/2) {    //0<=MemTable_count<=MemTable_capacity/2
                bench->end_time_max = bench->cur_time;              //old max

                bench->MemTable_count = 0;
                uint offset = 0;
                if(bench->big_sorted_run_count%2==1){
                    offset = bench->config->MemTable_capacity/2;
                }
                for(int i=0;i<bench->config->MemTable_capacity/2; i++){
                    for(int j=0;j<10;j++){
                        cout<<bench->h_keys[offset+i][j]<<endl;
                        print_128(bench->h_values[offset+i][j]);
                        cout<<endl;
                    }
                    cout<<endl;
                }
                cout << "dump begin time: " << bench->cur_time << endl;
                pthread_t bg_thread;
                int ret;
                if ((ret = pthread_create(&bg_thread, NULL, sst_dump, (void *) bench)) != 0) {
                    fprintf(stderr, "pthread_create:%s\n", strerror(ret));
                }
                pthread_detach(bg_thread);
                //bool findit = searchkv_in_all_place(bench, 2);
            }

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
			logt("round %d",start,st+t+1);
			bench->pro.rounds++;
			bench->pro.max_refine_size = max(bench->pro.max_refine_size, bench->grid_check_counter);
			bench->pro.max_filter_size = max(bench->pro.max_filter_size, bench->filter_list_index);
			bench->pro.max_bucket_num = max(bench->pro.max_bucket_num, bench->num_taken_buckets);
			bench->pro.num_pairs += bench->num_active_meetings;

//			bench->pro.num_meetings += bench->meeting_counter;
//            if (t != 0 && bench->meeting_counter > 0) {
//                fprintf(stdout,"time=%d meeting_counter=%d\n",st + t,bench->meeting_counter);           // st+t+1
//                for (int i = 0; i < bench->meeting_counter; i++) {
//                    //fprintf(stdout,"(%d,%d) %d-%d (%f,%f); ",bench->meetings[i].get_pid1(),bench->meetings[i].get_pid2(),bench->meetings[i].start,bench->meetings[i].end,bench->meetings[i].midpoint.x,bench->meetings[i].midpoint.y);
//                    fprintf(stdout, "%zu (%f,%f)(%f,%f)|%d-%d;", bench->meetings[i].key,
//                            bench->meetings[i].mbr.ow[0], bench->meetings[i].mbr.low[1], bench->meetings[i].mbr.high[0], bench->meetings[i].mbr.high[1],
//                            bench->meetings[i].start, benlch->meetings[i].end);
//                }
//                fprintf(stdout, "\n");
//            }

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
////                SSTable_of.open("../store/SSTable_of" + to_string(t), ios::out | ios::trunc);           //config.DBPath
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
//////                    __uint128_t first_key;
//////                    box first_box;
//////                    read_f.read((char *)&first_key, sizeof(__uint128_t));
//////                    read_f.read((char *)&first_box, sizeof(box));
//////                    print_128(first_key);
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


