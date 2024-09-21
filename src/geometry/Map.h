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

    Street(unsigned int i, Node *s, Node *e) :id(i) {
        assert(s&&e);
        start = s;
        end = e;
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

enum TripType{
    NOT_YET = 0,
    DRIVE = 1,
    WALK = 2,
    REST = 3
};

class Trace_Meta{
public:
    TripType type;
    Point loc;
    //int last_time;
    uint time_remaining = 0;

    int core = -2;
    Point end;

    double speed = 0;
    vector<Node *> trajectory;

//    Trace_Meta();
    ~Trace_Meta(){
        for (Node *node : trajectory) {
            delete node;
        }
        trajectory.clear();
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

    void check_Streets(){
        for(int i = 0; i < streets.size(); i++){
            if(streets[i]->id != i){       //stres[i]->id >= stres.size()
                cout << "stres id" << streets[i]->id << " " << i << endl;
                streets[i]->id = i;
            }
        }
    }

    ~Map();
    box *getMBR();
    Street * nearest(Point *target);
    //int navigate(vector<Point *> &result, Point *origin, Point *dest, double speed);
    void navigate(Point *positions, Trace_Meta & meta, int duration, int &count, uint num_objects, int obj);
    void print_region(box *region = NULL);
    // clone the map for multiple thread support
    Map *clone();
};


#endif /* DATAGEN_MAP_H_ */