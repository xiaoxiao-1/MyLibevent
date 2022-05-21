#ifndef _MY_EPOLL_H
#define _MY_EPOLL_H
#include "./IoMultiplex.h"
#include "../event_base/event_base.h"
#define MAX_EPOLL_TIMEOUT_MSEC (35 * 60 * 1000)

#define INITIAL_NFILES 32
#define INITIAL_NEVENTS 32
#define MAX_NEVENTS 4096
struct evepoll
{
	event *evread;
	event *evwrite;
};

class myepoll : public IoMultiplex
{
private:
    struct evepoll *fds;		//这个应该是evepoll的数组
	int nfds;					//最大的fd    默认值是32 
	struct epoll_event *events; //分配的epoll_event
	int nevents;				// 分配的epollevent个数   默认值是32 
	int epfd;					// epoll_create 创建

public:
    myepoll();
    ~myepoll();
    void init(struct event_base *);
    int add( struct event *);
    int del( struct event *);
    int dispatch(struct event_base *,struct timeval *);
    void dealloc(struct event_base *);

    int epoll_recalc(struct event_base *base,int max);
};

#endif