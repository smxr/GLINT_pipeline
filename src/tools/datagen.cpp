/*
 * main.cpp
 *
 *  Created on: Jan 11, 2021
 *      Author: teng
 */


#include "../geometry/Map.h"
#include "../util/config.h"
#include <vector>
#include <stdlib.h>
#include "../tracing/generator.h"
#include "../tracing/trace.h"

using namespace std;

void dumpTo(generator_configuration * config, uint st, Point * trace) {
    struct timeval start_time = get_cur_time();
    string filename = config->trace_path + to_string(st) + "_" + to_string(config->num_objects) + ".tr";
    ofstream wf(filename, ios::out|ios::binary|ios::trunc);
    size_t num_points = 100 * config->num_objects;
    wf.write((char *)trace, sizeof(Point)*num_points);
    wf.close();
    logt("dumped to %s",start_time, filename.c_str());
}

int main(int argc, char **argv){
	generator_configuration config = get_generator_parameters(argc, argv);
    //config.cur_duration = 500;
	Map *m = new Map(config.map_path);
	m->print_region();
	trace_generator *gen = new trace_generator(&config,m);
	Point *traces = new Point [100*config.num_objects];
    for(uint i = 0; i < config.duration; i+= 100){          //always whole 100
        config.cur_duration = min((config.start_time+config.duration-i),(uint)100);
        cout << "config.cur_duration : "<< config.cur_duration <<endl;
        gen->generate_trace(traces);
        // dumpTo(&config, i, traces);
    }

    delete []traces;
	delete gen;
	return 0;
}

