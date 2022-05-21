#ifndef _IOMULTIPLEX_H
#define _IOMULTIPLEX_H
#include <sys/epoll.h>
#include <string>
#include "../event_base/event_base.h"

class IoMultiplex
{
protected:
    std::string name; // IO复用的名称
    int need_reinit;  //need to reinit

public:
    IoMultiplex();
    virtual ~IoMultiplex();
    virtual void init(struct event_base *);
    virtual int add(event *);
    virtual int del(event *);
    virtual int dispatch(struct event_base *,struct timeval *);
    virtual void dealloc(struct event_base *);
    int get_need_reinit() const{
        return need_reinit;
    }
};

#endif