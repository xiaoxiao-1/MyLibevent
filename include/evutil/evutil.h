#ifndef _EVUTIL_H
#define _EVUTIL_H
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
static int evutil_make_fdclose_on_exec(int fd)
{
    if (fcntl(fd, F_SETFD, 1) == -1)
    {
        printf("fcntl(%d)", fd);
        return -1;
    }
    return 0;
}
int evutil_socketpair(int family, int type, int protocol, int fd[2])
{
    return socketpair(family, type, protocol, fd);
}

int evutil_make_socket_nonblocking(int fd) //设置socket为非阻塞
{

    long flags;
    if ((flags = fcntl(fd, F_GETFL, NULL)) < 0)
    {
        printf("fcntl(%d, F_GETFL)", fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        printf("fcntl(%d, F_SETFL)", fd);
        return -1;
    }

    return 0;
}

int evutil_close_socket(int fd){
    return close(fd);
}


void evutil_timer_clear(struct timeval* tvp){
    tvp->tv_sec=tvp->tv_usec=0;
}

#define evtimer_set(ev, cb, arg)	event_set(ev, -1, 0, cb, arg)


#define	evutil_timercmp(tvp, uvp, cmp)							\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?							\
	 ((tvp)->tv_usec cmp (uvp)->tv_usec) :						\
	 ((tvp)->tv_sec cmp (uvp)->tv_sec))



#define	evutil_timercmp(tvp, uvp, cmp)							\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?							\
	 ((tvp)->tv_usec cmp (uvp)->tv_usec) :						\
	 ((tvp)->tv_sec cmp (uvp)->tv_sec))

#define	evutil_timersub(tvp, uvp, vvp)						\
	do {													\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {							\
			(vvp)->tv_sec--;								\
			(vvp)->tv_usec += 1000000;						\
		}													\
	} while (0)

#endif