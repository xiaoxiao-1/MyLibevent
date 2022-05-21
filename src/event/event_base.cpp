#include <sys/types.h>
#include <sys/time.h>

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "../../include/event_base/event_base.h"
#include "../../include/evutil/evutil.h"
#include "../../include/datastruct/time.h"

struct event_base *current_base = NULL;
extern struct event_base *evsignal_base;
static int use_monotonic = 1; //支持系统时间（绝对时间）

/* Handle signals - This is a deprecated interface */			   //这是一个弃用的接口
int (*event_sigcb)(void); /* Signal callback when gotsig is set */ //当得到信号的时候调用这个进行处理
volatile sig_atomic_t event_gotsig; /* Set in signal handler */	   //获得信号标志位

/* Prototypes */ //原型
static void event_queue_insert(struct event_base *, event *, int);
static void event_queue_remove(struct event_base *, event *, int);
static int event_haveevents(struct event_base *);

static void event_process_active(struct event_base *);

static int timeout_next(struct event_base *, struct timeval **);
static void timeout_process(struct event_base *);
static void timeout_correct(struct event_base *, struct timeval *);

event_base::event_base()
{
}

event_base::~event_base()
{
	delete evsel; //
}
//设置当前时间 1.从event_base的缓存时间获取   2.从系统时间获取
int event_base::gettime(struct timeval *tp)
{
	if (this->tv_cache.tv_sec)
	{
		*tp = this->tv_cache; //为了提升性能  存储了一个系统时间的cache
		return (0);
	}
	if (use_monotonic)
	{
		struct timespec ts;

		if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
			return (-1);

		tp->tv_sec = ts.tv_sec;
		tp->tv_usec = ts.tv_nsec / 1000;
		return (0);
	}
	return -1; //到结尾说明获取时间失败
}

void event_base::event_base_init(void)
{
	int i;

	event_sigcb = NULL; //信号回调函数
	event_gotsig = 0;	//

	gettime(&(this->event_tv)); // event_base设置时间

	TAILQ_INIT(&(this->eventqueue));  //初始化尾队列   也就是tqh_first=NULL  tqh_last=&tqh_first
	this->sig.ev_signal_pair[0] = -1; //信号管道设置为-1  表示还没有创建
	this->sig.ev_signal_pair[1] = -1;

	this->evsel = new myepoll;

	/* allocate a single active event queue */
	event_base_priority_init(1); ////？？？
}

//如果参数是NULL 那么默认是当前的event_base
void event_base::event_base_free()
{
	int i, n_deleted = 0;
	event *ev;

	if (this == current_base)
		current_base = NULL;

	/* Delete all non-internal events. */
	//开始遍历eventqueue  删除所有除了EVLIST_INTERNAL事件 但是实际上这个事件只有一个把。
	for (ev = TAILQ_FIRST(&(this->eventqueue)); ev;)
	{
		event *next = TAILQ_NEXT(ev, ev_next);
		if (!(ev->ev_flags & EVLIST_INTERNAL))
		{
			event_del(ev);
			++n_deleted;
		}
		ev = next;
	}
	//删除所有定时事件
	while ((ev = timer_heap.top()) != NULL)
	{
		event_del(ev);
		timer_heap.pop();
		++n_deleted;
	}

	for (i = 0; i < this->nactivequeues; ++i)
	{
		for (ev = TAILQ_FIRST(this->activequeues[i]); ev;)
		{
			event *next = TAILQ_NEXT(ev, ev_active_next);
			if (!(ev->ev_flags & EVLIST_INTERNAL))
			{
				event_del(ev);
				++n_deleted;
			}
			ev = next;
		}
	}

	if (n_deleted)
		printf("%s: %d events were still set in base",
			   __func__, n_deleted);

	this->evsel->dealloc(this);

	for (i = 0; i < this->nactivequeues; ++i)
		assert(TAILQ_EMPTY(this->activequeues[i]));

	assert(timer_heap.empty());

	for (i = 0; i < this->nactivequeues; ++i)
		free(this->activequeues[i]);
	free(this->activequeues);

	assert(TAILQ_EMPTY(&(this->eventqueue)));
}

/* reinitialized the event base after a fork */
int event_base::event_reinit()
{
	int res = 0;
	event *ev;

	/* check if this event mechanism requires reinit */
	if (!this->evsel->get_need_reinit())
		return (0);

	/* prevent internal delete */
	if (this->sig.ev_signal_added)
	{
		/* we cannot call event_del here because the base has
		 * not been reinitialized yet. */
		event_queue_remove(this, &this->sig.ev_signal,
						   EVLIST_INSERTED);
		if (this->sig.ev_signal.ev_flags & EVLIST_ACTIVE)
			event_queue_remove(this, &this->sig.ev_signal,
							   EVLIST_ACTIVE);
		this->sig.ev_signal_added = 0;
	}

	this->evsel->dealloc(this);
	this->evsel->init(this);
	TAILQ_FOREACH(ev, &this->eventqueue, ev_next)
	{
		if (this->evsel->add(ev) == -1)
			res = -1;
	}

	return (res);
}

int event_base::event_priority_init(int npriorities)
{
	return event_base_priority_init(npriorities);
}

int event_base::event_base_priority_init(int npriorities)
{
	int i;

	if (this->event_count_active) //如果有激活的事件  那么就直接退出  换句话说就是必须没有激活的事件才能设置优先级
		return (-1);

	if (npriorities == this->nactivequeues)
		return (0);

	if (this->nactivequeues)
	{
		//如果优先级个数 > 0
		for (i = 0; i < this->nactivequeues; ++i)
		{
			free(this->activequeues[i]);
		}
		free(this->activequeues);
	}
	//下面就是多分配几个存储优先级链表的格子
	/* Allocate our priority queues */
	this->nactivequeues = npriorities;
	this->activequeues = (struct event_list **)
		calloc(this->nactivequeues, sizeof(struct event_list *));
	if (this->activequeues == NULL)
		printf("%s: calloc\n", __func__);

	//激活事件队列的每一个等级是使用 event的尾队列
	for (i = 0; i < this->nactivequeues; ++i)
	{
		this->activequeues[i] = (struct event_list *)malloc(sizeof(struct event_list));
		if (this->activequeues[i] == NULL)
			printf("%s: malloc\n", __func__);
		TAILQ_INIT(this->activequeues[i]);
	}

	return (0);
}

int event_base::event_haveevents()
{
	return (this->event_count > 0); //总注册事件数目大于0 即有事件注册
}

void event_base::event_process_active()
{
	event *ev;
	struct event_list *activeq = NULL;
	int i;
	short ncalls;

	for (i = 0; i < this->nactivequeues; ++i)
	{
		if (TAILQ_FIRST(this->activequeues[i]) != NULL)
		{
			activeq = this->activequeues[i];
			break;
		}
	}

	assert(activeq != NULL);

	for (ev = TAILQ_FIRST(activeq); ev; ev = TAILQ_FIRST(activeq))
	{
		if (ev->ev_events & EV_PERSIST)
			event_queue_remove(this, ev, EVLIST_ACTIVE);
		else
			event_del(ev);

		/* Allows deletes to work */
		ncalls = ev->ev_ncalls;
		ev->ev_pncalls = &ncalls;
		while (ncalls)
		{
			ncalls--;
			ev->ev_ncalls = ncalls;
			(*ev->ev_callback)((int)ev->ev_fd, ev->ev_res, ev->ev_arg);
			if (event_gotsig || this->event_break)
				return;
		}
	}
}

/*
 * Wait continously for events.  We exit only if no events are left.
 */

int event_base::event_dispatch(void)
{
	return (event_loop(0));
}

int event_base::event_base_dispatch()
{
	return (event_base_loop(0));
}


/* not thread safe */
int event_base::event_loopbreak()
{
	return (event_base_loopbreak());
}

int event_base::event_base_loopbreak()
{
	this->event_break = 1;
	return (0);
}

/* not thread safe */

int event_base::event_loop(int flags)
{
	return event_base_loop( flags);
}

int event_base::event_base_loop(int flags)
{
	struct timeval tv;
	struct timeval *tv_p;
	int res, done;

	/* clear time cache */
	//为啥要有一个缓存time？？？？？？？？？？？？？
	this->tv_cache.tv_sec = 0;

	if (this->sig.ev_signal_added)
		evsignal_base = this;
	done = 0;
	while (!done)
	{
		/* Terminate the loop if we have been asked to */
		//开始loop时先检查gotterm 和 break标志  如果被置位  直接退出循环 否则开始正常处理
		if (this->event_gotterm)
		{
			this->event_gotterm = 0;
			break;
		}

		if (this->event_break)
		{
			this->event_break = 0;
			break;
		}

		/* You cannot use this interface for multi-threaded apps */
		//开始循环之后做的第一件事就是检查有没有信号的到来
		while (event_gotsig)
		{
			event_gotsig = 0;
			if (event_sigcb)
			{
				res = (*event_sigcb)();
				if (res == -1)
				{
					errno = EINTR;
					return (-1);
				}
			}
		}
		//时间处理这里没看明白
		timeout_correct(&tv);

		tv_p = &tv;
		if (!this->event_count_active && !(flags & EVLOOP_NONBLOCK))
		{
			//没有active event  并且 阻塞运行
			timeout_next(&tv_p);
		}
		else
		{
			/*
			 * if we have active events, we just poll new events
			 * without waiting.
			 */
			evutil_timer_clear(&tv);
		}

		/* If we have no events, we just exit */
		if (!event_haveevents())
		{
			printf("%s: no events registered.\n", __func__);
			return (1);
		}

		/* update last old time */
		gettime(&(this->event_tv));

		/* clear time cache */
		this->tv_cache.tv_sec = 0;

		res = this->evsel->dispatch(this,  tv_p);

		if (res == -1)
			return (-1);
		gettime(&this->tv_cache); //之前已经tv_sec已经赋0了所以这里是重新获得时间并且存储到缓存中

		timeout_process();

		if (this->event_count_active)
		{
			event_process_active();
			if (!this->event_count_active && (flags & EVLOOP_ONCE)) //如果没有激活即事件并且只循环一次  停止循环  否则还会继续循环
				done = 1;
		}
		else if (flags & EVLOOP_NONBLOCK) //如果非阻塞运行
			done = 1;
	}

	/* clear time cache */
	this->tv_cache.tv_sec = 0;

	printf("%s: asked to terminate loop.\n", __func__);
	return (0);
}


/* One-time callback, it deletes itself */

static void
event_once_cb(int fd, short events, void *arg)
{
	struct event_once *eonce = (struct event_once*) arg;

	(*eonce->cb)(fd, events, eonce->arg);
	free(eonce);
}

/* not threadsafe, event scheduled once. */
int event_base::event_once(int fd, short events,
			   void (*callback)(int, short, void *), void *arg, const struct timeval *tv)
{
	return event_base_once(fd, events, callback, arg, tv);
}

/* Schedules an event once */
int event_base::event_base_once(int fd, short events,
					void (*callback)(int, short, void *), void *arg, const struct timeval *tv)
{
	struct event_once *eonce;
	struct timeval etv;
	int res;

	/* We cannot support signals that just fire once */
	if (events & EV_SIGNAL) //不支持信号事件
		return (-1);

	if ((eonce = (struct event_once*)calloc(1, sizeof(struct event_once))) == NULL)
		return (-1);

	eonce->cb = callback;
	eonce->arg = arg;

	if (events == EV_TIMEOUT)
	{
		if (tv == NULL)
		{
			evutil_timer_clear(&etv);
			tv = &etv;
		}

		evtimer_set(&eonce->ev, event_once_cb, eonce);
	}
	else if (events & (EV_READ | EV_WRITE))
	{
		events &= EV_READ | EV_WRITE;

		event_set(&eonce->ev, fd, events, event_once_cb, eonce);
	}
	else
	{
		/* Bad event combination */
		free(eonce);
		return (-1);
	}

	res = event_base_set(&eonce->ev);
	if (res == 0)
		res = event_add(&eonce->ev, tv);
	if (res != 0)
	{
		free(eonce);
		return (res);
	}

	return (0);
}
/*
 * 用来初始化一个event事件处理器
 */
void event_base::event_set(event *ev, int fd, short events,
			   void (*callback)(int, short, void *), void *arg)
{
	/* Take the current base - caller needs to set the real base later */
	ev->ev_base = current_base;

	ev->ev_callback = callback;
	ev->ev_arg = arg;
	ev->ev_fd = fd;
	ev->ev_events = events;
	ev->ev_res = 0;
	ev->ev_flags = EVLIST_INIT;
	ev->ev_ncalls = 0;
	ev->ev_pncalls = NULL;

	min_heap_elem_init(ev);

	/* by default, we put new events into the middle priority */
	if (current_base)
		ev->ev_pri = current_base->nactivequeues / 2; //最大优先级的一半   也就是中等优先级
}
//刚刚经过set的事件加入到event_base中   并且再次设置为中等优先级
int event_base::event_base_set(event *ev)
{
	/* Only innocent events may be assigned to a different base */
	if (ev->ev_flags != EVLIST_INIT)
		return (-1);

	ev->ev_base = this;
	ev->ev_pri = this->nactivequeues / 2;

	return (0);
}

/*
 * Set's the priority of an event - if an event is already scheduled
 * changing the priority is going to fail.
 */
//设置evnet的优先级并且更新当前event_base的优先级
int event_base::event_priority_set(event *ev, int pri)
{
	if (ev->ev_flags & EVLIST_ACTIVE)
		return (-1);
	if (pri < 0 || pri >= ev->ev_base->nactivequeues)
		return (-1);

	ev->ev_pri = pri;

	return (0);
}

//向相应的列表中添加事件
int event_base::event_add(event *ev, const struct timeval *tv)
{
	struct event_base *base = ev->ev_base;
	int res = 0;

	printf(
		"event_add: event: %p, %s%s%scall %p\n",
		ev,
		ev->ev_events & EV_READ ? "EV_READ " : " ",
		ev->ev_events & EV_WRITE ? "EV_WRITE " : " ",
		tv ? "EV_TIMEOUT " : " ",
		ev->ev_callback);
	//判断是否设置了额外的标志位
	assert(!(ev->ev_flags & ~EVLIST_ALL));

	/*
	 * prepare for timeout insertion further below, if we get a
	 * failure on any step, we should not change any state.
	 */

	//最先开始对超时的事件作处理
	//分配最小堆的一个单元然后添加到最小堆
	//读，写，信号事件进行注册
	if ((ev->ev_events & (EV_READ | EV_WRITE | EV_SIGNAL)) &&
		!(ev->ev_flags & (EVLIST_INSERTED | EVLIST_ACTIVE)))
	{								  // event感兴趣的事件为读写信号，并且不在三种事件管理里面
		res = base->evsel->add(ev); // epoll或者其他的io多路复用机制进行监听，
		if (res != -1)
			event_queue_insert(base, ev, EVLIST_INSERTED); //如果epoll监听成功  那么加入到注册事件队列中
	}

	/*
	 * we should change the timout state only if the previous event
	 * addition succeeded.
	 */
	//
	if (res != -1 && tv != NULL)
	{
		//这里应该是超时事件
		struct timeval now;

		/*
		 * we already reserved memory above for the case where we
		 * are not replacing an exisiting timeout.
		 */
		if (ev->ev_flags & EVLIST_TIMEOUT)
			event_queue_remove(base, ev, EVLIST_TIMEOUT);

		/* Check if it is active due to a timeout.  Rescheduling
		 * this timeout before the callback can be executed
		 * removes it from the active list. */
		if ((ev->ev_flags & EVLIST_ACTIVE) &&
			(ev->ev_res & EV_TIMEOUT))
		{
			/* See if we are just active executing this
			 * event in a loop
			 */
			if (ev->ev_ncalls && ev->ev_pncalls)
			{
				/* Abort loop */
				*ev->ev_pncalls = 0;
			}

			event_queue_remove(base, ev, EVLIST_ACTIVE);
		}

		base->gettime(&now);
		evutil_timeradd(&now, tv, &ev->ev_timeout);

		printf(
			"event_add: timeout in %ld seconds, call %p\n",
			tv->tv_sec, ev->ev_callback);

		event_queue_insert(base, ev, EVLIST_TIMEOUT);
	}

	return (res);
}

int event_base::event_del(event *ev)
{
	struct event_base *base;

	void *evbase;

	printf("event_del: %p, callback %p\n",
				 ev, ev->ev_callback);

	/* An event without a base has not been added */
	if (ev->ev_base == NULL) //说明event还没有被添加到event_base    所以不需要删除
		return (-1);

	assert(!(ev->ev_flags & ~EVLIST_ALL)); //断言没有设置的事件

	/* See if we are just active executing this event in a loop */
	if (ev->ev_ncalls && ev->ev_pncalls)
	{
		/* Abort loop */
		*ev->ev_pncalls = 0;
	}

	if (ev->ev_flags & EVLIST_TIMEOUT)
		event_queue_remove(base, ev, EVLIST_TIMEOUT);

	if (ev->ev_flags & EVLIST_ACTIVE)
		event_queue_remove(base, ev, EVLIST_ACTIVE);

	if (ev->ev_flags & EVLIST_INSERTED)
	{
		event_queue_remove(base, ev, EVLIST_INSERTED);
		return (base->evsel->del(ev));
	}

	return (0);
}

//就是直接插入到激活事件队列中   但是这个res是什么意思
void event_active(event *ev, int res, short ncalls)
{
	/* We get different kinds of events, add them together */
	if (ev->ev_flags & EVLIST_ACTIVE)
	{
		ev->ev_res |= res;
		return;
	}

	ev->ev_res = res;
	ev->ev_ncalls = ncalls;
	ev->ev_pncalls = NULL;
	event_queue_insert(ev->ev_base, ev, EVLIST_ACTIVE);
}

int event_base::timeout_next(struct timeval **tv_p)
{
	struct timeval now;
	event *ev;
	struct timeval *tv = *tv_p;

	if ((ev = timer_heap.top()) == NULL)
	{
		/* if no time-based events are active wait for I/O */
		*tv_p = NULL;
		return (0);
	}

	if (gettime(&now) == -1)
		return (-1);

	if (evutil_timercmp(&ev->ev_timeout, &now, <=))
	{
		evutil_timer_clear(tv);
		return (0);
	}

	evutil_timersub(&ev->ev_timeout, &now, tv);

	assert(tv->tv_sec >= 0);
	assert(tv->tv_usec >= 0);

	printf("timeout_next: in %ld seconds\n", tv->tv_sec);
	return (0);
}

/*
 * Determines if the time is running backwards by comparing the current
 * time against the last time we checked.  Not needed when using clock
 * monotonic.
 */

void event_base::timeout_correct(struct timeval *tv)
{
	event **pev;
	unsigned int size;
	struct timeval off;

	if (use_monotonic)
		return;

	/* Check if time is running backwards */
	gettime(tv);
	if (evutil_timercmp(tv, &(this->event_tv), >=))
	{
		this->event_tv = *tv;
		return;
	}

	printf("%s: time is running backwards, corrected\n",
		   __func__);
	evutil_timersub(&(this->event_tv), tv, &off);

	/*
	 * We can modify the key element of the node without destroying
	 * the key, beause we apply it to all in the right order.
	 */
	// pev = timerhea;
	// size = timer_heap.size();
	// for (; size-- > 0; ++pev)
	// {
	// 	struct timeval *ev_tv = &(**pev).ev_timeout;
	// 	evutil_timersub(ev_tv, &off, ev_tv);
	// }


	/* Now remember what the new time turned out to be. */
	this->event_tv = *tv;
}

//超时event的处理
void event_base::timeout_process()
{
	struct timeval now;
	event *ev;

	if (timer_heap.empty()) //最小堆已空  直接返回
		return;

	gettime(&now);

	while ((ev = timer_heap.top()))
	{
		if (evutil_timercmp(&ev->ev_timeout, &now, >))
			break;

		/* delete this event from the I/O queues */
		event_del(ev);

		printf("timeout_process: call %p\n",
			   ev->ev_callback);
		event_active(ev, EV_TIMEOUT, 1);
	}
}

void event_base::event_queue_remove(struct event_base *base,event *ev, int queue)
{
if (!(ev->ev_flags & queue))
		printf("%s: %p(fd %d) not on queue %x", __func__,
			   ev, ev->ev_fd, queue);

	if (~ev->ev_flags & EVLIST_INTERNAL)    //内部事件不用--
		base->event_count--;

	ev->ev_flags &= ~queue;
	switch (queue) {
	case EVLIST_INSERTED:     //信号事件
		TAILQ_REMOVE(&base->eventqueue, ev, ev_next);
		break;
	case EVLIST_ACTIVE:       //激活事件
		base->event_count_active--;
		TAILQ_REMOVE(base->activequeues[ev->ev_pri],
		    ev, ev_active_next);
		break;
	case EVLIST_TIMEOUT:     //超时事件
		// min_heap_erase(&base->timeheap, ev);
		break;
	default:
		printf("%s: unknown queue %x", __func__, queue);
	}
}
//作的事情有1.判断是否重复插入(除 active event) 如果重复 那么日志记录错误 2.注册事件总数++  3. 根据插入的event的类型(ev_flag) 插入到相应的队列中
void event_base::event_queue_insert(struct event_base *base,event *ev, int queue)
{
	if (ev->ev_flags & queue) {  //该事件处理器已经含有该标志
		/* Double insertion is possible for active events */
		if (queue & EVLIST_ACTIVE)  //对于active event可以重复插入   不日志记录报错
			return;

		printf("%s: %p(fd %d) already on queue %x", __func__,
			   ev, ev->ev_fd, queue);
	}

	if (~ev->ev_flags & EVLIST_INTERNAL)   //如果不是evsignal_info的event    注册的event总数++
		base->event_count++;

	ev->ev_flags |= queue;
	switch (queue) {
	case EVLIST_INSERTED:
		TAILQ_INSERT_TAIL(&base->eventqueue, ev, ev_next);
		break;
	case EVLIST_ACTIVE:
		base->event_count_active++;
		TAILQ_INSERT_TAIL(base->activequeues[ev->ev_pri],
		    ev,ev_active_next);
		break;
	case EVLIST_TIMEOUT: {
		base->timer_heap.push(ev);
		break;
	}
	default:
		printf("%s: unknown queue %x", __func__, queue);
	}
}