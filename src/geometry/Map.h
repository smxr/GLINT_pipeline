/*
 * Map.h
 *
 *  Created on: Jan 11, 2021
 *      Author: teng
 */

#ifndef DATAGEN_MAP_H_
#define DATAGEN_MAP_H_

#include "geometry.h"
#include <queue>
#include <iostream>
#include <fstream>
#include <float.h>
#include "../util/util.h"
#include "../util/config.h"

using namespace std;

class Node: public Point{
public:
	int id = 0;
	Node(){}
	Node(double xx, double yy){
		x = xx;
		y = yy;
	}
	Node(Node *n){
		x = n->x;
		y = n->y;
		id = n->id;
	}
};

/*
 * represents a segment with some features
 *
 * */
class Street {
public:

	unsigned int id = 0;
	Node *start = NULL;
	Node *end = NULL;
	// cache for Euclid distance of vector start->end
	double length = -1.0;
	//meters per second
	double speed = 10;
	// other streets this street connects
	vector<Street *> connected;
	// temporary storage for breadth first search
	Street * father_from_origin = NULL;

	~Street(){
		connected.clear();
		father_from_origin = NULL;
	}
	void print(){
		printf("%d\t: ",id);
		printf("[[%f,%f],",start->x,start->y);
		printf("[%f,%f]]\t",end->x,end->y);
		printf("\tconnect: ");
		for(Street *s:connected) {
			printf("%d\t",s->id);
		}
		printf("\n");
	}


	//not distance in real world, but Euclid distance of the vector from start to end
	double getLength() {
		if(length<0) {
			length = start->distance(*end);
		}
		return length;
	}

	Street(unsigned int i, Node *s, Node *e) {
		assert(s&&e);
		start = s;
		end = e;
		id = i;
	}

	Node *close(Street *seg);

	//whether the target segment interact with this one
	//if so, put it in the connected map
	bool touch(Street *seg);



	/*
	 * commit a breadth-first search start from this
	 *
	 * */
	Street *breadthFirst(Street *target);

	// distance from a street to a point
	double distance(Point *p){
		assert(p);
		return distance_point_to_segment(p->x,p->y,start->x,start->y,end->x,end->y);
	}
};



class Map {
	vector<Node *> nodes;
	vector<Street *> streets;
	box *mbr = NULL;

	void connect_segments(vector<vector<Street *>> connections);
	void dumpTo(const char *path);
	void loadFrom(const char *path);
	void loadFromCSV(const char *path);
public:

	Map(){};
	Map(string path);
	vector<Street *> getStreets(){
		return streets;
	}

	~Map();
	box *getMBR();
	Street * nearest(Point *target);
	int navigate(vector<Point *> &result, Point *origin, Point *dest, double speed);
	void print_region(box *region = NULL);
	// clone the map for multiple thread support
	Map *clone();
};

#endif /* DATAGEN_MAP_H_ */
