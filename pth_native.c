/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Portions Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of NGPT, a non-preemptive thread scheduling
**  library which can be found at http://www.ibm.com/developer.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA.
**
**  pth_native.c: Pth native thread handling.
*/
                             /* ``If you can't do it in
                                  ANSI C, it isn't worth doing.'' 
                                                -- Unknown        */
#include "pth_p.h"
#include "debug.h"
#include "allocation.h"
#include "queue.h"
#include "pqueue.h"
#include "schedule.h"
#include "spinlock.h"
#include "useldt.h"
#include <sched.h>
#include <linux/unistd.h>
#ifdef THREAD_DB
#include <td_manager.h>
#endif

#ifndef CLONE_PARENT
#define CLONE_PARENT	0x00008000	/* Same parent? */
#endif
#ifndef CLONE_THREAD
#define CLONE_THREAD	0x00010000	/* Same thread group? */
#endif

#if cpp

/* Watchdog timer interval (secs), determines how often the watchdog wakes up. */
#ifndef WATCHDOG_TIMER_INTERVAL
#define WATCHDOG_TIMER_INTERVAL	2
#endif

/* After MAX_SPIN_COUNT iterations, we put the calling thread to sleep. */

#ifndef MAX_SPIN_COUNT
#define MAX_SPIN_COUNT 50
#endif

/* 
 * Duration of sleep (in nanoseconds) when we can't acquire a spinlock
 *  after MAX_SPIN_COUNT.
 */

#ifndef SPIN_SLEEP_DURATION
#define SPIN_SLEEP_DURATION 2000001
#endif

#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
extern int __clone2(int (*fn)(void *arg), void *thread_bottom, size_t stack_size, int flags, void *arg);
#endif

struct pth_descr_st {
    volatile int    is_used;		/* 1 is descr is used, 0 if not             */
    int		    native_errno;	/* descriptor specific errno value	    */
    pth_descr_t	    self;		/* self pointer                             */
    pid_t	    pid;		/* pid of native thread			    */
    pid_t	    tid;		/* tid of native thread			    */
    size_t	    stacksize;		/* stack size				    */
    char	   *true_stack;		/* the "true" un-twiddled stack pointer     */
    char	   *stack;		/* the stack passed to clone		    */
    pth_t	    sched;		/* scheduler for this thread		    */
    pth_t	    current;		/* the current thread on this native	    */
    pth_t	    nexttimer_thread;	/* the timer thread this task is waiting on */
    int		    sched_index;	/* the index of this descriptor in table    */
    volatile int    is_bound;		/* 1 if thread is bound, 0 otherwise        */
    int		    sigpipe[2];		/* internal signal occurence pipe	    */
    sigset_t	    sigpending;		/* mask of pending signals		    */
    int		    sigraised[2];	/* mask of raised signals		    */
    struct pqueue_st wait_queue;	/* per clone wait queue			    */
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
    jmp_buf	   *mctx_trampoline;	/* trampoline context			    */
    pth_mctx_t	    mctx_caller;	/* trampoline caller			    */
    sig_atomic_t    mctx_called;	/* whether the trampoline has been called   */
    pth_mctx_t	   *mctx_creating;	/* the context of the creator		    */
    void	  (*mctx_creating_func)(void);
					/* the function to be called after creation */
    sigset_t	    mctx_creating_sigs;	/* the signals used during creation	    */
#endif
    char	   *stack_top;		/* the bottom of the stack                  */
    pth_qlock_t    sched_lock;		/* per native scheduler lock		    */
    unsigned       is_running:1;	/* 1 if scheduler is running, 0 to exit	    */
    unsigned       is_bounded:1;	/* 1 if user-thread is bounded to this      */	
    unsigned       ptrfixed:1;		/* 1 if ptr was adjusted, 0 otherwise	    */
    unsigned       do_yield:1;		/* if true, visit the eventmgr loop	    */
    pth_t          bounded_thread;	/* save bounded user-thread		    */
    struct pqueue_st new_queue;		/* per clone new queue		            */
    struct pqueue_st ready_queue;	/* per clone ready queue		    */
#ifdef THREAD_DB
    pth_qlock_t    td_hold_lock;	/* Lock to hold native until gdb sees it    */
#endif
};

#endif	/* cpp */

intern int pth_watchdog_enabled = FALSE;
intern int pth_patch_checked = FALSE;
intern struct pth_descr_st pth_watchdog_descr;
intern int pth_clone_flags = CLONE_VM | CLONE_FS | CLONE_SIGHAND | CLONE_FILES | CLONE_THREAD;

intern pth_descr_t pth_alloc_native(int create_stack, int is_watchdog)
{
    pth_descr_t	descr = (is_watchdog) ? &(pth_watchdog_descr) : &(pth_native_list[pth_number_of_natives++]);
    char	*stack = NULL;
    size_t	pagesize = getpagesize();
    size_t	stack_granularity;

      /* BUG? there is something screwed up here. If I reset this to
      ** ==, as in the original, it overwrites memory somewhere else
      ** (in my case, some of the cache allocator data
      ** structures). 
      */
    
    if (pth_number_of_natives >= PTH_MAX_NATIVE_THREADS-1) {
        pth_number_of_natives--;
        return NULL;
    }
    /* 
     * Since part of this structure is configurable, we're gonna
     * initialize the whole thing to 0x0 to ensure no problems later on...
     */
    memset(descr, 0x0, sizeof(struct pth_descr_st));

    descr->sched_index = pth_number_of_natives - 1;

#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
    if (!is_watchdog && descr->sched_index != 0) {
      descr->mctx_trampoline = pth_malloc (sizeof(jmp_buf));
      if (descr->mctx_trampoline == NULL) {
	    if (!is_watchdog) pth_number_of_natives--;
	    return NULL;
	}
    }
#endif

    /* Initialize the queue... */
    pqueue_init (&descr->wait_queue);
    pqueue_init (&descr->new_queue);
    pqueue_init (&descr->ready_queue);
    pth_lock_init(descr->sched_lock);

    /* If we're not creating a stack, just return now... */
    if (!create_stack)
	return descr;

    /* Create the native stack... */
#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
    stack_granularity = 2*pagesize;
#else
    stack_granularity = pagesize;
#endif

    /*
     * Stack size is minimal since all threads including the scheduler
     * run on their own stacks.  We only really need enough to run the
     * pth_new_scheduler routine below but we need to allocate at least
     * 1 page, properly aligned to keep the clone api happy.
     */
    descr->stacksize = (1*stack_granularity)+8;

    /* Allocate the stack... */
    stack = pth_malloc (descr->stacksize);
    if (stack == NULL) {
	if (!is_watchdog) pth_number_of_natives--;
	return NULL;
    }

    /* Save the unmodified ptr before we align it...  free will use this later... */
    descr->true_stack = stack;

    /* Align it if necessary... */
    if ((long)stack % 16L != 0L)
	stack = ((char *)(stack + 8));
    
    /* Save the possibly modified stack ptr... */
    descr->stack = stack;

    /* Calculate the stack bottom... */
    descr->stack_top = ((char *)(stack + descr->stacksize));

    descr->self = descr;

    return descr;
}

#ifdef THREAD_DB
static void pth_report_lwp_state_change(pth_descr_t descr, int td_event)
{
    struct pth_st lwp_report_thread;

    /* Setup needed fields in the lwp_report_thread. This is 
       a kludge, but should let us report the new lwp to gdb 
       before it hits a thread event. Gdb is already tossing
       threads with td_tid == 0, as will be the case here, 
       as lwp_report_thread is static NULL'ed.

       Fill in the boundnative with the descriptor for the 
       new lwp and do the event reporting.		    	     */
    memset(&lwp_report_thread, 0, sizeof(struct pth_st));
    lwp_report_thread.boundnative = descr;

    /* Signal the lwp create event.  The debugger sees it at 
       this point and attaches the new native as a lwp.	     */
    __pthread_signal_thread_event(pth_main, &lwp_report_thread, td_event);
}
#endif

intern pth_descr_t pth_new_native(int scope, pth_t t)
{
    pid_t	native_pid;
    pth_descr_t	descr = NULL;
    
    pth_acquire_lock(&pth_native_lock); 
    if (!pth_first_native.is_running) {
        pth_release_lock(&pth_native_lock);
        return NULL;
    }
    if (scope == PTH_SCOPE_SYSTEM) {
        /* Re-use the native thread, if available */
        int slot = 1;
        while (pth_native_list[slot].is_used) {
            descr = &pth_native_list[slot++];
            if (descr->is_bounded && !descr->bounded_thread) {
                pth_release_lock(&pth_native_lock);
                return descr;
            }
        }
    }
    if ((descr = pth_alloc_native(TRUE, FALSE)) == NULL) {
	pth_release_lock(&pth_native_lock);
	return NULL;
    }
    if (t)  descr->bounded_thread = t;
    pth_release_lock(&pth_native_lock);

    if (scope == PTH_SCOPE_SYSTEM)
        descr->is_bounded = TRUE;

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Set lock to hold native until event report completes */
	pth_acquire_lock (&descr->td_hold_lock);
    }
#endif

    /* Ready to clone... */
#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
    /* 
     * clone2() takes slightly different parameters than clone, such the stack bottom
     * rather than the stack top and the stack size.  This is actually easier to understand
     * from a programming standpoint but it is not available on all versions of the Linux
     * kernel.
     */
    native_pid = __clone2(pth_new_scheduler, descr->stack, descr->stacksize, pth_clone_flags, descr);
#else
    /*
     * Note: clone() take the *top* of the stack as it's parameter.
     */
    native_pid = clone(pth_new_scheduler, descr->stack_top, pth_clone_flags, descr);
#endif

    if (native_pid == -1) {
	pth_number_of_natives--;
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
	pth_free_mem (descr->mctx_trampoline, sizeof (jmp_buf));
#endif
	pth_free_mem (descr->true_stack, descr->stacksize);
	descr->is_used = FALSE;
	return NULL; /* oops! it failed */
    }

    pth_debug3("pth_new_native: after clone native_pid %d number_of_natives %d\n", native_pid, pth_number_of_natives);


#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Wait for the new clone to fill in some native descr fields
	   that gdb/libthread_db references in the LWP discovery.    */
	while (descr->is_used != TRUE)
		;
	/* signal the debugger that a new LWP exists		     */
	pth_report_lwp_state_change(descr, TD_CREATE);

	/* Unlock the native to continue.			     */
	pth_release_lock(&descr->td_hold_lock);
    }
#endif

    /* Return the native descr address... */
    return descr;
}

intern pid_t pth_new_watchdog(void)
{
    pid_t	native_pid;
    pth_descr_t	descr = NULL;
    pid_t	tid = k_gettid();
    
    _pth_acquire_lock(&pth_native_lock, tid);
    if (pth_watchdog_enabled) {
        _pth_release_lock(&pth_native_lock, tid);
        return -1;
    }
    pth_watchdog_enabled = TRUE;
    _pth_release_lock(&pth_native_lock, tid);

    if ((descr = pth_alloc_native(TRUE, TRUE)) == NULL) 
	return -1;

    /* Ready to clone... */
#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
    /* 
     * clone2() takes slightly different parameters than clone, such the stack bottom
     * rather than the stack top and the stack size.  This is actually easier to understand
     * from a programming standpoint but it is not available on all versions of the Linux
     * kernel.
     */
    native_pid = __clone2(pth_watchdog, descr->stack, descr->stacksize, pth_clone_flags, descr);
#else
    /*
     * Note: clone() take the *top* of the stack as it's parameter.
     */
    native_pid = clone(pth_watchdog, descr->stack_top, pth_clone_flags, descr);
#endif

    if (native_pid == -1) {
	pth_free_mem (descr->true_stack, descr->stacksize);
	descr->is_used = FALSE;
    }

    /* Return the native pid... */
    return native_pid;
}

intern void pth_drop_natives(void)
{
    int	    slot;
    pid_t   tid = pth_first_native.tid;
    pth_descr_t descr;
    int native_counts;

    /* Signal the watchdog to terminate... */
    pth_watchdog_enabled = FALSE;

    /* Finish watchdog termination... */
    if (pth_watchdog_descr.is_used) {
	pth_watchdog_descr.is_used = FALSE;
	pth_free_mem (pth_watchdog_descr.true_stack, pth_watchdog_descr.stacksize);
	if (pth_watchdog_descr.pid == getpid())
	    k_tkill(pth_watchdog_descr.tid, SIGKILL);
    }

    /* wait until all native threads are actually started... */
    _pth_acquire_lock(&pth_native_lock, tid);
    pth_first_native.is_running = FALSE;
    native_counts = pth_number_of_natives;
    _pth_release_lock(&pth_native_lock, tid);

    slot = 1;
    /* for each running cloned task, call cleanup to exit */
    while (slot < native_counts) {
	descr = &pth_native_list[slot];
	_pth_acquire_lock(&descr->sched_lock, tid);
	/* lock is used to synchronize with scheduler */
	if (descr->is_running) {
#ifdef THREAD_DB
	    /* Report the impending demise of the native, before
	       it happens. This keeps gdb up to date and happy.   */
	    pth_report_lwp_state_change(descr, TD_DEATH);
#endif
	    descr->is_running = FALSE;
	    if (descr->is_bounded && (descr->current != descr->sched)) {
		k_tkill(descr->tid, SIGKILL);
		if (pth_key_lock.owner == descr->tid)
		    _pth_release_lock(&pth_key_lock, descr->tid);
		pth_tcb_free(descr->current);
	    }
    	    pth_cleanup_native(slot);
	}
	_pth_release_lock(&descr->sched_lock, tid);
        slot++;
    }
    slot = 0;
    /* call cleanup for first native task */
    _pth_acquire_lock(&pth_native_lock, tid);
    pth_cleanup_native(slot);
    _pth_release_lock(&pth_native_lock, tid);
}

intern void pth_cleanup_native(int slot)
{
    struct timespec tm;
    pid_t tid;
    pth_descr_t descr;

    /* Cleanup for the native threads */
    if (pth_native_list[slot].is_used) {
	descr = &pth_native_list[slot];

	tid = pth_first_native.tid;

	pth_debug3("pth_cleanup_native: cleaning up pid %d, tid %d", 
		pth_native_list[slot].pid, pth_native_list[slot].tid);

	/* for the first native: loop and wait until all native threads exit... */
	while (!slot && pth_number_of_natives > 1) {
		_pth_release_lock(&pth_native_lock, tid);
                tm.tv_sec = 0;
                tm.tv_nsec = 1000001;
                k_nanosleep(&tm, NULL);
                _pth_acquire_lock(&pth_native_lock, tid);
	}

	/* wait til control is back to scheduler.... */
	if (slot != 0 && !descr->is_bounded) {
	    while (descr->current != descr->sched) {
	        _pth_release_lock(&descr->sched_lock, tid);
                tm.tv_sec = 0;
                tm.tv_nsec = 1000001;
                k_nanosleep(&tm, NULL);
		_pth_acquire_lock(&descr->sched_lock, tid);
		if (descr->is_used == FALSE)
		    return;
	    }
	}

	/* Now close the signal pipe... */
	close(pth_native_list[slot].sigpipe[0]);
	close(pth_native_list[slot].sigpipe[1]);

	/* At this point nothing can be current, clear it out... */
	if (descr->sched_index != 0) descr->current = NULL;

	/* Clean up the scheduler */
	pth_tcb_free(pth_native_list[slot].sched);

	/*
         * Free clone stack...
         * an' finally, clean out the table entry...
         */
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
	pth_free_mem (pth_native_list[slot].mctx_trampoline, sizeof(jmp_buf));
#endif
	if (slot == 0)
		return;

	pth_free_mem (pth_native_list[slot].true_stack, pth_native_list[slot].stacksize);
	pth_native_list[slot].is_used = FALSE;
	pth_number_of_natives--;

        /* First kill the native... If not slot 0 ;-) */
        if (pth_native_list[slot].tid && (pth_native_list[slot].pid == getpid()))
		k_tkill(pth_native_list[slot].tid, SIGKILL);
    
	/* release global locks, if held by this (dead) native */
	if (pth_key_lock.owner == descr->tid)
		_pth_release_lock(&pth_key_lock, descr->tid);
	if (pth_usr1_lock.owner == descr->tid)
		_pth_release_lock(&pth_usr1_lock, descr->tid);
	if (spinlock_locker (&pth_DQ.lock) == descr)
		spin_unlock (&pth_DQ.lock, descr);

	FREE_NATIVE(descr, descr->sched_index);
	
    }

    return;
}

intern void pth_dumpnatives(void)
{
    int n = 0;
    int i = 1;

    debugmsg ("| Native Thread Queue:\n");
    for (n = 0; n < PTH_MAX_SCHEDULERS; n++) {
	if (pth_native_list[n].is_used == FALSE)
	    break;
        debugmsg ("|   %d. native thread 0x%lx pid = %d, tid = %d\n",
		i++, (unsigned long)&pth_native_list[n], pth_native_list[n].pid, pth_native_list[n].tid);
    }
    if (pth_watchdog_descr.is_used)
	debugmsg ("|   %d. native thread 0x%lx pid = %d, tid = %d (WATCHDOG)\n",
		i++, (unsigned long)&pth_watchdog_descr, pth_watchdog_descr.pid, pth_watchdog_descr.tid);
    return;
}

intern int pth_native_yield(void)
{
    return sched_yield();
}

intern int pth_wakeup_anative(void)
{
    pth_descr_t ds;
    int i = 1;
    char c = (int)1;

    ds = &pth_native_list[i++];
    while (ds->is_used) {
	/* if a native currently waiting and is not bounded to a thread, wakeup */
	if (!ds->is_bound && !ds->is_bounded) {
	    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
	    return 1;
	}
	ds = &pth_native_list[i++];
    }
    return 0;
}

intern int pth_watchdog(void *arg)
{
    int i;
    char c = (int)1;
    struct timespec tm;
    pth_descr_t descr = NULL;

    pth_debug1("pth_watchdog: starting new watchdog");

    /* set the tid of this watchdog... */
    descr = (pth_descr_t)arg;
    descr->pid = getpid();
    descr->tid = k_gettid();
    descr->is_bound = 1;
    descr->is_used = TRUE;

    pth_debug2("pth_watchdog: pid is -> %d\n", descr->tid);

    while (pth_watchdog_enabled == TRUE) {
        tm.tv_sec = WATCHDOG_TIMER_INTERVAL;
	tm.tv_nsec = 0;
	k_nanosleep(&tm, NULL);
	pth_debug3("pth_watchdog: awake, tid = %d\nActive threads = %d", 
		descr->tid, pth_active_threads);

	if ((__pqueue_total (&pth_RQ) + pqueue_total (&pth_NQ)) > 0) {
	    pth_debug1("pth_watchdog: awake, work to be done");
	    for (i = 1; pth_native_list[i].is_used; i++) {
		if (!pth_native_list[i].is_bounded && pth_native_list[i].is_bound == FALSE)
		    pth_sc(write)(pth_native_list[i].sigpipe[1], &c, sizeof(char));
	    }
	}
    }

    pth_debug1("pth_watchdog: exiting");
    descr->is_used = FALSE;
	
    return 0;
}

intern void pth_exit_scheduler(pth_descr_t descr, int locked)
{
	pth_t t;

	if (descr->is_running || descr == &pth_first_native) {
		printf("pth_scheduler_exit: scheduler is still running...\n");
		abort();
	}
/* #warning FIXME: Should not we send these tasks to other natives? */
	spin_lock(&descr->ready_queue.lock, descr, NULL);
        while (__pqueue_total(&descr->ready_queue) != 0){
          t = thread_by_node(__pqueue_first_node (&descr->ready_queue));
          __pqueue_delete_node(&t->node);
          pth_tcb_free(t);
        }
	spin_unlock(&descr->ready_queue.lock, descr);

	close(descr->sigpipe[0]);
        close(descr->sigpipe[1]);
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
	pth_free_mem (descr->mctx_trampoline, sizeof(jmp_buf));
#endif
	if (!locked) {
        	_pth_acquire_lock(&pth_native_lock, descr->tid);
	}
	descr->is_used = FALSE;
	pth_number_of_natives--;
	_pth_release_lock(&pth_native_lock, descr->tid);
	_pth_release_lock(&descr->sched_lock, descr->tid);
	FREE_NATIVE(descr, descr->sched_index);
        pth_sc(exit)(0);
}

intern int pth_new_scheduler(void *arg)
{
    pth_attr_t	t_attr;
    pth_descr_t descr = NULL;
    pid_t tid = k_gettid();

    pth_debug1("pth_new_scheduler: starting new scheduler");

    /* set the tid of this scheduler... */
    descr = (pth_descr_t)arg;
    INIT_NATIVE_SELF(descr, descr->sched_index);
    _pth_acquire_lock(&descr->sched_lock, tid); 
    descr->pid = getpid();
    descr->tid = tid;
    descr->is_bound = 1;
    descr->is_used = TRUE;

    /* if first native is exiting, no need to continue */
    /* create the internal signal pipe for this scheduler... */
    if ((pth_exit_count > 0) || (pipe(descr->sigpipe) == -1)) {
	pth_t t;
        pth_debug1("pth_new_schdeuler: Either process is exiting or pipe() failed...");
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
	pth_free_mem (descr->mctx_trampoline, sizeof(jmp_buf));
#endif
	spin_lock(&descr->ready_queue.lock, descr, NULL);
        while (__pqueue_total(&descr->ready_queue) != 0) {
                t = thread_by_node(__pqueue_first_node(&descr->ready_queue));
                __pqueue_delete_node(&t->node);
                pth_threads_count--;
                pth_free_mem (t->true_stack, t->stacksize+8);
                if (t->ptrfixed)
                        t = (pth_t)((char *)((char *)t - 8));
                pth_free_mem (t, sizeof (struct pth_st)+8);
        }
	spin_unlock(&descr->ready_queue.lock, descr);
	descr->is_used = FALSE;
	pth_number_of_natives--;
	_pth_release_lock(&descr->sched_lock, tid);
	FREE_NATIVE(descr, descr->sched_index);
#ifdef THREAD_DB
	if (unlikely (__pthread_doing_debug)) {
	    td_debug("pth_new_scheduler: main thread exiting\n");
	    __td_sync_handles();
	}
#endif 
        pth_sc(exit)(0);
    }
    pth_fdmode(descr->sigpipe[0], PTH_FDMODE_NONBLOCK);
    pth_fdmode(descr->sigpipe[1], PTH_FDMODE_NONBLOCK);

    if (pth_active_threads <= 1) {
	pth_exit_scheduler(descr, TRUE);
    }

    /* spawn the scheduler thread that will schedule on the new thread */
    t_attr = pth_attr_new();
    if (t_attr == NULL) {
	pth_exit_scheduler(descr, TRUE);
    }

    /* Schedulers always get maximum priority :), actually, real-time */
    pth_attr_set(t_attr, PTH_ATTR_SCHEDPOLICY,  SCHED_FIFO);
    pth_attr_set(t_attr, PTH_ATTR_PRIO,         xprio_get_max (SCHED_FIFO));
    pth_attr_set(t_attr, PTH_ATTR_NAME,         "**SCHEDULER**");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE,     FALSE);
    pth_attr_set(t_attr, PTH_ATTR_CANCEL_STATE, PTH_CANCEL_DISABLE);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE,   32*1024);
    pth_attr_set(t_attr, PTH_ATTR_STACK_ADDR,   NULL);

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Freeze things here until we report lwp create event */
	pth_acquire_lock(&descr->td_hold_lock);
	/* One we get it free it, as we just wanted to wait for
	   the gdb TD_CREATE to complete.		       */
	pth_release_lock(&descr->td_hold_lock);
    }
#endif

    descr->sched = PTH_SPAWN(t_attr, pth_scheduler, NULL);
    pth_attr_destroy(t_attr);
    if (descr->sched == NULL) {
	pth_exit_scheduler(descr, FALSE);
    }

    descr->sched->lastrannative = descr->tid;

    pth_debug2("pth_new_scheduler: scheduler started, pid = %i.  Transferring control to it...", 
	    descr->sched->lastrannative);

    /* Make the scheduler the current thread for this native... */
    descr->current = descr->sched;
    descr->is_running = TRUE;

    /* switch context to this new scheduler... */
    pth_mctx_restore(&descr->sched->mctx);

    pth_debug2("pth_new_scheduler: came back from switch to scheduler!!!!  Errno = %i",
	    errno);

    /*NOTREACHED*/
    return 0;
}

intern int pth_initialized_minimal = FALSE;

intern struct cal_st gcal; /* The cache allocator */

intern void __attribute ((constructor)) pth_initialize_minimal(void)
{
    if (pth_initialized_minimal == FALSE) { 
	int rc = 0;
	pth_descr_t descr = NULL;

	pth_number_of_natives = 0;
	descr = pth_alloc_native(FALSE, FALSE);
	descr->self = descr;
	descr->pid = descr->tid = k_gettid();
	INIT_NATIVE_SELF(descr, 0);
	pth_initialized_minimal = TRUE; 

	/* 
	 * Finally, check for the existence of the NGPT system calls.
	 *  Note: the sys_futex call is latest addition, if it is there,
	 *  the others are also there.
	 */
	rc = sys_futex(&rc, 1, INT_MAX, NULL);
	if (rc != 0) {
	    fprintf(stderr, "NGPT Initialize Error: kernel does not support NGPT.\n");
	    abort();
	}
	
    }
}

