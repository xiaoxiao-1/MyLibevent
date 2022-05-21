#ifndef _EVENT_INTERNAL_H_
#define _EVENT_INTERNAL_H_


#include "../datastruct/queue.h"
#include "../signal/signal.h"
#include "../IoMultiplexing/myepoll.h"
#include "../IoMultiplexing/IoMultiplex.h"
#include <queue>
#include <vector>

#define EVLIST_TIMEOUT	0x01    //从属于定时器队列
#define EVLIST_INSERTED	0x02    //从属于注册队列
#define EVLIST_SIGNAL	0x04      //没有使用
#define EVLIST_ACTIVE	0x08      //激活事件队列
#define EVLIST_INTERNAL	0x10    //内部使用的事件  信号处理的时候用到
#define EVLIST_INIT	0x80
#define EVLIST_ALL	(0xf000 | 0x9f)


#define EV_TIMEOUT	0x01
#define EV_READ		0x02
#define EV_WRITE	0x04
#define EV_SIGNAL	0x08
#define EV_PERSIST	0x10	/* Persistant event */


#define EVENT_SIGNAL(ev)	(int)(ev)->ev_fd
#define EVENT_FD(ev)		(int)(ev)->ev_fd


#define EVLOOP_ONCE	0x01
#define EVLOOP_NONBLOCK	0x02


TAILQ_HEAD (event_list, event);
TAILQ_HEAD (evkeyvalq, evkeyval);

struct evkeyval {
	TAILQ_ENTRY(evkeyval) next;

	char *key;
	char *value;
};

class IoMultiplex;
// event 调用一次的结构体

typedef struct event {
	TAILQ_ENTRY (event) ev_next;  //所有事件的链表
	TAILQ_ENTRY (event) ev_active_next;   //激活事件的链表
	TAILQ_ENTRY (event) ev_signal_next;//信号事件的链表    这里所说的都是读写事件和信号事件     时间事件是由最小堆来管理的
	unsigned int min_heap_idx;	/* for managing timeouts */   //最小堆来管理超时事件，只有超时事件会设置这个

	struct event_base *ev_base;//所属的reactor实例

	int ev_fd;//事件的文件句柄
	short ev_events;    //关心的事件类型  
	short ev_ncalls;   //事件就绪是调用callback的次数
	short *ev_pncalls;	/* Allows deletes in callback *///???????????????

	struct timeval ev_timeout;   //超时时间

	int ev_pri;		/* smaller numbers are higher priority */    //事件的优先级  小的优先级高

	void (*ev_callback)(int, short, void *arg);  //回调函数
	void *ev_arg;   //

	int ev_res;		/* result passed to event callback */
	int ev_flags;
} event;

struct timer_compare{
	bool operator()(event* a,event* b){
		if(a->ev_timeout.tv_sec==b->ev_timeout.tv_sec){
			return a->ev_timeout.tv_usec>b->ev_timeout.tv_usec;
		}else{
			return a->ev_timeout.tv_sec==b->ev_timeout.tv_sec;
		}
	}
};

struct event_once
{
	event ev;

	void (*cb)(int, short, void *);
	void *arg;
};
//这是一个reactor实例
class event_base
{
public:
	IoMultiplex *evsel;
	int event_count;		/* counts number of total events */
	int event_count_active; /* counts number of active events */

	int event_gotterm; /* Set to terminate loop */			 //这里是直接停止循环
	int event_break; /* Set to terminate loop immediately */ //这个好像是调用了event_term函数之后设置的标志位   当完成一轮循环之后就会停止循环

	/* active event management */
	struct event_list **activequeues; //
	int nactivequeues;				  //下面存储event尾队列的个数

	/* signal handling info */
	struct evsignal_info sig;

	struct event_list eventqueue;
	struct timeval event_tv;

	std::priority_queue<event*,std::vector<event*>,timer_compare> timer_heap;

	struct timeval tv_cache;

public:
	event_base();
	~event_base();

	void event_base_init();
	void event_base_free();
	int event_base_priority_init(int npriorities);
	int gettime(struct timeval *tp);
	int event_haveevents();
	int event_reinit();
	static void event_queue_remove(struct event_base *base, event *ev, int queue);
	static void event_queue_insert(struct event_base *base, event *ev, int queue);
	void timeout_process();
	void timeout_correct(struct timeval *tv);
	int timeout_next(struct timeval **tv_p);
	int event_base_set(event *ev); //事件刚创建之后设置base 和 privority
	static int event_del(event *ev);
	static int event_add(event *ev, const struct timeval *tv);
	int event_priority_init(int npriorities);
	static int event_priority_set(event *ev, int pri);
	static void event_set(event *ev, int fd, short events,
						  void (*callback)(int, short, void *), void *arg);
	int event_base_once(int fd, short events,
						void (*callback)(int, short, void *), void *arg, const struct timeval *tv);
	int event_once(int fd, short events,
				   void (*callback)(int, short, void *), void *arg, const struct timeval *tv);
	int event_base_loop(int flags);
	int event_loop(int flags);
	void event_process_active();

	int event_loopbreak();
	int event_base_loopbreak();

	int event_base_loopexit(struct event_base *event_base, const struct timeval *tv);

	int event_dispatch(void);
	int event_base_dispatch();
};
int _evsignal_set_handler(struct event_base *base, int evsignal,
						  void (*fn)(int));
int _evsignal_restore_handler(struct event_base *base, int evsignal);


#endif
