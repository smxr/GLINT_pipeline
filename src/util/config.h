/*
 * context.h
 *
 *  Created on: Jan 16, 2021
 *      Author: teng
 */

#ifndef SRC_UTIL_CONTEXT_H_
#define SRC_UTIL_CONTEXT_H_

#include <boost/program_options.hpp>

#include "util.h"
namespace po = boost::program_options;
class configuration{
public:
	// shared parameters
	int thread_id = 0;
	uint num_threads = 1;
	uint duration = 1000;
	uint num_objects = 1000;
	string trace_path = "/gisdata/chicago/traces";

	// for query only
	uint start_time = 0;
	uint grid_capacity = 100;
	uint zone_capacity = 100;
	size_t num_meeting_buckets = 100000;

	double grid_amplify = 2;
	uint refine_size = 3;
	bool dynamic_schema = true;
	bool phased_lookup = true;
	bool unroll = true;
	uint schema_update_delay = 1; //
	uint min_meet_time = 10;
	double reach_distance = 2;
	double x_buffer = 0;
	double y_buffer = 0;
	bool gpu = false;
	uint specific_gpu = 0;
	bool analyze_reach = false;
	bool analyze_grid = false;
	bool profile = false;

	void print(){
		fprintf(stderr,"configuration:\n");
		fprintf(stderr,"num threads:\t%d\n",num_threads);
		fprintf(stderr,"num objects:\t%d\n",num_objects);
		fprintf(stderr,"grid capacity:\t%d\n",grid_capacity);
		fprintf(stderr,"zone capacity:\t%d\n",zone_capacity);
		fprintf(stderr,"start time:\t%d\n",start_time);
		fprintf(stderr,"duration:\t%d\n",duration);
		fprintf(stderr,"reach distance:\t%.0f m\n",reach_distance);
		fprintf(stderr,"minimum meeting time:\t%d\n",min_meet_time);
		fprintf(stderr,"num buckets:\t%ld\n",num_meeting_buckets);

		fprintf(stderr,"trace path:\t%s\n",trace_path.c_str());
		fprintf(stderr,"use gpu:\t%s\n",gpu?"yes":"no");
		if(gpu){
			fprintf(stderr,"which gpu:\t%d\n",specific_gpu);
		}
		fprintf(stderr,"unroll:\t%s\n",unroll?"yes":"no");
		fprintf(stderr,"dynamic schema:\t%s\n",dynamic_schema?"yes":"no");
		fprintf(stderr,"schema update gap:\t%d\n",schema_update_delay);

		fprintf(stderr,"analyze reach:\t%s\n",analyze_reach?"yes":"no");
		fprintf(stderr,"analyze grid:\t%s\n",analyze_grid?"yes":"no");

	}
};


inline configuration get_parameters(int argc, char **argv){
	configuration config;
	config.num_threads = get_num_threads();

	po::options_description desc("query usage");
	desc.add_options()
		("help,h", "produce help message")
		("gpu,g", "use gpu for processing")
		("profile,p", "profile the memory usage")
		("disable_phased_filter", "disable phased filter")
		("disable_unroll,u", "disable unroll the refinement")
		("disable_dynamic_schema", "the schema is not dynamically updated")

		("analyze_reach", "analyze the reaches statistics")
		("analyze_grid", "analyze the grid statistics")
		("threads,n", po::value<uint>(&config.num_threads), "number of threads")
		("specific_gpu", po::value<uint>(&config.specific_gpu), "use which gpu")
		("grid_capacity", po::value<uint>(&config.grid_capacity), "maximum number of objects per grid ")
		("grid_amplify", po::value<double>(&config.grid_amplify), "amplify the grid size to avoid overflow")
		("zone_capacity", po::value<uint>(&config.zone_capacity), "maximum number of objects per zone buffer")
		("refine_size", po::value<uint>(&config.refine_size), "number of refine list entries per object")
		("objects,o", po::value<uint>(&config.num_objects), "number of objects")
		("num_buckets,b", po::value<size_t>(&config.num_meeting_buckets), "number of meeting buckets")

		("duration,d", po::value<uint>(&config.duration), "duration of the trace")
		("min_meet_time,m", po::value<uint>(&config.min_meet_time), "minimum meeting time")

		("start_time,s", po::value<uint>(&config.start_time), "the start time of the duration")

		("reachable_distance,r", po::value<double>(&config.reach_distance), "reachable distance (in meters)")
		("trace_path,t", po::value<string>(&config.trace_path), "path to the trace file")

		;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	if (vm.count("help")) {
		cout << desc << "\n";
		exit(0);
	}
	po::notify(vm);

	if(vm.count("gpu")){
		config.gpu = true;
	}
	if(vm.count("analyze_reach")){
		config.analyze_reach = true;
	}
	if(vm.count("analyze_grid")){
		config.analyze_grid = true;
	}
	if(vm.count("profile")){
		config.profile = true;
	}

	if(!vm.count("zone_capacity")||!vm.count("gpu")){
		config.zone_capacity = config.grid_capacity;
	}
	if(!vm.count("num_buckets")){
		config.num_meeting_buckets = 2*config.num_objects;
	}
	if(vm.count("disable_phased_filter")){
		config.phased_lookup = false;
	}
	if(vm.count("disable_unroll")){
		config.unroll = false;
		config.zone_capacity = config.grid_capacity;
	}else if(!vm.count("zone_capacity")){
		config.zone_capacity = config.grid_capacity/2;
	}

	if(vm.count("disable_dynamic_schema")){
		config.dynamic_schema = false;
	}


	config.print();
	return config;
}


class generator_configuration:public configuration{
public:
	// how many percent of the initial points are evenly distributed
	double walk_rate = 0.4;
	double walk_speed = 1.0;
	double drive_rate = 0.2;
	double drive_speed = 15.0;
	int max_rest_time = 600;

	string map_path = "/gisdata/chicago/streets";
	string meta_path = "/gisdata/chicago/tweet.dat";

	void print(){
		fprintf(stderr, "generator configuration:\n");
		fprintf(stderr,"num threads:\t%d\n",num_threads);
		fprintf(stderr,"num objects:\t%d\n",num_objects);
		fprintf(stderr,"duration:\t%d\n",duration);
		fprintf(stderr,"walk rate:\t%.2f\n",walk_rate);
		fprintf(stderr,"walk speed:\t%.2f\n",walk_speed);
		fprintf(stderr,"drive rate:\t%.2f\n",drive_rate);
		fprintf(stderr,"drive speed:\t%.2f\n",drive_speed);

		fprintf(stderr,"map path:\t%s\n",map_path.c_str());
		fprintf(stderr,"metadata path:\t%s\n",meta_path.c_str());
		fprintf(stderr,"trace path:\t%s\n",trace_path.c_str());
	}
};


inline generator_configuration get_generator_parameters(int argc, char **argv){
	generator_configuration config;
	config.num_threads = get_num_threads();

	po::options_description desc("generator usage");
	desc.add_options()
		("help,h", "produce help message")
		("threads,n", po::value<uint>(&config.num_threads), "number of threads")
		("objects,o", po::value<uint>(&config.num_objects), "number of objects")
		("duration,d", po::value<uint>(&config.duration), "duration of the trace")
		("map_path", po::value<string>(&config.map_path), "path to the map file")
		("trace_path", po::value<string>(&config.trace_path), "path to the trace file")
		("meta_path", po::value<string>(&config.meta_path), "path to the metadata file")
		("walk_rate", po::value<double>(&config.walk_rate), "percent of walk")
		("walk_speed", po::value<double>(&config.walk_speed), "the speed of walk (meters/second)")
		("drive_rate", po::value<double>(&config.drive_rate), "percent of drive")
		("drive_speed", po::value<double>(&config.drive_speed), "the speed of drive (meters/second)")
		;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	if (vm.count("help")) {
		cout << desc << "\n";
		exit(0);
	}
	po::notify(vm);

	assert(config.walk_rate+config.drive_rate<=1);
	config.print();
	return config;
}


#endif /* SRC_UTIL_CONTEXT_H_ */
