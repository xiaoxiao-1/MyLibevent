#include "../../include/IoMultiplexing/myepoll.h"
#include <cstdio>
#include "../../include/evutil/evutil.h"
#include <string.h>
myepoll::myepoll()
{
}

myepoll::~myepoll()
{
}

void myepoll::init(struct event_base *base)
{
    int epollfd; // epollfd   用于向其中注册事件

    if ((epollfd = epoll_create(32000)) == -1)
    {

        return;
    }

    evutil_make_fdclose_on_exec(epfd); //如果fcntl出错进行日志记录

    this->epfd = epollfd;

    this->events = (epoll_event *)malloc(INITIAL_NEVENTS * sizeof(struct epoll_event)); //初始化时分配了最多32个epoll_event   也就是说只能最多监听32个事件
    if (this->events == NULL)
    { //如果malloc失败，那么需要free前面已经分配的epollop结构体
        free(this->events);
        return;
    }
    this->nevents = INITIAL_NEVENTS;

    this->fds = (evepoll *)calloc(INITIAL_NFILES, sizeof(struct evepoll));
    if (this->fds == NULL)
    {
        free(this->events);
        return;
    }
    this->nfds = INITIAL_NFILES;

    evsignal_init(base); // evnet_base的信号事件也需要init
}

int myepoll::dispatch(struct event_base *base, struct timeval *tv)
{

    struct epoll_event *events = this->events;
    struct evepoll *evep;
    int i, res, timeout = -1;

    if (tv != NULL)
        timeout = tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000; //单位是ms   向上取整

    if (timeout > MAX_EPOLL_TIMEOUT_MSEC)
    {
        timeout = MAX_EPOLL_TIMEOUT_MSEC;
    }

    res = epoll_wait(this->epfd, events, this->nevents, timeout); //-1 无限等待时间    0 是立即返回

    if (res == -1)
    {
        if (errno != EINTR)
        { // EINTR是执行可能会阻塞的系统调用的时候发生的错误
            //如果errno不是EINTR 那么可能epoll wait出现严重错误
            printf("warn:epoll_wait");
            return (-1);
        }

        evsignal_process(base);
        return (0);
    }
    else if (base->sig.evsignal_caught)
    {
        evsignal_process(base);
    }

    //处理事件  其实就是送到激活事件队列中
    for (i = 0; i < res; i++)
    {
        int what = events[i].events;
        event *evread = NULL, *evwrite = NULL;
        int fd = events[i].data.fd;

        if (fd < 0 || fd >= this->nfds) //如果fd不符合这个条件就跳过  但是什么时候才能不符合这个条件呢。
            continue;
        evep = &(this->fds[fd]);

        if (what & (EPOLLHUP | EPOLLERR))
        {
            evread = evep->evread;
            evwrite = evep->evwrite;
        }
        else
        {
            if (what & EPOLLIN)
            {
                evread = evep->evread;
            }

            if (what & EPOLLOUT)
            {
                evwrite = evep->evwrite;
            }
        }

        if (!(evread || evwrite))
            continue;
        ///检测到有事件需要处理 然后送到活动事件队列中
        if (evread != NULL)
            event_active(evread, EV_READ, 1);
        if (evwrite != NULL)
            event_active(evwrite, EV_WRITE, 1);
    }
    //返回值和分配的nevents相同 说明已经全部用完了。。  需要再分配
    if (res == this->nevents && this->nevents < MAX_NEVENTS)
    {
        /* We used all of the event space this time.  We should
           be ready for more events next time. */
        int new_nevents = this->nevents * 2;
        struct epoll_event *new_events;

        new_events = realloc(this->events,
                             new_nevents * sizeof(struct epoll_event));
        if (new_events)
        {
            this->events = new_events;
            this->nevents = new_nevents;
        }
    }

    return (0);
}

int myepoll::add(event *ev)
{

    struct epoll_event epev = {0, {0}};
    struct evepoll *evep;
    int fd, op, events;

    if (ev->ev_events & EV_SIGNAL)
        return (evsignal_add(ev));

    fd = ev->ev_fd; //文件句柄fd作为数组索引？？？
    if (fd >= this->nfds)
    {
        /* Extent the file descriptor array as necessary */
        if (epoll_recalc(ev->ev_base, fd) == -1)
            return (-1);
    }
    evep = &this->fds[fd];
    op = EPOLL_CTL_ADD;
    events = 0;
    if (evep->evread != NULL)
    {
        events |= EPOLLIN;
        op = EPOLL_CTL_MOD;
    }
    if (evep->evwrite != NULL)
    {
        events |= EPOLLOUT;
        op = EPOLL_CTL_MOD;
    }

    if (ev->ev_events & EV_READ)
        events |= EPOLLIN;
    if (ev->ev_events & EV_WRITE)
        events |= EPOLLOUT;

    epev.data.fd = fd;
    epev.events = events;
    if (epoll_ctl(this->epfd, op, ev->ev_fd, &epev) == -1)
        return (-1);

    /* Update events responsible */
    if (ev->ev_events & EV_READ)
        evep->evread = ev;
    if (ev->ev_events & EV_WRITE)
        evep->evwrite = ev;

    return (0);
}

int myepoll::del(event *ev)
{

    struct epoll_event epev = {0, {0}};
    struct evepoll *evep;
    int fd, events, op;
    int needwritedelete = 1, needreaddelete = 1;

    if (ev->ev_events & EV_SIGNAL)
        return (evsignal_del(ev));

    fd = ev->ev_fd;
    if (fd >= this->nfds)
        return (0);
    evep = &this->fds[fd];

    op = EPOLL_CTL_DEL;
    events = 0;

    if (ev->ev_events & EV_READ)
        events |= EPOLLIN;
    if (ev->ev_events & EV_WRITE)
        events |= EPOLLOUT;

    if ((events & (EPOLLIN | EPOLLOUT)) != (EPOLLIN | EPOLLOUT))
    {
        if ((events & EPOLLIN) && evep->evwrite != NULL)
        {
            needwritedelete = 0;
            events = EPOLLOUT;
            op = EPOLL_CTL_MOD;
        }
        else if ((events & EPOLLOUT) && evep->evread != NULL)
        {
            needreaddelete = 0;
            events = EPOLLIN;
            op = EPOLL_CTL_MOD;
        }
    }

    epev.events = events;
    epev.data.fd = fd;

    if (needreaddelete)
        evep->evread = NULL;
    if (needwritedelete)
        evep->evwrite = NULL;

    if (epoll_ctl(this->epfd, op, fd, &epev) == -1)
        return (-1);

    return (0);
}

void myepoll::dealloc(struct event_base *base)
{
    evsignal_dealloc(base);
    if (this->fds)
        free(this->fds);
    if (this->events)
        free(this->events);
    if (this->epfd >= 0)
        close(this->epfd);
}

int myepoll::epoll_recalc(struct event_base *base, int max)
{
    if (max >= this->nfds)
    {
        struct evepoll *new_fds;
        int new_nfds;

        new_nfds = this->nfds;
        while (new_nfds <= max)
            new_nfds <<= 1;

        new_fds = (evepoll *)realloc(this->fds, nfds * sizeof(struct evepoll));
        if (fds == NULL)
        {
            printf("warning:realloc epoll evepoll");
            return (-1);
        }
        this->fds = new_fds;
        memset(fds + this->nfds, 0,
               (new_nfds - this->nfds) * sizeof(struct evepoll));
        this->nfds = new_nfds;
    }

    return (0);
}