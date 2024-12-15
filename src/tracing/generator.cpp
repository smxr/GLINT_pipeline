/*
 * trace_generator.cpp
 *
 *  Created on: Feb 1, 2021
 *      Author: teng
 */

#include "../index/QTree.h"

#include "generator.h"

/*
 *
 * Trip member functions
 *
 * */

Trip::Trip(string str){

    vector<string> cols;
    tokenize(str,cols,",");

    start.timestamp = 0;
    char tmp[2];
    tmp[0] = cols[2][11];
    tmp[1] = cols[2][12];
    start.timestamp += atoi(tmp)*3600;
    tmp[0] = cols[2][14];
    tmp[1] = cols[2][15];
    start.timestamp += atoi(tmp)*60;
    tmp[0] = cols[2][17];
    tmp[1] = cols[2][18];
    start.timestamp += atoi(tmp);
    if(cols[2][20]=='P'){
        start.timestamp += 12*3600;
    }
    end.timestamp = start.timestamp + atoi(cols[4].c_str());

    start.coordinate = Point(atof(cols[18].c_str()),atof(cols[17].c_str()));
    end.coordinate = Point(atof(cols[21].c_str()),atof(cols[20].c_str()));
}

void Trip::print_trip(){
    printf("time: %d to %d\n",start.timestamp,end.timestamp);
    printf("position: (%f %f) to (%f %f)\n",start.coordinate.x,start.coordinate.y,end.coordinate.x,end.coordinate.y);
}

void Trip::resize(int md){
    if(md>0&&duration()>md){
        if(type==REST){
            end.timestamp = start.timestamp+md;
        }else{
            double portion = md*1.0/duration();
            end.coordinate.x = (end.coordinate.x-start.coordinate.x)*portion+start.coordinate.x;
            end.coordinate.y = (end.coordinate.y-start.coordinate.y)*portion+start.coordinate.y;
            end.timestamp = start.timestamp + md+1;
        }
    }
}


/*
 *
 * functions for generating simulated traces of an object
 * based on the real world statistics
 *
 * */

trace_generator::trace_generator(generator_configuration *conf, Map *m){
    assert(conf && m);

    config = conf;
    map = m;

    ifstream infile(config->meta_path.c_str(), ios::in | ios::binary);
    if(!infile.is_open()){
        log("failed opening %s",config->meta_path.c_str());
        exit(0);
    }

    infile.read((char *)&tweet_count, sizeof(tweet_count));
    tweets = new Point[tweet_count];
    tweets_assign = new uint[tweet_count];
    for(int i=0;i<tweet_count;i++){
        infile.read((char *)&tweets[i], sizeof(Point));
        infile.read((char *)&tweets_assign[i], sizeof(uint));
    }
    infile.read((char *)&core_count, sizeof(core_count));
    cores = new gen_core[core_count];
    for(int i=0;i<core_count;i++){
        cores[i].id = i;
        infile.read((char *)&cores[i].core, sizeof(Point));
        int dest_num = 0;
        infile.read((char *)&dest_num, sizeof(dest_num));
        int d = 0;
        double r = 0.0;
        for(int j=0;j<dest_num;j++){
            infile.read((char *)&d, sizeof(d));
            infile.read((char *)&r, sizeof(r));
            cores[i].destination.push_back(pair<int, double>(d,r));
        }
    }
    cout<<"core_count : "<<core_count<<endl;
    cout<<"tweet_count : "<<tweet_count<<endl;
//    for(uint i = 0; i < core_count; i++){
//        cores[i].core.print();
//    }
//    cerr <<"cores and tweets points" << endl;
//    for(uint i = 0; i < tweet_count; i++){
//        tweets[i].print();
//    }
    for(int i=0;i<tweet_count;i++){
        cores[tweets_assign[i]].assigned_tweets.push_back(i);
    }
    infile.close();
    meta_data = new Trace_Meta[config->num_objects];
}
trace_generator::~trace_generator(){
    //map = NULL;
    if(map){
        delete map;
    }
    if(tweets){
        delete []tweets;
    }
    if(tweets_assign){
        delete []tweets_assign;
    }
    if(cores){
        delete []cores;
    }
    if(meta_data){
        delete []meta_data;
    }
}

//bool orderzone(ZoneStats *i, ZoneStats *j) { return (i->count>j->count); }
//
//void trace_generator::analyze_trips(const char *path, int limit){
//	struct timeval start = get_cur_time();
//
//	if(total){
//		delete total;
//	}
//	std::ifstream file(path);
//	if(!file.is_open()){
//		log("%s cannot be opened",path);
//		exit(0);
//	}
//	std::string str;
//	//skip the head
//	std::getline(file, str);
//	total = new ZoneStats(0);
//
//	while (std::getline(file, str)&&--limit>0){
//		Trip *t = new Trip(str);
//		t->start.coordinate.x += 0.02*(get_rand_double()-0.5);
//		t->start.coordinate.y += 0.012*(get_rand_double()-0.5);
//		t->end.coordinate.x += 0.02*(get_rand_double()-0.5);
//		t->end.coordinate.y += 0.012*(get_rand_double()-0.5);
//		// a valid trip should be covered by the map,
//		// last for a while and the distance larger than 0
//		if(map->getMBR()->contain(t->start.coordinate)&&
//		   map->getMBR()->contain(t->end.coordinate)&&
//		   t->length()>0&&
//		   t->duration()>0){
//
//			int gids[2];
//			gids[0] = grid->getgridid(&t->start.coordinate);
//			gids[1] = grid->getgridid(&t->end.coordinate);
//			double dist = t->start.coordinate.distance(t->end.coordinate, true);
//			for(int i=0;i<2;i++){
//				int zid = gids[i];
//				int ezid = gids[!i];
//				zones[zid]->count++;
//				zones[zid]->duration += t->duration();
//				zones[zid]->length += dist;
//				total->count++;
//				total->duration += t->duration();
//				total->length += dist;
//			}
//		}
//		delete t;
//	}
//	file.close();
//
//	// reorganize
//	sort(zones.begin(),zones.end(),orderzone);
//	logt("analyze trips in %s",start, path);
//}


/*
 *
 * get the next location according to the distribution of the statistic
 *
 *
 * */


Point trace_generator::get_random_location(int seed){
    int tid = 0;
    if(seed==-1){
        tid = get_rand_number(tweet_count)-1;
    }else{
        assert(seed<core_count);
        if(cores[seed].assigned_tweets.size()>0){
            tid = get_rand_number(cores[seed].assigned_tweets.size())-1;
            tid = cores[seed].assigned_tweets[tid];
        }else{
            tid = get_rand_number(tweet_count)-1;
        }
    }
    double xval = tweets[tid].x + (0.5-get_rand_double())*100*degree_per_meter_longitude(tweets[tid].y);                //100
    double yval = tweets[tid].y + (0.5-get_rand_double())*100*degree_per_meter_latitude;
    //maybe not in the map mbr
//    box map_mbr = map->getMBR();
//    if(xval < map_mbr.low[0]){
//        double offset = map_mbr.low[0] - xval;
//        xval = map_mbr.low[0] + offset;
//    }
//    if(xval > map_mbr.high[0]){
//        double offset = xval - map_mbr.high[0];
//        xval = map_mbr.high[0] - offset;
//    }
//    if(yval < map_mbr.low[1]){
//        double offset = map_mbr.low[1] - yval;
//        yval = map_mbr.low[1] + offset;
//    }
//    if(yval > map_mbr.high[1]){
//        double offset = yval - map_mbr.high[1];
//        yval = map_mbr.high[1] - offset;
//    }
    return Point(xval, yval);
}

int trace_generator::get_core(int seed){
    int next_seed = 0;
    if(seed==-1){
        next_seed = get_rand_number(core_count)-1;
        assert(next_seed>=0&&next_seed<core_count);
    }else{
        double target = get_rand_double();
        double cum = 0;
        for(int i=0;i<cores[seed].destination.size();i++){
            cum += cores[seed].destination[i].second;
            if(cum>=target){
                next_seed = cores[seed].destination[i].first;
                break;
            }
        }
    }
    return next_seed;
}

uint trace_generator::get_ave_walk_time(){
    double p = get_rand_double();
    if (p < 0.01) { // 1/100   600
        return 600;
    } else if (p < 0.07) { // 6/100  200
        return 200;
    } else if (p < 0.17) { // 10/100  100
        return 100;
    } else if (p < 0.37) { // 20/100  50
        return 50;
    } else { // 63/100  10
        return 10;
    }
}

void trace_generator::fill_trace(Point * ret, Map *mymap, int obj){                 //return --- result
    // use the default map for single thread mode
    if(!mymap){
        mymap = map;
    }
    assert(mymap);
    if(meta_data[obj].core == -2){                          //initial
        meta_data[obj].core = get_core();
        meta_data[obj].loc = get_random_location(meta_data[obj].core);
        meta_data[obj].type = NOT_YET;
    }
    //bool exactly_finish = false;
    int count = 0;
    while(count<config->cur_duration){
        if(meta_data[obj].type==NOT_YET){
            meta_data[obj].end = get_random_location(meta_data[obj].core);
            double p = get_rand_double();
            if(p < config->drive_rate){
                meta_data[obj].type = DRIVE;
            }else if(p < config->drive_rate + config->walk_rate){
                meta_data[obj].type = WALK;
            }else {
                meta_data[obj].type = REST;
            }
//            if(tryluck(config->drive_rate)){
//                meta_data[obj].type = DRIVE;
//                rested = false;
//            }else if(tryluck(config->walk_rate)){
//                meta_data[obj].type = WALK;
//                rested = false;
//            }else if(!rested){
//                meta_data[obj].type = REST;
//                rested = true;
//            }
        }
        if(meta_data[obj].type == DRIVE){
            if(meta_data[obj].trajectory.empty()){                      //new trip
                meta_data[obj].core = get_core(meta_data[obj].core);            //change core
                meta_data[obj].end = get_random_location(meta_data[obj].core);
                meta_data[obj].speed = config->drive_speed -5 + (uint)(10 * get_rand_double());
            }
            //meta_data[obj].speed = config->drive_speed;
            mymap->navigate(ret, meta_data[obj], config->cur_duration, count, config->num_objects, obj);
            if(meta_data[obj].trajectory.empty()){
                meta_data[obj].type = NOT_YET;
            }
        }else if(meta_data[obj].type == WALK){
            if(!meta_data[obj].time_remaining){
                meta_data[obj].time_remaining = (get_ave_walk_time() - 10) * 2 * get_rand_double() + 5;
            }
            const double step = config->walk_speed/meta_data[obj].end.distance(meta_data[obj].loc, true);
//            uint pause_timestamp = 10 * get_rand_double();
//            uint pause_length = 10 * get_rand_double();
            double portion;
            for(portion = step;portion<(1+step) && count<config->cur_duration && meta_data[obj].time_remaining > 0;){
                ret[count*config->num_objects+obj].x = meta_data[obj].loc.x+portion*(meta_data[obj].end.x - meta_data[obj].loc.x);
                ret[count*config->num_objects+obj].y = meta_data[obj].loc.y+portion*(meta_data[obj].end.y - meta_data[obj].loc.y);
                count++;
                meta_data[obj].time_remaining--;
                portion += step;
//                if(count == pause_timestamp * 10 + 1 && get_rand_double() > 0.4){
//                    for(uint i = 0; i < pause_length && count<config->cur_duration; i++){
//                        ret[count*config->num_objects+obj].x = ret[(count-1)*config->num_objects+obj].x;
//                        ret[count*config->num_objects+obj].y = ret[(count-1)*config->num_objects+obj].y;
//                        count++;
//                        meta_data[obj].time_remaining--;
//                    }
//                }
            }
            meta_data[obj].loc = ret[(count-1)*config->num_objects+obj];
            if(!meta_data[obj].time_remaining || portion > 1){
                meta_data[obj].type = NOT_YET;
            }
        }else if(meta_data[obj].type == REST){
            if(!meta_data[obj].time_remaining){
                meta_data[obj].time_remaining = (config->max_rest_time - 20) * get_rand_double() + 10;
                //meta_data[obj].time_remaining = (config->max_rest_time - 200) * get_rand_double() + 100;
            }
            //int dur = meta_data[obj].rest_time;
            while(meta_data[obj].time_remaining > 0 && count < config->cur_duration){
                ret[count*config->num_objects+obj] = meta_data[obj].loc;
                count++;
                meta_data[obj].time_remaining--;
            }
            if(!meta_data[obj].time_remaining){
                meta_data[obj].type = NOT_YET;
            }
        }
//        if(count<config->cur_duration){             //must
//            assert(meta_data[obj].type == NOT_YET);
//            meta_data[obj].type = NOT_YET;
//        }
    }
}

void *gentrace_unit(void *arg){
    query_context *ctx = (query_context *)arg;
    trace_generator *gen = (trace_generator *)ctx->target[0];
    Point *result = (Point *)ctx->target[1];
    Map *mymap = gen->map->clone();
    while(true){
        // pick one object for generating
        size_t start = 0;
        size_t end = 0;
        if(!ctx->next_batch(start,end)){
            break;
        }
        for(int obj=start;obj<end;obj++){
            gen->fill_trace(result, mymap, obj);
        }

    }
    delete mymap;
    return NULL;
}

// void trace_generator::generate_trace(Point * traces){
//     srand (time(NULL));
//     struct timeval start = get_cur_time();
//     pthread_t threads[config->num_threads];
//     query_context tctx;
//     tctx.config = config;
//     tctx.target[0] = (void *)this;
//     tctx.target[1] = (void *)traces;
//     tctx.num_units = config->num_objects;
//     tctx.report_gap = 1;
//     tctx.num_batchs = 100000;
//     for(int i=0;i<config->num_threads;i++){
//         pthread_create(&threads[i], NULL, gentrace_unit, (void *)&tctx);
//     }
//     for(int i = 0; i < config->num_threads; i++ ){
//         void *status;
//         pthread_join(threads[i], &status);
//     }
//     logt("generate traces",start);
// }

void trace_generator::generate_trace(Point * traces){
    srand (time(NULL));
    struct timeval start = get_cur_time();
    pthread_t threads[config->num_threads];
    query_context tctx;
    tctx.config = config;
    tctx.target[0] = (void *)this;
    tctx.target[1] = (void *)traces;
    tctx.num_units = config->num_objects;
    tctx.report_gap = 1;
    tctx.num_batchs = 100000;
    // for(int i=0;i<config->num_threads;i++){
    //     pthread_create(&threads[i], NULL, gentrace_unit, (void *)&tctx);
    // }
#pragma omp parallel for num_threads(128)
    for(int i = 0; i < config->num_objects / 100000; i++ ){
        trace_generator *gen = this;
        Point *result = traces;
        Map *mymap = gen->map->clone();
        for (int j = 0; j < 100000; j++)
        {
            gen->fill_trace(result, mymap, i);
        }
        delete mymap;
    }
    logt("generate traces",start);
}
