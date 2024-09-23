/*
 * Map.cpp
 *
 *  Created on: Jan 11, 2021
 *      Author: teng
 */

#include "Map.h"
#include <time.h>
#include <sys/time.h>
#include <bits/stdc++.h>



Node *Street::close(Street *seg) {
    if(seg==NULL) {
        return NULL;
    }
    if(seg->start==start||seg->start==end) {
        return seg->start;
    }
    if(seg->end==end||seg->end==start) {
        return seg->end;
    }
    return NULL;
}

//whether the target segment interact with this one
//if so, put it in the connected map
bool Street::touch(Street *seg) {
    //if those two streets are connected, record the connection relationship
    //since one of the two streets is firstly added, it is for sure it is unique in others list
    if(close(seg)!=NULL) {
        connected.push_back(seg);
        seg->connected.push_back(this);
        return true;
    }
    return false;
}



/*
 * commit a breadth-first search start from this
 *
 * */
Street *Street::breadthFirst(Street *target) {

    queue<Street *> q;
    q.push(this);
    Street *dest = NULL;
    while(!q.empty()) {
        dest = q.front();
        q.pop();
        if(dest == target) {//found
            break;
        }
        for(Street *sc:dest->connected) {
            if(sc==this){
                continue;
            }
            if(sc->father_from_origin == NULL) {
                sc->father_from_origin = dest;
                q.push(sc);
            }
        }
    }
    if(dest == target){
        return dest;
    }else{
        return NULL;
    }
}



/*
 *
 * Map member functions
 *
 * */

Map::Map(string path){
    assert(path.size()>0);
    log("loading from %s",path.c_str());
    if(path.size()>4&&
       path.at(path.size()-4)=='.'&&
       path.at(path.size()-3)=='c'&&
       path.at(path.size()-2)=='s'&&
       path.at(path.size()-1)=='v'){
        this->loadFromCSV(path.c_str());
        this->dumpTo(path.substr(0, path.size()-4).c_str());
    }else{
        this->loadFrom(path.c_str());
    }
}

Map::~Map(){
    for(Street *s:streets){
        if(s)                   //but why
            delete s;
    }
    for(Node *p:nodes){
        delete p;
    }
    if(mbr){
        delete mbr;
    }
}
box *Map::getMBR(){
    if(!mbr){
        mbr = new box();
        for(Node *p:nodes){
            mbr->update(*p);
        }
    }
    return mbr;
}

/*
 * compare each street pair to see if they connect with each other
 * */
void Map::connect_segments(vector<vector<Street *>> connection) {
    log("connecting streets");
    struct timeval start = get_cur_time();

    int total = 0;
    for(vector<Street *> &connects:connection){
        for(int i=0;i<connects.size()-1;i++){
            for(int j=i+1;j<connects.size();j++){
                connects[i]->connected.push_back(connects[j]);
                connects[j]->connected.push_back(connects[i]);
                total += 2;
            }
        }
    }

    logt("touched %d streets %f",start,streets.size(),total*1.0/streets.size());

    // get the maximum connected component graph
    int *checked = new int[streets.size()];
    for(int i=0;i<streets.size();i++) {
        checked[i] = 0;
    }

    vector<Street *> cur_max_graph;
    int connected = 0;
    int cur_list = 0;
    while(connected<streets.size()){
        queue<Street *> q;
        Street *seed = NULL;
        vector<Street *> tmp;
        for(int i=0;i<streets.size();i++) {
            if(!checked[i]){
                seed = streets[i];
            }
        }
        q.push(seed);
        while(!q.empty()) {
            Street *cur = q.front();
            q.pop();
            if(checked[cur->id]==1){
                continue;
            }
            tmp.push_back(cur);
            checked[cur->id] = 1;
            connected++;
            for(Street *sc:cur->connected) {
                if(!checked[sc->id]) {
                    q.push(sc);
                }
            }
        }
        if(cur_max_graph.size()<tmp.size()){
            cur_max_graph.clear();
            cur_max_graph.insert(cur_max_graph.begin(), tmp.begin(), tmp.end());
        }
        tmp.clear();
    }

    // update the street list with only the connected streets
    unordered_set<Street *> included;
    for(Street *st:cur_max_graph){
        included.insert(st);
    }
    for(Street *st:streets){
        if(included.find(st)==included.end()){
            delete st;
        }
    }
    streets.clear();
    streets.insert(streets.end(), cur_max_graph.begin(), cur_max_graph.end());
    for(unsigned int i=0;i<streets.size();i++){
        streets[i]->id = i;
    }
    delete checked;
    logt("connected %d streets",start,streets.size());
}

void Map::dumpTo(const char *path) {

    ofstream wf(path, ios::out | ios::binary|ios::trunc);

    unsigned int num = nodes.size();
    wf.write((char *)&num, sizeof(num));
    for(Node *p:nodes){
        wf.write((char *)&p->x, sizeof(p->x));
        wf.write((char *)&p->y, sizeof(p->y));
    }
    num = streets.size();
    wf.write((char *)&num, sizeof(num));
    for(Street *s:streets){
        wf.write((char *)&s->start->id, sizeof(s->start->id));
        wf.write((char *)&s->end->id, sizeof(s->end->id));
        num = s->connected.size();
        wf.write((char *)&num, sizeof(num));
        for(Street *cs:s->connected){
            wf.write((char *)&cs->id, sizeof(cs->id));
        }
    }
    wf.close();
    log("dumped to %s",path);
}

void Map::loadFrom(const char *path) {

    struct timeval start_time = get_cur_time();
    ifstream in(path, ios::in | ios::binary);
    if(!in.is_open()){
        log("%s cannot be opened",path);
        exit(0);
    }

    vector<vector<unsigned int>> connected;

    unsigned int num;
    in.read((char *)&num, sizeof(num));
    nodes.resize(num);
    for(unsigned int i=0;i<num;i++){
        Node *n = new Node();
        n->id = i;
        in.read((char *)&n->x, sizeof(n->x));
        in.read((char *)&n->y, sizeof(n->y));
        nodes[i] = n;
    }
    in.read((char *)&num, sizeof(num));
    streets.resize(num);
    connected.resize(num);
    int slen = num;
    unsigned int id = 0;
    Node *start = NULL;
    Node *end = NULL;
    for(;id<slen;id++){
        in.read((char *)&num, sizeof(num));
        start = nodes[num];
        in.read((char *)&num, sizeof(num));
        end = nodes[num];
        in.read((char *)&num, sizeof(num));
        int len = num;
        vector<unsigned int> cons;
        for(int j=0;j<len;j++){
            in.read((char *)&num, sizeof(num));
            cons.push_back(num);
        }
        connected[id] = cons;
        assert(start&&end);
        streets[id] = new Street(id, start, end);
    }

    for(int i=0;i<slen;i++){
        for(unsigned int sl:connected[i]){
            streets[i]->connected.push_back(streets[sl]);
        }
        connected[i].clear();
    }
    connected.clear();
    in.close();
    getMBR();
    mbr->print();
    logt("loaded %d nodes %d streets from %s",start_time,nodes.size(), streets.size(), path);
}


void Map::loadFromCSV(const char *path){

    std::ifstream file(path);
    if(!file.is_open()){
        log("%s cannot be opened",path);
        exit(0);
    }
    std::string str;
    vector<string> fields;
    //skip the head
    std::getline(file, str);
    map<string, Node *> nodeset;
    char cotmp[256];
    vector<double> values;
    unsigned int id = 0;
    Node *start_point = NULL;
    Node *end_point = NULL;
    vector<vector<Street *>> connections;
    while (std::getline(file, str)){
        // Process str
        fields.clear();
        tokenize(str, fields);
        if(fields.size()<=2){
            continue;
        }
        string geo = fields[1];
        if(geo.size()<18){
            continue;
        }
        values = parse_double_values(geo);
        if(values.size()==0){
            continue;
        }
        assert(values.size()%2==0);

        double start[2] = {values[0], values[1]};
        double end[2] = {values[values.size()-2], values[values.size()-1]};

        sprintf(cotmp, "%.14f_%.14f", values[0], values[1]);

        if(nodeset.find(cotmp)==nodeset.end()){
            Node *p = new Node(start[0],start[1]);
            p->id = nodeset.size();
            nodes.push_back(p);
            vector<Street *> cs;
            connections.push_back(cs);
            nodeset[cotmp] = p;
        }
        start_point = nodeset[cotmp];
        sprintf(cotmp, "%.14f_%.14f", values[values.size()-2], values[values.size()-1]);
        if(nodeset.find(cotmp)==nodeset.end()){
            Node *p = new Node(end[0],end[1]);
            p->id = nodeset.size();
            nodes.push_back(p);
            vector<Street *> cs;
            connections.push_back(cs);
            nodeset[cotmp] = p;
        }
        end_point = nodeset[cotmp];
        Street *ns = new Street(id++,start_point,end_point);
        streets.push_back(ns);
        connections[start_point->id].push_back(ns);
        connections[end_point->id].push_back(ns);
    }
    nodeset.clear();
    connect_segments(connections);
    for(vector<Street *> &cs:connections){
        cs.clear();
    }
    connections.clear();
    log("%ld nodes and %ld streets are loaded",nodes.size(),streets.size());
    getMBR();
}

Map *Map::clone(){
    Map *nmap = new Map();
    nmap->nodes.resize(nodes.size());
    for(Node *p:nodes){
        Node *np = new Node(p);
        nmap->nodes[np->id] = np;
    }
    nmap->streets.resize(streets.size());
    for(Street *s:streets){
        Street *ns = new Street(s->id,nmap->nodes[s->start->id],nmap->nodes[s->end->id]);
        nmap->streets[s->id] = ns;
    }

    for(Street *s:streets){
        for(Street *cs:s->connected){
            nmap->streets[s->id]->connected.push_back(nmap->streets[cs->id]);
        }
    }
    nmap->getMBR();
    return nmap;
}


Street *Map::nearest(Point *target){
    assert(target);
    Street *ret;
    double dist = DBL_MAX;
    for(Street *st:streets) {
        double d = st->distance(target);
        if(d<dist){
            ret = st;
            dist = d;
        }
    }
    return ret;
}

/*
 * simulate the trajectory of the trip.
 * with the given streets the trip has covered, generate a list of points
 * that the taxi may appear at a given time
 *
 * */
//int Map::navigate(vector<Point *> &positions, Point *origin, Point *dest, double speed){
//
//    assert(origin);
//    assert(dest);
//    assert(speed>0);
//
//    // get the closest points streets to the source and destination points
//    vector<Street *> ret;
//    Street *o = nearest(origin);
//    Street *d = nearest(dest);
//
//    //conduct a breadth first query to get a list of streets
//    for(Street *s:streets) {
//        s->father_from_origin = NULL;
//    }
//    Street *s = o->breadthFirst(d);
//    assert(s && s==d);
//    do{
//        ret.push_back(s);
//        s = s->father_from_origin;
//    }while(s!=NULL);
//    reverse(ret.begin(),ret.end());
//
//    // convert the street sequence to point sequences
//    vector<Node *> trajectory;
//    Node *originnode = new Node(origin->x, origin->y);
//    Node *destnode = new Node(dest->x, dest->y);
//    trajectory.push_back(originnode);
//    Node *cur;
//    if(ret.size()==1){
//        cur = ret[0]->start;
//    }else{
//        cur = ret[0]->close(ret[1]);
//        // swap to other end
//        if(ret[0]->start==cur){
//            cur = ret[0]->end;
//        }else{
//            cur = ret[0]->start;
//        }
//    }
//
//    for(int i=0;i<ret.size();i++){
//        trajectory.push_back(cur);
//        if(ret[i]->start==cur){
//            cur = ret[i]->end;
//        }else{
//            cur = ret[i]->start;
//        }
//    }
//    trajectory.push_back(cur);
//    trajectory.push_back(destnode);
//
//    // quantify the street sequence to generate a list of
//    // points with fixed gap
//    double dist_from_origin = 0;
//    int point_index = 0;
//    for(int i=0;i<trajectory.size()-1;i++) {
//        Node *cur_start = trajectory[i];
//        Node *cur_end = trajectory[i+1];
//        double length = cur_start->distance(*cur_end, true);
//        double cur_dis = (point_index+1)*speed-dist_from_origin;
//        while(cur_dis<length) {
//            //have other position can be reported in this street
//            double cur_portion = cur_dis/length;
//            //now get the longitude and latitude and timestamp for current event and add to return list
//            Point *p = new Point(cur_start->x+(cur_end->x-cur_start->x)*cur_portion,
//                                 cur_start->y+(cur_end->y-cur_start->y)*cur_portion);
//            positions.push_back(p);
//            // move to next point
//            point_index++;
//            cur_dis += speed;
//        }
//        //move to next street
//        dist_from_origin += length;
//    }
//
//    trajectory.clear();
//    ret.clear();
//    delete originnode;
//    delete destnode;
//    return positions.size();
//}

void Map::navigate(Point *positions, Trace_Meta & meta, int duration, int &count, uint num_objects, int obj){

//    assert(origin);
//    assert(dest);
//    assert(speed>0);
    if(meta.trajectory.empty()){
        // get the closest points streets to the source and destination points
        vector<Street *> ret;
        Street *o = nearest(&meta.loc);
        Street *d = nearest(&meta.end);

        //conduct a breadth first query to get a list of streets
        for(Street *s:streets) {
            s->father_from_origin = NULL;
        }
        Street *s = o->breadthFirst(d);
        assert(s && s==d);
        do{
            ret.push_back(s);
            s = s->father_from_origin;
        }while(s!=NULL);
        reverse(ret.begin(),ret.end());

        // convert the street sequence to node sequences
        Node *originnode = new Node(meta.loc.x, meta.loc.y);
        Node *destnode = new Node(meta.end.x, meta.end.y);
        meta.trajectory.push_back(originnode);
        Node *cur;
        if(ret.size()==1){
            cur = ret[0]->start;
        }else{
            cur = ret[0]->close(ret[1]);
            // swap to other end
            if(ret[0]->start==cur){
                cur = ret[0]->end;
            }else{
                cur = ret[0]->start;
            }
        }

        for(int i=0;i<ret.size();i++){
            Node * temp = new Node(cur->x,cur->y);
            meta.trajectory.push_back(temp);
            if(ret[i]->start==cur){
                cur = ret[i]->end;
            }else{
                cur = ret[i]->start;
            }
        }
        Node * temp = new Node(cur->x,cur->y);
        meta.trajectory.push_back(temp);
        meta.trajectory.push_back(destnode);
        ret.clear();
    }


    // quantify the street sequence to generate a list of
    // points with fixed gap
    double dist_from_origin = 0;
    int point_index = 0;
    bool finish = true;
    while(meta.trajectory.size()>1){
        Node *cur_start = meta.trajectory[0];
        Node *cur_end = meta.trajectory[1];
        double length = cur_start->distance(*cur_end, true);
        double cur_dis = (point_index+1)*meta.speed-dist_from_origin;
        while(cur_dis<length) {
            //have other position can be reported in this street
            double cur_portion = cur_dis/length;
            assert(count<duration);
            //now get the longitude and latitude and timestamp for current event and add to return list
            positions[count*num_objects+obj].x = cur_start->x+(cur_end->x-cur_start->x)*cur_portion;
            positions[count*num_objects+obj].y = cur_start->y+(cur_end->y-cur_start->y)*cur_portion;
            count++;
            // move to next point
            cur_dis += meta.speed;
            point_index++;
            //if(count==duration&&meta.trajectory.size()>2){
            if(count==duration){
                finish = false;
                break;
            }
        }
        if(!finish){
            meta.loc = positions[(count-1)*num_objects+obj];
            delete meta.trajectory[0];
            meta.trajectory[0] = new Node(meta.loc.x, meta.loc.y);
            return;
        }
        delete meta.trajectory[0];
        meta.trajectory.erase(meta.trajectory.begin());
        //move to next street
        dist_from_origin += length;
    }
    if(finish){
        assert(meta.trajectory.size()==1);
        delete meta.trajectory[0];
        meta.trajectory.clear();
        meta.loc = meta.end;
    }

}

void Map::print_region(box *region){
    if(region==NULL){
        region = mbr;
    }
    printf("MULTILINESTRING(");
    bool first = true;
    for(int i=0;i<streets.size();i++){
        if(region->contain(*streets[i]->start)||region->contain(*streets[i]->end)){
            if(!first){
                printf(",");
            }else{
                first = false;
            }
            printf("(%f %f, %f %f)",streets[i]->start->x,streets[i]->start->y,streets[i]->end->x,streets[i]->end->y);
        }
    }
    printf(")\n");
}
