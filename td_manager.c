/* Adapted for NGPT from linuxthreads/manager.c implementation          */
/* Copyright (C) 1996 Xavier Leroy (Xavier.Leroy@inria.fr)              */
/*                                                                      */
/* This program is free software; you can redistribute it and/or        */
/* modify it under the terms of the GNU Library General Public License  */
/* as published by the Free Software Foundation; either version 2       */
/* of the License, or (at your option) any later version.               */
/*                                                                      */
/* This program is distributed in the hope that it will be useful,      */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU Library General Public License for more details.                 */

                             /* ``Debuggers are a crutch for folks who
				  don't choose to learn the code.''
                                            -- Linus                    */

/* The "thread manager" thread: manages creation and termination of threads */

#define _PTHREAD_PRIVATE
#include "pthread.h"
#include "pth_p.h"
#include "allocation.h"
#include "schedule.h"

#ifdef _PTHREAD_QLOCK_DEFINED
struct _pthread_fastlock {
    long int __status;
    int __spinlock;
};
#endif

#undef _PTHREAD_PRIVATE
#include "td_manager.h"
#include "semaphore.h"

/* glibc internal calls used */
extern int __sched_get_priority_max (int __algorithm);
extern int __sched_get_priority_min (int __algorithm);
extern int __sched_setscheduler (__pid_t __pid, int __policy,
				 __const struct sched_param *__param);

/* Internal signals used by gdb and the thread manager */
int __pthread_sig_debug    = __SIGRTMIN + 3;
int __pthread_sig_restart  = __SIGRTMIN + 4;
int __pthread_sig_cancel   = __SIGRTMIN + 5;

/* Dummy thread to set main and manager handle slots to for gdb. Note
   that we overlay this with the real thread when we set them up. We 
   may find out later that some init of this is necessary.              */
static struct pth_st dummy_main;

/* Array of active threads. Entry 0 is reserved for the initial thread. */
struct pthread_handle_struct  __pthread_handles[MAX_TD_DEBUG_HANDLES] =
		     { {&dummy_main}, {&dummy_main}, /* All NULLs */ };

/* Lock for pthread_handles array access synchronization */
static pth_qlock_t  handles_lock;

/* For debugging purposes put the maximum number of threads in a variable.  */
const int __pthread_handles_max = MAX_TD_DEBUG_HANDLES;

/* Max number of threads supported, open ended. Will use POSIX value */
strong_alias(__pthread_handles_max, __linuxthreads_pthread_threads_max)

/* Number of active entries in __pthread_handles (used by gdb) */
volatile int __pthread_handles_num = 2;	   /* main & manager */

/* Whether to use debugger additional actions for thread creation
   (init 0, set to 1 by gdb) */
volatile int __pthread_threads_debug;

/* If libthread_db is involved to setup thread debugging 
   (init 0, set to 1 by libthread_db[td_ta_new]) */
volatile int __pthread_doing_debug;

/* The thread manager thread */
pth_t __pthread_manager_thread;

/* Lock for manager thread setup synchronization */
pth_qlock_t  manager_setup_lock;

/* The manager thread pipe to read requests */
static int __pthread_manager_pipe[2];

/* The manager thread stacksize (in K) */
#define MANAGER_STACKSIZE	32

/* File descriptor for sending requests to the thread manager. */
/* Initially -1, meaning that the thread manager is not running. */
int __pthread_manager_request = -1;

/* Other end of the pipe for sending requests to the thread manager. */
int __pthread_manager_reader;

/* Globally enabled events.  */
volatile td_thr_events_t __pthread_threads_events;

/* Pointer to thread descriptor with last event.  */
volatile pth_t __pthread_last_event;

/* Size of a pthread descriptor (pth thread tcb) */
const int __linuxthreads_pthread_sizeof_descr
				= sizeof(struct _pthread_descr_struct);

/* Forward declarations */
static void td_thread_create(struct pthread_request request,
					    int *retval, void **retdata);
static void td_thread_complete (pth_t new_thread);
static void td_handle_free(pth_t th_id);
static void td_handle_exit(pth_t issuing_thread, int exitcode);
extern void __pthread_wait_for_restart_signal(pth_t self);
static void __pthread_manager_adjust_prio(int thread_prio);
extern int  __pthread_manager_event(void *arg);
extern void __linuxthreads_create_event (void);
extern void __linuxthreads_death_event (void);
extern void __linuxthreads_reap_event (void);
extern void __pthread_manager_sighandler(int sig);
extern int __attribute__((noreturn)) __pthread_manager(void *arg);
static struct pthread_handle_struct *td_add_handle(pth_t t);
static void poke_natives(void);

/* The server thread managing requests for thread creation and termination */
int
__attribute__ ((noreturn))
__pthread_manager(void *arg)
{
    int reqfd = __pthread_manager_reader;
    struct pollfd ufd;
    sigset_t manager_mask;
    int n;
    struct pthread_request request;
    int  retcode;
    void *retdata;

    td_debug("__pthread_manager: starting\n");

    /* Block all signals except __pthread_sig_debug and SIGTRAP (??) */
    sigfillset(&manager_mask);
    sigdelset(&manager_mask, SIGTRAP);      /* for debugging purposes */
    sigdelset(&manager_mask, __pthread_sig_debug);
    pth_sc(sigprocmask)(SIG_SETMASK, &manager_mask, NULL);

    /* Raise our priority to match that of main thread. See comment
       in the function heder for current implementation.         */
    __pthread_manager_adjust_prio(__thread_prio_get(pth_main));

    /* Synchronize debugging of the thread manager */
    n = pth_sc(read)(reqfd, (char *)&request, sizeof(request));
    pth_assert(n == sizeof(request) && request.req_kind == REQ_DEBUG);
    td_debug("pthread_manager: got sync REQ_DEBUG for manager thread\n");

    ufd.fd = reqfd;
    ufd.events = POLLIN;
    /* Enter server loop */
    td_debug("__pthread_manager: entering server loop\n");
    while(1) {
	n = pth_sc(poll)(&ufd, 1, 2000);

	/* Read and execute request */
	if (n == 1 && (ufd.revents & POLLIN)) {
	    n = pth_sc(read)(reqfd, (char *)&request, sizeof(request));
	    pth_assert(n == sizeof(request));
	    td_debug("__pthread_manager: new request\n");
	    switch(request.req_kind) {
	      case REQ_CREATE:
		td_debug("__pthread_manager: REQ_CREATE\n");
		td_thread_create(request, &retcode, &retdata);
		(request.req_thread)->td_retcode = retcode;
		(request.req_thread)->td_retval  = retdata;
		break;
	      case REQ_FREE:
		td_debug("__pthread_manager: REQ_FREE\n");
		td_handle_free(request.req_args.free.thread_id);
		break;
	      case REQ_PROCESS_EXIT:
	      case REQ_MAIN_THREAD_EXIT:
		td_debug("__pthread_manager: REQ_EXIT\n");
		td_handle_exit(request.req_thread,
				    request.req_args.exit.code);
		/* NOTREACHED */
		break;
	      case REQ_POST:
		sem_post(request.req_args.post);
		break;
	      case REQ_DEBUG:
		td_debug("__pthread_manager: REQ_DEBUG\n");
		/* Make gdb aware of new thread and gdb will restart the
		   new thread when it is ready to handle the new thread. */
		if (__pthread_threads_debug)
		    raise(__pthread_sig_debug);
		break;
	      case REQ_KICK:
		/* This is just a prod to get the manager to reap some
		   threads right away, avoiding a potential delay at shutdown. */
		break;
	     }

	     /* Wake up requestor as we finished servicing the request */
	     if (request.req_thread_lock != NULL)
		_pth_release_lock(request.req_thread_lock, 1);
	}
	if (pth_watchdog_enabled == TRUE)
	    poke_natives();	   /* manager loop watchdog, for debug */
    }
}

int __pthread_manager_event(void *arg)
{
    td_debug("__pthread_manager_event\n");

    /* Get the lock the manager will free once all is correctly set up. */
    pth_acquire_lock (&manager_setup_lock);
    /* Free it immediately.  */
    pth_release_lock (&manager_setup_lock);

    return __pthread_manager(arg);
}

/* Process creation */

static void td_thread_create(struct pthread_request request,
					    int *retcode, void **retdata)
{
    pth_t t;

    td_debug("td_thread_create\n");

    pth_assert(request.req_kind == REQ_CREATE);

    /* Do the thread spawn with what we have as pthread.c does it. A
       wrinkle is that we use __pth_spawn vs pth_spawn, which allows
       the td_thread_complete call back to fix up thread init before
       the thread gets scheduled. We pass the full pthread_request 
       strucuture to td_thread complete to get what it needs, as the
       thread start args; this is fixed up to the real args later.   */
    t = (pth_t)pth_spawn_cb(request.req_args.create.attr,
			    request.req_args.create.fn,
			    (void *)&request, /* hack for td_thread_complete */
			    td_thread_complete);
    /* Return new thread address in requesting threads td_retval */
    *retcode = (t == NULL) ? EAGAIN : 0;
    *retdata = t;
}

/* Second half of td_thread_create, Invoked as a callback from 
 * pth_spawn after the thread is built but before the thread gets 
 * put on the new queue to be scheduled
 */
static void td_thread_complete (pth_t new_thread)
{
    struct pthread_request *reqp =
		  (struct pthread_request *)new_thread->start_arg;

    td_debug("td_thread_complete\n");

    pth_assert(reqp != NULL && reqp->req_kind == REQ_CREATE);

    /* Fixup new thread's start_args after handle_args hack */
    new_thread->start_arg = reqp->req_args.create.arg;

    /* Allocate new thread identifier, save index, set lock address   */
    new_thread->td_tid = new_thread;

    /* Raise priority of thread manager if needed (later) */
    // __pthread_manager_adjust_prio(new_thread->prio);
}

/* The functions contained here do nothing, they just return.  */

void
__linuxthreads_create_event (void)
{
    td_debug("__linuxthreads_create_event\n");
}

void
__linuxthreads_death_event (void)
{
    td_debug("__linuxthreads_death_event\n");
}

void
__linuxthreads_reap_event (void)
{
    td_debug("__linuxthreads_reap_event\n");
}

/* Try to free the resources of a thread when requested by pthread_join
   or pthread_detach on a terminated thread. */
static void td_handle_free(pth_t th_id)
{
    td_debug("td_handle_free\n");
}

/* Process-wide exit() */
static void td_handle_exit(pthread_descr issuing_thread, int exitcode)
{
    td_debug("td_handle_exit\n");
    /* Nothing else to do */
}

/* Handler for __pthread_sig_cancel in thread manager thread */
void __pthread_manager_sighandler(int sig)
{
    td_debug("__pthread_manager_sighandler\n");
}

/* Adjust priority of thread manager so that it always run at a 
   priority higher than all threads. For this cut we will use a
   bound native with the highest SCHED_FIFO priority, better later 
   when we support thread priorities/real-time. 		*/
void __pthread_manager_adjust_prio(int thread_prio)
{
    struct sched_param param;
    pth_descr_t descr;

    td_debug("__pthread_manager_adjust_prio\n");

    // if (thread_prio <= __pthread_manager_thread.prio) return;
    descr = pth_get_native_descr();
    // param.sched_priority =
    //	  thread_prio < __sched_get_priority_max(SCHED_FIFO) ?
    //				thread_prio + 1 : thread_prio;
    param.sched_priority = __sched_get_priority_max(SCHED_FIFO);
    __sched_setscheduler(descr->pid, SCHED_FIFO, &param);
    // __pthread_manager_thread.prio = thread_prio;
}

static void manager_complete(pth_t t)
{
#ifdef THREAD_DB
    if (__pthread_doing_debug) {
	__pthread_manager_thread = t;	/* covers sync_handles before spawn completes */
    }
#endif
}

/* Initialize the thread manager, called from pth_initialize() */
int __pthread_initialize_manager(void)
{
    struct pthread_request request;
    pth_attr_t t_attr;
    pth_descr_t descr = NULL;

    td_debug("__pthread_initialize_manager: starting\n");

    /* If basic initialization not done yet (e.g. we're called from a
       constructor run before our constructor), do it now */
    pth_init();

    /* Setup pipe to communicate with thread manager */
    if (pipe(__pthread_manager_pipe) == -1) {
	fprintf(stderr,"pthread_initialize_manager: unable to create pipe\n");
	pth_release_lock(&manager_setup_lock);
	return -1;
    }

    /* the current descriptor... */
    if ((descr = pth_get_native_descr()) == NULL) {
	fprintf(stderr,"pth_init: unable to retrieve initial descriptor !?!?!?\n");
	abort();
    }

    /* Set the manager thread's attributes */
    if ((t_attr = pth_attr_new()) == NULL) {
	fprintf(stderr,"pthread_initialize_manager: unable to allocate thread attribute\n");
	abort();
    }
    /* Schedulers always get maximum priority :), actually, real-time */
    pth_attr_set(t_attr, PTH_ATTR_SCHEDPOLICY,  SCHED_FIFO);
    pth_attr_set(t_attr, PTH_ATTR_PRIO,         xprio_get_max (SCHED_FIFO));
    pth_attr_set(t_attr, PTH_ATTR_NAME,         "**MANAGER**");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE,     FALSE);
    pth_attr_set(t_attr, PTH_ATTR_CANCEL_STATE, PTH_CANCEL_DISABLE);
    pth_attr_set(t_attr, PTH_ATTR_SCOPE,	PTHREAD_SCOPE_SYSTEM);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE,   MANAGER_STACKSIZE*1024);
    pth_attr_set(t_attr, PTH_ATTR_STACK_ADDR,   NULL);

    /* Start the thread manager */
    if (pth_main->td_report_events != 0)
    {
        /* It's a bit more complicated.  We have to report the creation of
	   the manager thread.  */
	int idx = __td_eventword (TD_CREATE);
	uint32_t mask = __td_eventmask (TD_CREATE);

	td_debug("__pthread_initialize_manager: doing thread events\n");

	if ((mask & (__pthread_threads_events.event_bits[idx]
		   | pth_main->td_events.eventmask.event_bits[idx])) != 0)
	{
	    td_debug("__pthread_initialize_manager: doing TD_CREATE event\n");

 	    /* Create and start the thread manager thread */
	    __pthread_manager_thread = (pth_t)pth_spawn_cb(t_attr,
    			  (void *(*)(void *))(__pthread_manager_event),
					NULL,
					manager_complete);
	    if (__pthread_manager_thread == NULL) {
		fprintf(stderr, "pthread_initialize_manager: can't create manager thread\n");
		pth_release_lock(&manager_setup_lock);
		pth_attr_destroy(t_attr);
		close(__pthread_manager_pipe[0]);
		close(__pthread_manager_pipe[1]);
		return FALSE;
	    }

	    td_debug("__pthread_initialize_manager: manager thread spawn ok\n");

	    /* Now fill in the information about the new thread in
	       the newly created thread's data structure.  We cannot let
	       the new thread do this since we don't know whether it was
	       already scheduled when we send the event.  */
	    __pthread_manager_thread->td_events.eventdata =
						  &__pthread_manager_thread;
	    __pthread_manager_thread->td_events.eventnum = TD_CREATE;
	    __pthread_last_event		= __pthread_manager_thread;

	    /* Get the handles array updated for the new thread */
	    __pthread_handles[1].h_descr  = __pthread_manager_thread;
	    __pthread_manager_thread->td_tid = __pthread_manager_thread;
	    __pthread_manager_thread->td_nr  = 1;

	    /* Now call the function which signals the event.  */
	    __linuxthreads_create_event ();
	}
    }
    else
    {
 	/* Create and start the thread manager thread */

	__pthread_manager_thread = pth_spawn_cb(t_attr,
    			  (void *(*)(void *))(__pthread_manager),
						NULL,
						manager_complete);
	if (__pthread_manager_thread == NULL) {
	    fprintf(stderr, "pthread_initialize_manager: can't create manager thread\n");
	    pth_release_lock(&manager_setup_lock);
	    pth_attr_destroy(t_attr);
	    close(__pthread_manager_pipe[0]);
	    close(__pthread_manager_pipe[1]);
	    return FALSE;
	}

	td_debug("__pthread_initialize_manager: manager thread spawn ok\n");

	/* Get the handles array updated for the new thread */
	__pthread_handles[1].h_descr  = __pthread_manager_thread;
	__pthread_manager_thread->td_tid = __pthread_manager_thread;
	__pthread_manager_thread->td_nr  = 1;
    }
    pth_attr_destroy(t_attr);
    __pthread_manager_thread->lastrannative = descr->tid;

    __pthread_manager_request = __pthread_manager_pipe[1]; /* writing end */
    __pthread_manager_reader  = __pthread_manager_pipe[0]; /* reading end */

    /* Make gdb aware of new thread manager */
    if (__pthread_doing_debug && __pthread_threads_debug) {
	td_debug("__pthread_initialize_manager: before raise");
	raise(__pthread_sig_debug);
	/* We suspend ourself and gdb will wake us up when it is
	   ready to handle us. */
	__pthread_wait_for_restart_signal(PTH_SELF());
    }

    /* Synchronize debugging of the thread manager */
    request.req_kind = REQ_DEBUG;
    pth_sc(write)(__pthread_manager_request,
			 (char *)&request, sizeof(struct pthread_request));
    td_debug("__pthread_initialize_manager: REQ_DEBUG submitted\n");

    /* release the lock as the manager should now be ready */
    pth_release_lock(&manager_setup_lock);

    /* Allow the manager to get scheduled in */
    pth_yield(__pthread_manager_thread);

    return 0;
}

void __pthread_wait_for_restart_signal(pth_t self)
{
    sigset_t mask1, maskorig;
    int      signo;

    td_debug("__pthread_wait_for_restart_signal: waiting\n");

    /* setup mask1 with restart signal to unblock   */
    sigemptyset(&mask1);
    sigaddset(&mask1, __pthread_sig_restart);
    /* update the thread's signal mask, saving orig */
    pth_sigmask(SIG_UNBLOCK, &mask1, &maskorig);
    
    signo = 0;
    do {
	/* Wait for signal */
	pth_sigwait_ev(&mask1, &signo, NULL);
    } while (signo !=__pthread_sig_restart);
    td_debug("__pthread_wait_for_restart_signal: restarted\n");
    pth_sigmask(SIG_SETMASK, &maskorig, NULL);
}

/* A thread is being cancelled, notify the thread manager */
void __pthread_signal_thread_event(pth_t current, pth_t target, int td_event)
{
    td_debug("__pthread_signal_thread_event\n");

    /* Use the dummy if we got a NULL, as we must be in init */
    if (current == NULL)
	current = __pthread_handles[0].h_descr;

    /* See whether we have to signal the event */
    if (current->td_report_events)
    {
	/* See whether td_event is in any of the mask.  */
	int idx = __td_eventword (td_event);
	uint32_t mask = __td_eventmask (td_event);

	if ((mask & (__pthread_threads_events.event_bits[idx]
	   | (current->td_events.eventmask.event_bits[idx]))) != 0) {
	    /* Yep, we have to signal the event  */
	    target->td_events.eventnum  = td_event;
	    target->td_events.eventdata = target;
	    __pthread_last_event = target;

	    /* Now call the function to signal the event.  */
	    switch (td_event)
	    {
		case TD_CREATE:
		    __linuxthreads_create_event();
		    break;
		case TD_DEATH:
		    __linuxthreads_death_event();
		    break;
		case TD_REAP:
		    __linuxthreads_reap_event();
		    break;
		default:
		    break;
	    }
	}
    }
    td_debug(__FUNCTION__ ": leaving\n");
}

/* Handler for the DEBUG signal.
   The debugging strategy is as follows:
   On reception of a REQ_DEBUG request (sent by new threads created to
   the thread manager under debugging mode), the thread manager throws
   __pthread_sig_debug to itself. The debugger (if active) intercepts
   this signal, takes into account new threads and continue execution
   of the thread manager by propagating the signal because it doesn't
   know what it is specifically done for. In the current implementation,
   the thread manager simply discards it. */
void __pthread_handle_sigdebug(int sig)
{
    td_debug("__pthread_handle_sig_debug\n");
}

void __pthread_handle_sigrestart(int sig)
{
    td_debug("__pthread_handle_sig_restart\n");

  /* Nothing */
}

/*
 * Sync the __pthread_handles array with NGPT's conception of the active
 * thread list. This should be called evey time we change the active pth
 * thread list to keep debugging in sync. This cleans up the original 
 * implementation in that we don't have to use the thread manager to do 
 * this for REQ_CREATE and REQ_FREE, it's auto done. No reaps are needed
 * by the thread manager. We keep the notion of the main and manager as 
 * slots 0 and 1 respectively.
 * 
 * Overhead may be an issue for large numbers of threads, if there are 
 * rapid changes to the pth active list but it should only occur on the
 * debug code paths.
 *
 * An optimization below attempts to call a slot updated if the thread 
 * handle is equal, intended for minor rebuilds. A concern is if a new 
 * thread can come back with an old thread address, if so we may have 
 * to not do this. Akin to this is how long a terminated thread hangs 
 * around, with Linuxthreads apparently for a while but here when the 
 * active list deletes it the thread is gone; will need to see if this
 * is an ok assumption or we need to keep terminated thread structures 
 * around for debugging.
 *
 * The code takes the native and handles locks during the rebuild.
 */
void __td_sync_handles(void)
{
    pth_t t;
    struct lnode_st *tq;
 
    td_debug(__FUNCTION__ "\n");

    /* lock native and handles for duration */
    pth_acquire_lock(&pth_native_lock);
    pth_acquire_lock(&handles_lock);
    /* walk pth active list and add threads to handles */
    __pthread_handles_num = 2;	     /* main + manager */
    for (tq = list_next (&pth_threads_queue);
         !list_node_is_tail (tq, &pth_threads_queue);
         tq = list_next (tq))
    {
	/* get the active thread tcb address */
        t = list_entry (tq, struct pth_st, thread_queue);
	/* add the thread handle */
	if (td_add_handle(t) == NULL)
	    break;
    }
    /* release native lock */
    pth_release_lock(&pth_native_lock);
    /* release handles lock */
    pth_release_lock(&handles_lock);
    td_debug2(__FUNCTION__ ": %d handles after sync\n", __pthread_handles_num);
}
strong_alias(__td_sync_handles, __pthread_sync_thread_debug)

static struct pthread_handle_struct *td_add_handle(pth_t t)
{
    int indx;

    /* If at max return NULL as we can't grow it */
    if (__pthread_handles_num == __pthread_handles_max)
	return NULL;
    /* Compute index for handles array, keeping main, and manager */
    indx = (t == pth_main) ? 0 : 
	   (t == __pthread_manager_thread) ? 1 : __pthread_handles_num;
    td_debug3(__FUNCTION__ ": for thread 0x%x index %d\n", t, indx);
    /* If not set up already, build the handle slot */
    if (__pthread_handles[indx].h_descr != t) {
	__pthread_handles[indx].h_descr  = t;
	t->td_nr   = indx;
    }

    /* If we added/updated a slot past 0/1 bump active count */
    if (indx > 1)
	__pthread_handles_num++;

    /* return with slot address */
    return &__pthread_handles[indx];
}

static void poke_natives(void)
{
    int i;
    char c = 1;
    if ((pqueue_total(&pth_RQ)+pqueue_total(&pth_NQ)) > 0) {
	pth_debug1("poke_natives: awake, work to be done");
	for (i = 1; pth_native_list[i].is_used; i++) {
	    if (!pth_native_list[i].is_bounded &&
		 pth_native_list[i].is_bound == FALSE)
		pth_sc(write)(pth_native_list[i].sigpipe[1],
						         &c, sizeof(char));
	}
    }
}
