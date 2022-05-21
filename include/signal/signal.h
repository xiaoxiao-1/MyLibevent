#ifndef _EVSIGNAL_H_
#define _EVSIGNAL_H_
#include "../event_base/event_base.h"
typedef void (*ev_sighandler_t)(int);
typedef int sig_atomic_t ;
//存储信号有关的信息
struct evsignal_info {
	event ev_signal;		//socket_pair也需要触发事件被捕捉到，这个就是通过这个结构来被捕捉的
	int ev_signal_pair[2];		//管道                                                                                                                                                                   
	int ev_signal_added;
	volatile sig_atomic_t evsignal_caught;
	struct event_list evsigevents[NSIG];    //event的尾队列  设置尾队列数组是为了同样的信号可以被同时触发，一个链表表示同样的信号，
	sig_atomic_t evsigcaught[NSIG];  //原子模型   同一事件被触发的次数
	//保存之前的信号

	struct sigaction **sh_old;

	int sh_old_max; //？？？？
};
int evsignal_init(struct event_base *);
void evsignal_process(struct event_base *);
int evsignal_add(event *);
int evsignal_del(event *);
void evsignal_dealloc(struct event_base *);

#endif /* _EVSIGNAL_H_ */
