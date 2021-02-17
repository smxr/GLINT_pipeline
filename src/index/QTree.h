/*
 * QTree.h
 *
 *  Created on: Jan 1, 2021
 *      Author: teng
 */

#ifndef SRC_INDEX_QTREE_H_
#define SRC_INDEX_QTREE_H_

#include "../geometry/geometry.h"
#include "../util/query_context.h"
#include <float.h>
#include <stack>
using namespace std;


/**
 * children layout
 *
 *     2   3
 *
 *     0   1
 * */
enum QT_Direction{
	bottom_left = 0,
	bottom_right = 1,
	top_left = 2,
	top_right = 3
};
class QTNode;

/*
 *
 * each child is represented as an unsigned integer number
 * the lowest bit is the sign of whether it is a leaf or not
 * the rest bits represent the id if it is a leaf,
 * otherwise point to the offset of the children information
 * of such child
 *
 * */
typedef struct QTSchema{
	uint children[4];
	double mid_x;
	double mid_y;
}QTSchema;


class QTConfig{
public:
	// for regulating the split of nodes
	int max_level = INT_MAX;
	int max_leafs = INT_MAX;
	int max_objects = INT_MAX;
	// minimum width of each region in meters
	double min_width = 5;
	double x_buffer = 0;
	double y_buffer = 0;
	bool split_node = true;
	// counter
	int num_leafs = 0;

	// the buffer to all the points
	Point *points = NULL;
	QTConfig(){}
};

class QTNode{
public:

	double mid_x = 0;
	double mid_y = 0;
	int level = 0;
	QTConfig *config = NULL;
	uint node_id = 0;
	box mbr;
	QTNode *children[4] = {NULL,NULL,NULL,NULL};
	// the IDs of each point belongs to this node
	uint *objects = NULL;
	int object_index = 0;
	int capacity = 0;

	void set_id(uint &id){
		if(isleaf()){
			node_id = id++;
		}else{
			for(int i=0;i<4;i++){
				children[i]->set_id(id);
			}
		}
	}

	QTNode(double low_x, double low_y, double high_x, double high_y, QTConfig *conf){
		mbr.low[0] = low_x;
		mbr.low[1] = low_y;
		mbr.high[0] = high_x;
		mbr.high[1] = high_y;
		mid_x = (mbr.high[0]+mbr.low[0])/2;
		mid_y = (mbr.high[1]+mbr.low[1])/2;
		assert(mbr.low[0]!=mbr.high[0]);
		assert(mbr.low[1]!=mbr.high[1]);
		assert(mbr.low[0]!=mid_x);
		assert(mbr.low[1]!=mid_y);
		config = conf;
		capacity = conf->max_objects+1;
		objects = (uint *)malloc((capacity)*sizeof(uint));
	}
	QTNode(box m, QTConfig *conf):QTNode(m.low[0], m.low[1], m.high[0], m.high[1],conf){
	}
	~QTNode(){
		if(!isleaf()){
			for(int i=0;i<4;i++){
				delete children[i];
			}
		}else{
			free(objects);
		}
	}
	inline bool isleaf(){
		return children[0]==NULL;
	}

	void query(vector<uint> &result, Point *p){
		if(isleaf()){
			result.push_back(this->node_id);
		}else{
			// could be possibly in multiple children with buffers enabled
			bool top = (p->y>mid_y-config->y_buffer);
			bool bottom = (p->y<=mid_y+config->y_buffer);
			bool left = (p->x<=mid_x+config->x_buffer);
			bool right = (p->x>mid_x-config->x_buffer);
			if(bottom&&left){
				children[0]->query(result, p);
			}
			if(bottom&&right){
				children[1]->query(result, p);
			}
			if(top&&left){
				children[2]->query(result, p);
			}
			if(top&&right){
				children[3]->query(result, p);
			}
		}
	}

	bool split(){
		bool should_split = config->split_node &&
							object_index>=config->max_objects &&
				   	   	    level<config->max_level;
							//config->num_leafs<config->max_leafs &&
				   	   	    //&&mbr.width(true)>config->min_width;
		if(!should_split){
			return false;
		}

		children[bottom_left] = new QTNode(mbr.low[0],mbr.low[1],mid_x,mid_y, config);
		children[bottom_right] = new QTNode(mid_x,mbr.low[1],mbr.high[0],mid_y, config);
		children[top_left] = new QTNode(mbr.low[0],mid_y,mid_x,mbr.high[1], config);
		children[top_right] = new QTNode(mid_x,mid_y,mbr.high[0],mbr.high[1], config);

		for(int i=0;i<4;i++){
			children[i]->level = level+1;
		}
		for(int i=0;i<object_index;i++){
			// reinsert all the objects to next level
			insert(objects[i]);
		}
		free(objects);
		object_index = 0;
		return true;
	}

	void insert(uint pid){
		if(isleaf()){
			// avoid overflow the buffer
			// happens when the grid is too condense
			if(object_index==capacity){
				capacity += config->max_objects;
				uint *newobjects = (uint *)malloc(capacity*sizeof(uint));
				memcpy(newobjects,objects,(capacity-config->max_objects)*sizeof(uint));
				free(objects);
				objects = newobjects;
			}
			objects[object_index++] = pid;
			split();
		}else{
			// no need to lock other nodes
			Point *p = config->points+pid;
			// could be possibly in multiple children
			bool top = (p->y>mid_y);
			bool bottom = (p->y<=mid_y);
			bool left = (p->x<=mid_x);
			bool right = (p->x>mid_x);
			if(bottom&&left){
				children[0]->insert(pid);
			}
			if(bottom&&right){
				children[1]->insert(pid);
			}
			if(top&&left){
				children[2]->insert(pid);
			}
			if(top&&right){
				children[3]->insert(pid);
			}
		}
	}

	size_t leaf_count(){
		if(isleaf()){
			return 1;
		}else{
			size_t num = 0;
			for(int i=0;i<4;i++){
				num += children[i]->leaf_count();
			}
			return num;
		}
	}
	size_t node_count(){
		if(isleaf()){
			return 0;
		}else {
			size_t num = 1;
			for(int i=0;i<4;i++){
				num += children[i]->node_count();
			}
			return num;
		}
	}
	size_t num_objects(){
		if(isleaf()){
			return object_index;
		}else{
			size_t num = 0;
			for(int i=0;i<4;i++){
				num += children[i]->num_objects();
			}
			return num;
		}
	}
	void get_leafs(vector<QTNode *> &leafs, bool skip_empty = true){

		if(isleaf()){
			if(!skip_empty||object_index>0){
				leafs.push_back(this);
			}
		}else{
			for(int i=0;i<4;i++){
				children[i]->get_leafs(leafs, skip_empty);
			}
		}
	}

	void get_leafs(vector<QTNode *> &grids, vector<size_t> &object_num,bool skip_empty = true){

		if(isleaf()){
			if(!skip_empty||object_index>0){
				grids.push_back(this);
				object_num.push_back(object_index);
			}
		}else{
			for(int i=0;i<4;i++){
				children[i]->get_leafs(grids, object_num, skip_empty);
			}
		}
	}

	void fix_structure(){
		object_index = 0;
		if(!isleaf()){
			for(int i=0;i<4;i++){
				children[i]->fix_structure();
			}
		}
		config->split_node = false;
	}

	void finalize(){
		uint id = 0;
		set_id(id);
	}

	void print(){
		vector<QTNode *> nodes;
		get_leafs(nodes,false);
		printf("MULTIPOLYGON(");
		for(int i=0;i<nodes.size();i++){
			if(i>0){
				printf(",");
			}
			printf("((");
			nodes[i]->mbr.print_vertices();
			printf("))");
		}
		printf(")\n");
		nodes.clear();
	}

	Point *get_point(uint pid){
		return config->points+objects[pid];
	}

	void create_schema(QTSchema *schema){
		uint offset = 0;
		create_schema(schema, offset);
	}
	void create_schema(QTSchema *schema, uint &offset){
		assert(!isleaf());
		uint curoff = offset++;
		schema[curoff].mid_x = mid_x;
		schema[curoff].mid_y = mid_y;
		for(int i=0;i<4;i++){
			if(children[i]->isleaf()){
				schema[curoff].children[i] = children[i]->node_id;
				schema[curoff].children[i] <<= 1;
				schema[curoff].children[i] |= 1;
			}else{
				schema[curoff].children[i] = offset;
				schema[curoff].children[i] <<= 1;
				children[i]->create_schema(schema, offset);
			}
		}
	}

//	void pack_data(uint *pids, offset_size *os){
//		if(isleaf()){
//			if(node_id==0){
//				os[node_id].offset = 0;
//			}else{
//				os[node_id].offset = os[node_id-1].offset+os[node_id-1].size;
//			}
//			os[node_id].size = object_index;
//			if(object_index>0){
//				memcpy((void *)(pids+os[node_id].offset),(void *)objects,os[node_id].size*sizeof(uint));
//			}
//		}else{
//			for(int i=0;i<4;i++){
//				children[i]->pack_data(pids, os);
//			}
//		}
//	}

//	void print_node(){
//		printf("level: %d objects: %ld width: %f height: %f",level,object_index,mbr.width(true),mbr.height(true));
//		mbr.print();
//		print_points(objects,object_index);
//	}

};

#endif /* SRC_INDEX_QTREE_H_ */
