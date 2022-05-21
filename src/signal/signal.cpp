#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include "../../include/event_base/event_base.h"
#include "../../include/signal/signal.h"
#include "../../include/evutil/evutil.h"

struct event_base *evsignal_base = NULL;

static void evsignal_handler(int sig);

/* Callback for when the signal handler write a byte to our signaling socket */
//从signal socket中读取一个字节的数据 统一事件源之后一个字节代表一种信号   需要进行回调
static void
evsignal_cb(int fd, short what, void *arg)
{
	static char signals[1];
	ssize_t n;
	n = recv(fd, signals, sizeof(signals), 0);
}

//作的事情无非就是： 创建管道 然后设置为close-on-exec
//注意evsignal_init并没有注册到event_base    只有当添加一个信号事件的的时候会自动检查是否添加   如果没有会注册到event_base
int evsignal_init(struct event_base *base)
{
	int i;

	//它上面说的是统一io事件，就是通过创建一个管道，每当检测到一个信号，然后向管道中写入相应的值，这样就可以唤醒循环，和socket的事件相似了。
	if (evutil_socketpair(
			AF_UNIX, SOCK_STREAM, 0, base->sig.ev_signal_pair) == -1)
	{
		return -1;
	}

	evutil_make_fdclose_on_exec(base->sig.ev_signal_pair[0]);
	evutil_make_fdclose_on_exec(base->sig.ev_signal_pair[1]);
	base->sig.sh_old = NULL;
	base->sig.sh_old_max = 0;
	base->sig.evsignal_caught = 0;
	memset(&base->sig.evsigcaught, 0, sizeof(sig_atomic_t) * NSIG); // NSIG linux可以接受到的信号的最大值  65
	/* initialize the queues for all events */
	for (i = 0; i < NSIG; ++i)
		TAILQ_INIT(&base->sig.evsigevents[i]); //每一个信号事件用一个尾队列来表示

	evutil_make_socket_nonblocking(base->sig.ev_signal_pair[0]); //设置为非阻塞

	event_set(&base->sig.ev_signal, base->sig.ev_signal_pair[1],
			  EV_READ | EV_PERSIST, evsignal_cb, &base->sig.ev_signal); //初始化信号event（事件处理器）
	base->sig.ev_signal.ev_base = base;
	base->sig.ev_signal.ev_flags |= EVLIST_INTERNAL; //设置这个event是信号事件（相当于一个标志？）  具体看EVLIST的所有类型

	return 0;
}

int _evsignal_set_handler(struct event_base *base,
						  int evsignal, void (*handler)(int))
{

	struct sigaction sa;

	struct evsignal_info *sig = &base->sig;
	void *p;

	if (evsignal >= sig->sh_old_max)
	{ // evsignal 是信号值 难道sh_old_max是历史中捕捉到的最大的信号值？
		int new_max = evsignal + 1;
		printf("%s: evsignal (%d) >= sh_old_max (%d), resizing\n",
			   __func__, evsignal, sig->sh_old_max);
		p = realloc(sig->sh_old, new_max * sizeof(*sig->sh_old));
		if (p == NULL)
		{
			printf("realloc\n");
			return (-1);
		}

		memset((char *)p + sig->sh_old_max * sizeof(*sig->sh_old),
			   0, (new_max - sig->sh_old_max) * sizeof(*sig->sh_old));

		sig->sh_old_max = new_max;
		sig->sh_old = (struct sigaction **)p;
	}

	/* allocate space for previous handler out of dynamic array */
	sig->sh_old[evsignal] = (struct sigaction *)malloc(sizeof *sig->sh_old[evsignal]);
	if (sig->sh_old[evsignal] == NULL)
	{
		printf("malloc\n");
		return (-1);
	}
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);

	if (sigaction(evsignal, &sa, sig->sh_old[evsignal]) == -1)
	{
		printf("sigaction\n");
		free(sig->sh_old[evsignal]);
		sig->sh_old[evsignal] = NULL;
		return (-1);
	}

	return (0);
}

//如果需要添加的事件是一个信号事件的时候就需要调用这个函数
//这个函数无非作的就是设置了信号的处理函数  然后插入到evsignal_info的信号事件队列中  然后注册到event_base中
//又想到为啥要用一个尾队列来存储一个event呢   下面给出的解释是multiple events may listen to the same signal（多个event来处理同一个信号) 为啥这样做？？？
int evsignal_add(event *ev)
{
	int evsignal;
	struct event_base *base = ev->ev_base;
	struct evsignal_info *sig = &ev->ev_base->sig;

	if (ev->ev_events & (EV_READ | EV_WRITE))
		printf(1, "%s: EV_SIGNAL incompatible use\n", __func__);
	evsignal = EVENT_SIGNAL(ev); //取出的这个是信号值  也就是说如果一个信号事件 那么他的ev_fd是信号值
	assert(evsignal >= 0 && evsignal < NSIG);
	if (TAILQ_EMPTY(&sig->evsigevents[evsignal]))
	{ //检查这个信号对应的event_list是否为空
		event_debug(("%s: %p: changing signal handler", __func__, ev));
		if (_evsignal_set_handler(
				base, evsignal, evsignal_handler) == -1)
			return (-1);

		/* catch signals if they happen quickly */
		evsignal_base = base;

		if (!sig->ev_signal_added)
		{
			if (event_add(&sig->ev_signal, NULL))
				return (-1);
			sig->ev_signal_added = 1;
		}
	}

	/* multiple events may listen to the same signal */
	TAILQ_INSERT_TAIL(&sig->evsigevents[evsignal], ev, ev_signal_next);

	return (0);
}

int _evsignal_restore_handler(struct event_base *base, int evsignal)
{
	int ret = 0;
	struct evsignal_info *sig = &base->sig;
	struct sigaction *sh;


	sh = sig->sh_old[evsignal];
	sig->sh_old[evsignal] = NULL;
	if (sigaction(evsignal, sh, NULL) == -1)
	{
		event_warn("sigaction");
		ret = -1;
	}
	free(sh);

	return ret;
}

int evsignal_del(event *ev)
{
	struct event_base *base = ev->ev_base;
	struct evsignal_info *sig = &base->sig;
	int evsignal = EVENT_SIGNAL(ev); //信号值

	assert(evsignal >= 0 && evsignal < NSIG);

	/* multiple events may listen to the same signal */
	TAILQ_REMOVE(&sig->evsigevents[evsignal], ev, ev_signal_next); //删除掉所有在信号事件队列中注册该信号的事件

	if (!TAILQ_EMPTY(&sig->evsigevents[evsignal])) //如果没有清空
		return (0);

	printf("%s: %p: restoring signal handler", __func__, ev);

	return (_evsignal_restore_handler(ev->ev_base, EVENT_SIGNAL(ev)));
}

static void
evsignal_handler(int sig)
{
	int save_errno = errno;

	if (evsignal_base == NULL)
	{
		printf(
			"%s: received signal %d, but have no base configured",
			__func__, sig);
		return;
	}

	evsignal_base->sig.evsigcaught[sig]++;	//这个信号被捕捉的次数+1
	evsignal_base->sig.evsignal_caught = 1; //设置现在有信号被捕捉到

	signal(sig, evsignal_handler); //  设置这个信号的处理函数本函数


	/* Wake up our notification mechanism */
	send(evsignal_base->sig.ev_signal_pair[0], "a", 1, 0); //发送一个"a"字符串
	errno = save_errno;
}
//处理信号的函数 无非就是遍历数组 得到每个信号事件发生的次数 然后激活该事件（设置该事件为ACTIVE，然后送到event_base的激活事件队列中）  如果不是永久事件那么需要从队列中删除这个事件（调用event_del)
void evsignal_process(struct event_base *base)
{
	struct evsignal_info *sig = &base->sig;
	event *ev, *next_ev;
	sig_atomic_t ncalls;
	int i;

	base->sig.evsignal_caught = 0;
	for (i = 1; i < NSIG; ++i)
	{
		ncalls = sig->evsigcaught[i];
		if (ncalls == 0)
			continue;
		sig->evsigcaught[i] -= ncalls;

		for (ev = TAILQ_FIRST(&sig->evsigevents[i]);
			 ev != NULL; ev = next_ev)
		{
			next_ev = TAILQ_NEXT(ev, ev_signal_next);
			if (!(ev->ev_events & EV_PERSIST))
				event_del(ev);
			event_active(ev, EV_SIGNAL, ncalls);
		}
	}
}

void evsignal_dealloc(struct event_base *base)
{
	int i = 0;
	//如果已经添加到事件队列  那么就从事件队列中删除  然后修改标志为没有
	if (base->sig.ev_signal_added)
	{
		event_del(&base->sig.ev_signal);
		base->sig.ev_signal_added = 0;
	}
	for (i = 0; i < NSIG; ++i)
	{
		if (i < base->sig.sh_old_max && base->sig.sh_old[i] != NULL)
			_evsignal_restore_handler(base, i);
	}

	if (base->sig.ev_signal_pair[0] != -1)
	{
		evutil_close_socket(base->sig.ev_signal_pair[0]);
		base->sig.ev_signal_pair[0] = -1;
	}
	if (base->sig.ev_signal_pair[1] != -1)
	{
		evutil_close_socket(base->sig.ev_signal_pair[1]);
		base->sig.ev_signal_pair[1] = -1;
	}
	base->sig.sh_old_max = 0;

	/* per index frees are handled in evsig_del() */
	if (base->sig.sh_old)
	{
		free(base->sig.sh_old);
		base->sig.sh_old = NULL;
	}
}
