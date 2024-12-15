/*
 * query_context.h
 *
 *  Created on: Jan 25, 2021
 *      Author: teng
 */

#ifndef QUERY_CONTEXT_H_
#define QUERY_CONTEXT_H_
#include "config.h"
#include "../geometry/geometry.h"
#include "../cuda/cuda_util.cuh"
#include <pthread.h>

#define MAX_LOCK_NUM 10000

#define MAX_TARGET_NUM 10

class offset_size{
public:
	uint offset;
	uint size;
};

class query_context{
	size_t next_report = 0;
	size_t step = 0;

public:
	configuration *config = NULL;
	size_t counter = 0;
	size_t num_units = 0;
	size_t num_batchs = 1000;
	size_t report_gap = 100;
	pthread_mutex_t *lk;

	// query source
	void *target[MAX_TARGET_NUM];

	~query_context(){
		for(int i=0;i<MAX_TARGET_NUM;i++){
			target[i] = NULL;
		}
		delete []lk;
	}
	query_context(){
		lk = new pthread_mutex_t[MAX_LOCK_NUM];
		for(int i=0;i<MAX_LOCK_NUM;i++){
			pthread_mutex_init(&lk[i], NULL);
		}
	}
	void lock(int hashid=0){
		pthread_mutex_lock(&lk[hashid%MAX_LOCK_NUM]);
	}
	void unlock(int hashid=0){
		pthread_mutex_unlock(&lk[hashid%MAX_LOCK_NUM]);
	}
	bool next_batch(size_t &start, size_t &end){
		pthread_mutex_lock(&lk[0]);
		start = counter;
		counter += max((size_t)1, num_units/num_batchs);
		end = counter;
			end = num_units;
		if(end>num_units){
		}
		//log("%d %d %d %d",start,next_report,num_objects,report_gap);
		if(report_gap<100&&end>=next_report&&start<num_units){
			log("%ld%%",(end+1)*100/num_units);
			next_report += num_units*report_gap/100;
		}

		pthread_mutex_unlock(&lk[0]);
		return start<num_units;
	}
	void idle(){
		pthread_mutex_lock(&lk[0]);
		counter--;
		pthread_mutex_unlock(&lk[0]);
	}
	void busy(){
		pthread_mutex_lock(&lk[0]);
		counter++;
		pthread_mutex_unlock(&lk[0]);
	}
	bool all_idle(){
		return counter == 0;
	}
	void reset(){
		next_report = 0;
		counter = 0;
	}
};


class time_query{
public:
    uint t_start = 0;
    uint t_end = 0;
    bool abandon = true;
    bool check_key_time(__uint128_t key){           // tq->t_start -= ctb.start_min   tq->t_end -= ctb.start_min
        uint key_end = get_key_end(key);
        uint key_start = key_end - get_key_duration(key);
        return abandon || (key_start < t_end) && (t_start < key_end);
    }
};

class box_query{
public:
    box search_b;
    bool abandon = true;
    bool check_box_intersect(box key_box){
        return abandon || search_b.intersect(key_box);
    }
};

class object_query{

};

struct box_search_info{
    uint ctb_id;
    uint ctf_id;
    box * bmap_mbr;
    time_query tq;
};


#endif /* QUERY_CONTEXT_H_ */
