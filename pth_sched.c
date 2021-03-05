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
**  pth_sched.c: Pth thread scheduler, the real heart of Pth
*/
                             /* ``Recursive, adj.;
                                  see Recursive.''
                                     -- Unknown   */
#include "pth_p.h"
#include "debug.h"
#include "pqueue.h"
#include "schedule.h"

#ifdef THREAD_DB
#include "td_manager.h"
#endif

  /* Debug info switches */

#define INIT_DEBUG 0
#define EVENTMGR_DEBUG 0

/* The one actual declaration */
struct pth_descr_st pth_native_list[PTH_MAX_NATIVE_THREADS];

#if cpp

/* external references to pth_native_list */
extern struct pth_descr_st pth_native_list[PTH_MAX_NATIVE_THREADS];

#endif
				    /* complete list of native thread descriptors.	*/
intern pth_t        pth_main;       /* the main thread					*/
intern pth_t        pth_fork_thread;/* the fork thread (main thread of fork process)	*/
intern struct pqueue_st pth_NQ;     /* queue of new threads				*/
intern struct pqueue_st pth_RQ;     /* queue of threads ready to run			*/
intern struct pqueue_st pth_WQ;     /* queue of threads waiting for mutex related event */
intern struct pqueue_st pth_SQ;      /* queue of suspended threads                      	*/
intern struct pqueue_st pth_DQ;      /* queue of terminated threads                      */

intern float        pth_loadval;    /* average scheduler load value			*/

static pth_time_t   pth_loadticknext;
intern pth_time_t   pth_loadtickgap;

intern sigset_t	    pth_sigblock;
intern pth_qlock_t  pth_native_lock;
intern pth_qlock_t  pth_sig_lock;
intern pth_qlock_t  pth_init_lock = pth_qlock_st_INIT_UNLOCKED;
intern pth_qlock_t  pth_exit_lock;
intern int	    pth_exit_count;

intern struct lnode_st pth_threads_queue;
intern int	    pth_threads_count;

extern int sigandset(sigset_t *, __const sigset_t *, __const sigset_t *) __THROW;

/* initialize the scheduler ingredients */
intern void pth_scheduler_init(void)
{
#ifdef NATIVE_SELF
    pth_descr_t descr = NATIVE_SELF;
#else
    pth_descr_t descr = pth_get_native_descr();
#endif

    __fdebugmsg (INIT_DEBUG, "%s(): starting up\n", __FUNCTION__);
    
    /* initialize pth_native_lock */
    pth_lock_init(pth_native_lock);

    /* initialize pth_key_lock */
    pth_lock_init(pth_key_lock);

    /* initialize pth_exit_lock */
    pth_lock_init(pth_exit_lock);
    pth_exit_count = 0;

    /* initialize pth_sig_lock */
    pth_lock_init(pth_sig_lock);

    pth_threads_count = 0;
    lnode_init (&pth_threads_queue);
    __fdebugmsg (INIT_DEBUG, "%s(): initialized locks and nodes\n",
                 __FUNCTION__);
    
    /* 
     * fill in the rest of the first native slot... 
     * (initially filled out in pth_initialize_minimal()...)
     */
    descr->stacksize	= 0;
    descr->true_stack	= NULL;
    descr->stack	= NULL;
    descr->sched_index	= 0;
    descr->is_bound     = 1;
    descr->is_used      = TRUE;
    descr->is_running   = TRUE;
    
    /* create the internal signal pipe */
    if (pipe(descr->sigpipe) == -1) {
        fprintf(stderr, "**Pth** INIT: Cannot create internal pipe: %s\n",
                strerror(errno));
        abort();
    }
    pth_fdmode(descr->sigpipe[0], PTH_FDMODE_NONBLOCK);
    pth_fdmode(descr->sigpipe[1], PTH_FDMODE_NONBLOCK);

    /* initalize the thread queues */
    pqueue_init(&pth_NQ);
    pqueue_init(&pth_RQ);
    pqueue_init(&pth_WQ);
    pqueue_init(&pth_SQ);
    pqueue_init(&pth_DQ);
    __fdebugmsg (INIT_DEBUG, "%s(): initialized thread queues\n",
                 __FUNCTION__);

    /* initialize load support */
    pth_loadval = 1.0;
    pth_time_set_now(&pth_loadticknext);

    sigemptyset(&pth_sigblock);
    return;
}

/* drop all threads (except for the currently active one) */
intern void pth_scheduler_drop(void)
{
    pth_t thread;
    pth_descr_t descr = pth_get_native_descr();
    struct lnode_st *lnode;

    lnode = list_next (&pth_threads_queue);
    while (!list_node_is_tail (lnode, &pth_threads_queue)) {
        thread = list_entry (lnode, struct pth_st, thread_queue);
	lnode = list_next (lnode);
	if (thread != descr->current)
	   pth_tcb_free(thread);
    }
    return;
}

intern int pth_WQ_count(void)
{
    int slot = 0;
    int count = 0;

    while (pth_native_list[slot].is_used) {
	pth_descr_t ds = &pth_native_list[slot++];
	count += pqueue_total(&ds->wait_queue);
    }
    count += pqueue_total(&pth_WQ);
    return count;
}

intern int pth_WQ_delete(pth_t t)
{
    int rc = FALSE;
    int slot = 0;
#ifdef NATIVE_SELF
    pth_descr_t descr = NATIVE_SELF;
#else
    pth_descr_t descr = pth_get_native_descr();
#endif

    while (pth_native_list[slot].is_used) {
	pth_descr_t ds = &pth_native_list[slot++];
	if (__pqueue_contains(&ds->wait_queue, &t->node)) {
	    rc = TRUE;
	    spin_lock(&ds->wait_queue.lock, descr, NULL);
	    __pqueue_delete_node(&t->node);
	    t->waited_native = 0;
	    spin_unlock(&ds->wait_queue.lock, descr);
	    break;
	}
    }
    return rc;
}

/*
 * Update the average scheduler load.
 *
 * This is called on every context switch, but we have to adjust the
 * average load value every second, only. When we're called more than
 * once per second we handle this by just calculating anything once
 * and then do NOPs until the next ticks is over. When the scheduler
 * waited for more than once second (or a thread CPU burst lasted for
 * more than once second) we simulate the missing calculations. That's
 * no problem because we can assume that the number of ready threads
 * then wasn't changed dramatically (or more context switched would have
 * been occurred and we would have been given more chances to operate).
 * The actual average load is calculated through an exponential average
 * formula.
 */
#define pth_scheduler_load(now, RQ) \
    if (pth_time_cmp((now), &pth_loadticknext) >= 0) { \
        pth_time_t ttmp; \
        int numready; \
        numready = pqueue_total(RQ); \
        ttmp = *(now); \
        do { \
            pth_loadval = (numready*0.25) + (pth_loadval*0.75); \
            pth_time_sub(&ttmp, &pth_loadtickgap); \
        } while (pth_time_cmp(&ttmp, &pth_loadticknext) >= 0); \
        pth_loadticknext = *(now); \
        pth_time_add(&pth_loadticknext, &pth_loadtickgap); \
    }

/* the heart of this library: the thread scheduler */
intern void *pth_scheduler(void *dummy)
{
    sigset_t sigs;
    pth_time_t running;
    pth_time_t snapshot;
    struct sigaction sa;
    sigset_t ss;
    int sig;
    pth_t t;
    pth_descr_t descr = NULL;
    volatile pth_t current;
    volatile pth_t this_sched = NULL;
    pid_t tid;
    struct pqueue_st *NQ;         /* queue of new threads   */
    struct pqueue_st *RQ;         /* queue of ready threads */
    int rq_elements = 0;

    /*
     * bootstrapping
     */
    /* find the pth_t for this scheduler... */
    if ((descr = pth_get_native_descr()) == NULL) {
	fprintf(stderr,"pth_scheduler: unable to find scheduler for pid %i.  Aborting...\n", 
		(unsigned int)k_gettid());
	abort();
    }
    this_sched = descr->sched;
    tid = descr->tid;
    pth_debug1("pth_scheduler: bootstrapping");

    /* mark this thread as a special scheduler thread */
    this_sched->state = PTH_STATE_SCHEDULER;

    /* block all signals in the scheduler thread */
    sigfillset(&sigs);
    pth_sc(sigprocmask)(SIG_SETMASK, &sigs, NULL);

    /* initialize the snapshot time for bootstrapping the loop */
    pth_time_set_now(&snapshot);

    if (descr->is_bounded || native_is_group_leader(descr)) {
	NQ = &descr->new_queue;
	RQ = &descr->ready_queue;
    } else {
	NQ = &pth_NQ;
	RQ = &pth_RQ;
    }

    /*
     * endless scheduler loop
     */
    for (;;) {
	pth_debug1("pth_scheduler: running...");

        /*
         * Move threads from new queue to ready queue and give
         * them maximum priority so they start immediately
         */
	if (!descr->is_bounded) {
	    spin_lock(&NQ->lock, descr, NULL);
	    while (pqueue_total (NQ) != 0) {
                t = thread_by_node (__pqueue_first_node (NQ));
                __pqueue_delete_node(&t->node);
		t->state = PTH_STATE_READY;
                pqueue_prepend_second_node(RQ, &t->node, descr);
		pth_debug3("pth_scheduler: new thread(0x%lx)\"%s\" moved to top of ready queue", 
				t, t->name);
	    }
	    spin_unlock(&NQ->lock, descr);
	}

        /*
         * Update average scheduler load
         */
        pth_scheduler_load(&snapshot, RQ);

	/*ibm begin*/
	while(TRUE) {
	    /*
	     * Find next thread in ready queue
	     */
	    spin_lock(&RQ->lock, descr, NULL);
	    current = __pqueue_total (RQ) > 0?
              thread_by_node (__pqueue_first_node(RQ)) : NULL;
	    if (current == NULL) {
		pth_debug1("pth_scheduler: No threads ready to run on this native thread, sleeping...\n");
		spin_unlock(&RQ->lock, descr);
		pth_native_yield();
		pth_debug1("pth_scheduler: Awake again, looking for work...");
		break;
	    }
            __pqueue_delete_node (&current->node);

	    /*
	     * See if the thread is unbound...
	     * Break out and schedule if so...
	     */
	    if (current->boundnative == 0) {
		spin_unlock(&RQ->lock, descr);
		break;
	    }
	    /*
	     * See if the thread is bound to a different native thread...
	     * Break out and schedule if not...
	     */
	    if (current->boundnative == descr) {
		spin_unlock(&RQ->lock, descr);
		break;
	    }
#if 0
	    if (descr->is_bounded && descr->bounded_thread != current) {
		_pth_release_lock(&(RQ->q_lock), tid);
		fprintf(stderr,"pth_scheduler: Bounded thread is not correct for tid=%i. Aborting...\n", tid);
		abort();
	    }
#endif

	    /* 
	     * The thread is bound to a different native thread...
	     * We, therefore, need to put it back in the ready queue but
	     * we'll give it favored status...
	     */
	    if (current->boundnative == &pth_first_native) {
		struct pqueue_st *rq = &pth_first_native.ready_queue; 
		pqueue_prepend_second_node(rq, &current->node, descr);
	    } else
		__pqueue_favor(&current->node);

	    spin_unlock(&RQ->lock, descr);

	    /* if current boundnative thread is sleeping, wakeup */
	    if (current->boundnative) {
		char c=(int)1;
		pth_descr_t ds = current->boundnative;
 
		_pth_acquire_lock(&(ds->sched_lock), tid);
		pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
		_pth_release_lock(&(ds->sched_lock), tid);
		current = NULL;
		break;
	    }
	}

	if (current == NULL) {
	    rq_elements = 0;
	    goto event_wait;
	}


	/*ibm end*/
        pth_debug5("pth_scheduler: thread(0x%lx) \"%s\" selected (prio=%d, eprio=%d)",
                   current, current->name, __thread_prio_get (current),
                   __thread_eprio_get (current));

        /*
         * Raise additionally thread-specific signals
         * (they are delivered when we switch the context)
         *
         * Situation is ('#' = signal pending):
         *     process pending (descr->sigpending):      ----####
         *     thread pending (pth_current->sigpending): --##--##
         * Result has to be:
         *     process new pending:                      --######
         */
	_pth_acquire_lock(&(current->lock), tid);
        if (current->sigpendcnt > 0) {
	    int cnt = 1;
            sigpending(&descr->sigpending);
            for (sig = 1; sig < PTH_NSIG; sig++) {
                if (sigispending(current->sigpending, sig)) {
                    if (!sigismember(&descr->sigpending, sig)) {
			if (sigismember(&current->sigactionmask, sig))
                        	k_tkill(tid, sig); /* send to a thread */
			else
                        	kill(descr->pid, sig);	/* send to process */
		    }
		    if (cnt++ == current->sigpendcnt)
			break;
		}
	    }
        }
	_pth_release_lock(&(current->lock), tid);

        /*
         * Set running start time for new thread
         * and perform a context switch to it
         */
        pth_debug3("pth_scheduler: switching to thread 0x%lx (\"%s\")",
                   (unsigned long)current, current->name);

        /* update thread times */
        pth_time_set_now(&current->lastran);

        /* update scheduler times */
        running = current->lastran;
        pth_time_sub(&running, &snapshot);
        pth_time_add(&this_sched->running, &running);

	/* update the native thread we're running on this time...     ibm*/
	current->lastrannative = tid;

	/* set the "current" thread as current */
	descr->current = current;

    	_pth_release_lock(&descr->sched_lock, tid);

        /* ** ENTERING THREAD ** - by switching the machine context */
        pth_mctx_switch(&this_sched->mctx, &current->mctx);

    	// _pth_acquire_lock(&descr->sched_lock, tid); /* it's already locked! */

        /* set the scheduler thread as current */
	descr->current = this_sched;
	if (descr->is_running == FALSE) {
	    pth_tcb_free(descr->sched);
	    pth_exit_scheduler(descr, FALSE);
	}

        /* update scheduler times */
        pth_time_set_now(&snapshot);
        pth_debug3("pth_scheduler: cameback from thread 0x%lx (\"%s\")",
                   (unsigned long)current, current->name);

        /*
         * Calculate and update the time the previous thread was running
         */
        running = snapshot;
        pth_time_sub(&running, &current->lastran);
        pth_time_add(&current->running, &running);
        pth_debug4("pth_scheduler: thread (0x%lx) \"%s\" ran %.6f",
                   current, current->name, pth_time_t2d(&running));


        /*
         * Remove still pending thread-specific signals
         * (they are re-delivered next time)
         *
         * Situation is ('#' = signal pending):
         *     thread old pending (pth_current->sigpending): --##--##
         *     process old pending (descr->sigpending):      ----####
         *     process still pending (sigstillpending):      ---#-#-#
         * Result has to be:
         *     process new pending:                          -----#-#
         *     thread new pending (pth_current->sigpending): ---#---#
         */
	_pth_acquire_lock(&(current->lock), tid);
        if (current->sigpendcnt > 0) {
            sigset_t sigstillpending;
            sigpending(&sigstillpending);
            for (sig = 1; sig < PTH_NSIG; sig++) {
                if ((current->sigthread != current) && sigispending(current->sigpending, sig)) {
		    /* if the current thread is signalled by another thread... */
                    if (!sigismember(&sigstillpending, sig)) {
                        /* thread (and perhaps also process) signal delivered */
                        sigdelpending(current->sigpending, sig);/* delete from sigpending list */
			if (!current->sigpendcnt--)
		    	     break;
                    }
                    else if (!sigismember(&descr->sigpending, sig)) {
                        /* thread signal not delivered */
                        pth_util_sigdelete(sig);
                    }
		}
            }
        }
	_pth_release_lock(&(current->lock), tid);

        /*
         * Check for stack overflow
         */
        if (current->stackguard != NULL) {
            if (*current->stackguard != 0xDEAD) {
                pth_debug3("pth_scheduler: stack overflow detected for thread 0x%lx (\"%s\")",
                           (unsigned long)current, current->name);
                /*
                 * if the application doesn't catch SIGSEGVs, we terminate
                 * manually with a SIGSEGV now, but output a reasonable message.
                 */
                if (__libc_sigaction(SIGSEGV, NULL, &sa) == 0) {
                    if (sa.sa_handler == SIG_DFL) {
                        fprintf(stderr, "**NGPT** STACK OVERFLOW: tid = %d, thread pid_t=0x%lx, name=\"%s\"\n",
                                (int) current_tid(), (unsigned long)current, current->name);
			/* We kill the main thread, passign the segv... */
                        kill(pth_primordial_thread()->tid, SIGSEGV);
                        sigfillset(&ss);
                        sigdelset(&ss, SIGSEGV);
                        sigsuspend(&ss);
                        abort();
                    }
                }
                /*
                 * else we terminate the thread only and send us a SIGSEGV
                 * which allows the application to handle the situation...
                 */
                current->join_arg = (void *)0xDEAD;
                current->state = PTH_STATE_EXIT;
                k_tkill(tid, SIGSEGV);
            }
        }

	/* if thread changes the native status.... */
	if (descr->is_bounded) {
		RQ = &descr->ready_queue;
		NQ = &descr->new_queue;
	}
        /*
         * When previous thread is now marked as dead, kick it out
         */
	if (current->state == PTH_STATE_DEAD)
	    current = NULL;

	if (current != NULL && current->state == PTH_STATE_EXIT) {
            pth_debug3("pth_scheduler: marking thread (0x%lx) \"%s\" as exiting", current, current->name);
            if (!current->joinable) {
		pth_debug2("pth_scheduler: thread 0x%lx not joinable, reaping...", current);

#ifdef THREAD_DB
		/* if thread debugging is enabled, alert the thread manager */
		if (unlikely (__pthread_doing_debug))
		    __pthread_signal_thread_event(current, current, TD_DEATH);
#endif

                pth_tcb_free(current); 
	    } else {
		char c = (int)1;
		pth_debug2("pth_scheduler: thread 0x%lx joinable, moving to DEAD queue...", current);
		spin_lock(&pth_DQ.lock, descr, NULL);
                __pqueue_append_node(&pth_DQ, &current->node);
		current->state = PTH_STATE_DEAD;
		if (current->joined_native) 
			pth_sc(write)(current->joined_native->sigpipe[1], &c, sizeof(char));
		else if (!descr->is_bounded && current->joined_thread) {
			pth_t tmp = current->joined_thread;
			pth_descr_t ds = tmp->waited_native;
			if (ds != NULL) {
			    /* move waiting thread from wait-queue to ready-queue */
			    spin_lock(&ds->wait_queue.lock, descr, NULL);
			    if (tmp->waited_native) { /* to avoid race with eventmgr */
				__pqueue_delete_node(&tmp->node);
				tmp->waited_native = 0;
				spin_unlock(&ds->wait_queue.lock, descr);

				spin_lock(&RQ->lock, descr, NULL);
				tmp->state = PTH_STATE_READY;
                                __thread_eprio_recalculate (tmp);
				__pqueue_append_node(RQ, &tmp->node);
			 	spin_unlock(&RQ->lock, descr);
	       			spin_unlock(&pth_DQ.lock, descr);

				continue;
			    } else
				spin_unlock(&ds->wait_queue.lock, descr);
			}
		}
	        spin_unlock(&pth_DQ.lock, descr);
	    }
            current = NULL;
        }

	/*
	 * If the thread was marked for suspension, 
	 *  move it the suspend queue now, then
	 *  go and check for events...
	 */
	if (current != NULL && current->should_suspend == TRUE) {
	    current->should_suspend = FALSE;
	    pqueue_append_node (&pth_SQ, &current->node, descr);
	}

        /*
         * When thread wants to wait for an event
         * move it to waiting queue now
         */
        if (current != NULL && current->state == PTH_STATE_WAITING) {
	    char c = (int)1;
            pth_debug3("pth_scheduler: moving thread (0x%lx) \"%s\" to waiting queue", current, current->name);
	    if ((current->boundnative == &pth_first_native) && (descr != &pth_first_native)) {
		pth_descr_t ds = &pth_first_native;
		if (pth_exit_count > 0) { 
		    pqueue_append_node (&ds->ready_queue, &current->node, descr);
                    
		    _pth_acquire_lock(&(ds->sched_lock), tid);
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
		    _pth_release_lock(&(ds->sched_lock), tid);
		} else {
                    spin_lock(&ds->wait_queue.lock, descr, NULL);
		    __pqueue_append_node(&ds->wait_queue, &current->node);
		    current->waited_native = ds;
                    spin_unlock (&ds->wait_queue.lock, descr);
		}
	    } else if ((native_is_group_leader(descr)) && descr->is_bounded) {
		int i = 1;
		pth_descr_t ds;

		/* find new unbounded native to handle pth_main */
		while (i < pth_number_of_natives) {
		    if (!pth_native_list[i].is_bounded)
			break;
		    i++;
		}
		ds = &pth_native_list[i];

		descr->is_bounded = FALSE;
		/* insert pth_main in pth_RQ list */
                pqueue_append_node(&pth_RQ, &current->node, descr);
                
		/* wakeup new native */
		if (ds->is_used)
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
	    } else {
                spin_lock(&descr->wait_queue.lock, descr, NULL);
		__pqueue_append_node(&descr->wait_queue, &current->node);
		current->waited_native = descr;
                spin_unlock(&descr->wait_queue.lock, descr);
	    }
            current = NULL;
        }

        /*
         * Adjust effective priority of the current, expired, thread,
         * so we give a change to others [CATCH! This only happens
         * when the thread is SCHED_OTHER].
         */
	spin_lock(&RQ->lock, descr, NULL);
/* #warning FIXME: Avoiding starvation of lower level prios */
        if (current != NULL)
            __pqueue_append_node(RQ, &current->node);
	rq_elements = pqueue_total(RQ);
	spin_unlock(&RQ->lock, descr);

        /*
         * Manage the events in the waiting queue, i.e. decide whether their
         * events occurred and move them to the ready queue. But wait only if
         * we have already no new or ready threads.
         */
    event_wait:
  	pth_time_set_now(&snapshot);
        if (rq_elements == 0
            && pqueue_total(NQ) == 0 ) {
	    pth_sched_eventmanager(&snapshot, descr, FALSE /* wait */);
        } else {
	    if (rq_elements == 1 || descr->do_yield) {
		descr->do_yield = 0;
		pth_sched_eventmanager(&snapshot, descr, TRUE  /* poll */);
	    }
	}
    }

    /* NOTREACHED */
    return NULL;
}

/*
 * Look whether some events already occurred and move
 * corresponding threads from waiting queue back to ready queue.
 */
intern void pth_sched_eventmanager(pth_time_t *now, pth_descr_t descr, int dopoll)
{
    pth_event_t nexttimer_ev;
    pth_time_t nexttimer_value;
    pth_event_t evh;
    pth_event_t ev;
    pth_t t = NULL;
    pth_t tlast;
    int this_occurred;
    int any_occurred;
    fd_set rfds;
    fd_set wfds;
    fd_set efds;
    pth_time_t delay;
    struct timeval delay_timeval;
    struct timeval *pdelay;
    sigset_t oss;
    struct sigaction sa;
    struct sigaction osa[1+PTH_NSIG];
    char minibuf[128];
    int loop_repeat;
    int fdmax;
    int n;
    int rc;
    int sig;
    int slot = 0;
    int	any_event_occurred;
    sigset_t sigs_we_block;
    sigset_t sigcatch;
    int mutex_event;
    struct pqueue_st *RQ;         /* queue of ready threads */
    char c = (int)1;
    int is_bounded = descr->is_bounded;
    int m_to_n = (!native_is_group_leader(descr) && !descr->is_bounded);
    int loop_once = 0; 

    pth_debug2("pth_sched_eventmanager: enter in %s mode",
               dopoll ? "polling" : "waiting");

    /* entry point for internal looping in event handling */
    loop_entry:
    loop_repeat = FALSE;

    /* initialize fd sets */
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    fdmax = -1;

    /* initialize signal status */
    sigpending(&descr->sigpending);
    memset(&descr->sigraised, 0x0, sizeof(descr->sigraised));

    /* initialize next timer */
    nexttimer_value = pth_time_zero;
    descr->nexttimer_thread = NULL;
    nexttimer_ev = NULL;

    if (native_is_group_leader(descr)) {
	sigemptyset(&sigcatch);
    	_pth_acquire_lock(&pth_sig_lock, descr->tid);
    	memcpy((void *)&sigs_we_block, &pth_sigblock, sizeof(sigset_t)); 
    	_pth_release_lock(&pth_sig_lock, descr->tid);
    }

    /* if this native is bounded and !dopoll, examine only it's wait queue events */
    if (!is_bounded) {
	RQ = &pth_RQ;
	/* if dopoll and ready queue contains elements, examine own wait queue events */
	if (m_to_n && dopoll && (pqueue_total(RQ) > 1)) {
	    slot = descr->sched_index;
	    loop_once = 1;
	}
    } else {
	if (dopoll) {
	    /* wakeup first native, which will examine all natives */
	    pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
	    return;
	}
	slot = descr->sched_index;
	RQ = &(descr->ready_queue);
    }

    any_event_occurred = FALSE;
    mutex_event = FALSE;

    /* for all threads in the native's waiting queue... */
    while (pth_native_list[slot].is_used) {
	pth_descr_t ds = &pth_native_list[slot++];

	/* if this is not first native and not a bounded native thread...  */
	/* for M:N (if this is not bounded), don't loop for 1:1 (bounded natives) */
	if (m_to_n && (ds->is_bounded || (ds == &pth_first_native))) {
	    continue;
	} else {
	    /* first native examines all natives ... */
	    if (ds->is_bounded || (ds == &pth_first_native))
		RQ = &(ds->ready_queue);
	    else
		RQ = &pth_RQ;
	}

        __fdebugmsg (EVENTMGR_DEBUG,
                     "%s (now %p, descr %p, dopoll %d): info: ds %p\n",
                     __FUNCTION__, now, descr, dopoll, ds);
        
	spin_lock(&ds->wait_queue.lock, descr, NULL);
        t = pqueue_total (&ds->wait_queue) == 0?
          NULL : thread_by_node (__pqueue_first_node(&ds->wait_queue));
	while (t != NULL) {

	    any_occurred = FALSE;
            __fdebugmsg (EVENTMGR_DEBUG,
                         "%s (now %p, descr %p, dopoll %d): info: ds %p thread %p\n",
                         __FUNCTION__, now, descr, dopoll, ds, t);
	    /* ... and all their events... */
	    if (t->events == NULL) {
		t = thread_by_node (__pqueue_get_next_node(&t->node));
		continue;
	    }

	    /* ...check whether events occurred */
	    ev = evh = t->events;
	    do {
		/* cancellation support... */
		if (t->cancelreq && (ev->ev_type != PTH_EVENT_MUTEX)) {
		    any_occurred = TRUE;
		    break;
		}
		/* if signal is pending and unblocked on a thread, wakeup.... */
		if (t->sigpendcnt > 0 && (ev->ev_type != PTH_EVENT_SIGS)) {
		    int cnt = 1;
		    for (sig = 1; sig < PTH_NSIG; sig++) {
			if (sigispending(t->sigpending, sig)) {
			    if (!sigismember(&t->mctx.sigs, sig))
				any_occurred = TRUE;
			    if (cnt++ == t->sigpendcnt)
				break;
			}
		    }
		    if (any_occurred == TRUE)
			break;
		}
		if (!ev->ev_occurred) {
		    this_occurred = FALSE;

		    /* Filedescriptor I/O */
		    if (ev->ev_type == PTH_EVENT_FD && (ds == descr)) {
			/* filedescriptors are checked later all at once.
			   Here we only assemble them in the fd sets */
			if (ev->ev_goal & PTH_UNTIL_FD_READABLE)
			    FD_SET(ev->ev_args.FD.fd, &rfds);
			else if (ev->ev_goal & PTH_UNTIL_FD_WRITEABLE)
			    FD_SET(ev->ev_args.FD.fd, &wfds);
			else if (ev->ev_goal & PTH_UNTIL_FD_EXCEPTION)
			    FD_SET(ev->ev_args.FD.fd, &efds);
			if (fdmax < ev->ev_args.FD.fd)
			    fdmax = ev->ev_args.FD.fd;
		    }
		    /* Filedescriptor Set Select I/O */
		    else if (ev->ev_type == PTH_EVENT_SELECT) {
			/* filedescriptors are checked later all at once.
			   Here we only merge the fd sets. */
			pth_util_fds_merge(ev->ev_args.SELECT.nfd,
					   ev->ev_args.SELECT.rfds, &rfds,
					   ev->ev_args.SELECT.wfds, &wfds,
					   ev->ev_args.SELECT.efds, &efds);
			if (fdmax < ev->ev_args.SELECT.nfd-1)
			    fdmax = ev->ev_args.SELECT.nfd-1;
		    }
		    /* Signal Set for sigwait() */
		    else if (ev->ev_type == PTH_EVENT_SIGS) {
			int wakeup_firstnative = 0;
			for (sig = 1; sig < PTH_NSIG; sig++) {
			    if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
				/* thread signal handling */
				if (sigispending(t->sigpending, sig)) {
				    *(ev->ev_args.SIGS.sig) = sig;
				    this_occurred = TRUE;
				    sigdelpending(t->sigpending, sig);
				    if (!t->sigpendcnt--)
					break;
				}
				/* process signal handling */
				if (sigismember(&descr->sigpending, sig)) {
				    if (ev->ev_args.SIGS.sig != NULL)
					*(ev->ev_args.SIGS.sig) = sig;
				    pth_util_sigdelete(sig);
				    sigdelset(&descr->sigpending, sig);
				    this_occurred = TRUE;
				} else {
				    if (native_is_group_leader(descr)) {
					sigdelset(&sigs_we_block, sig);
					sigaddset(&sigcatch, sig);
				    } else
					wakeup_firstnative = 1;
				}
			    }
			}
			if (wakeup_firstnative)
			    pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
		    }
		    /* Timer */
		    else if (ev->ev_type == PTH_EVENT_TIME) {
			if (pth_time_cmp(&(ev->ev_args.TIME.tv), now) < 0)
			    this_occurred = TRUE;
			else if (ev->ev_flags != TRUE) {
			    /* remember the timer which will be elapsed next */
			    if (m_to_n || pth_max_native_threads==1 || t->boundnative == descr) {
				if ((descr->nexttimer_thread == NULL && nexttimer_ev == NULL) ||
				    pth_time_cmp(&(ev->ev_args.TIME.tv), &nexttimer_value) < 0) {
				    if (descr->nexttimer_thread && nexttimer_ev)
				    	nexttimer_ev->ev_flags = FALSE;
				    descr->nexttimer_thread = t;
				    nexttimer_ev = ev;
				    nexttimer_ev->ev_flags = TRUE;
				    nexttimer_value = ev->ev_args.TIME.tv;
				}
			    }
			}
		    }
		    /* Message Port Arrivals */
		    else if (ev->ev_type == PTH_EVENT_MSG) {
			if (pth_ring_elements(&(ev->ev_args.MSG.mp->mp_queue)) > 0)
			    this_occurred = TRUE;
		    }
		    /* Mutex Release */
		    else if (ev->ev_type == PTH_EVENT_MUTEX) {
			if (!(ev->ev_args.MUTEX.mutex->mx_state & PTH_MUTEX_LOCKED)) {
			    this_occurred = TRUE;
			    mutex_event = TRUE;
			}
		    }
		    /* Condition Variable Signal */
		    else if (ev->ev_type == PTH_EVENT_COND) {
			if (ev->ev_args.COND.cond->cn_state & PTH_COND_SIGNALED) {
			    if (ev->ev_args.COND.cond->cn_state & PTH_COND_BROADCAST)
				this_occurred = TRUE;
			    else {
				if (!(ev->ev_args.COND.cond->cn_state & PTH_COND_HANDLED)) {
				    ev->ev_args.COND.cond->cn_state |= PTH_COND_HANDLED;
				    this_occurred = TRUE;
				    mutex_event = TRUE;
				}
			    }
			}
		    }
		    /* Thread Termination */
		    else if (ev->ev_type == PTH_EVENT_TID) {
			if ( (ev->ev_args.TID.tid != NULL
				&& ev->ev_args.TID.tid->state == ev->ev_goal)
			    || (t == pth_main && pth_active_threads == 1
				&& !thread_exists(ev->ev_args.TID.tid)))
			    this_occurred = TRUE;
		    }
		    /* Custom Event Function */
		    else if (ev->ev_type == PTH_EVENT_FUNC) {
			if (ev->ev_args.FUNC.func(ev->ev_args.FUNC.arg)) 
			    this_occurred = TRUE;
			else if (ev->ev_flags != TRUE) {
			    if (m_to_n || pth_max_native_threads==1 || t->boundnative == descr) {
				pth_time_t tv;
				tv = *now;
				pth_time_add(&tv, &(ev->ev_args.FUNC.tv));
				if ((descr->nexttimer_thread == NULL && nexttimer_ev == NULL) ||
				     pth_time_cmp(&tv, &nexttimer_value) < 0) {
				     if (descr->nexttimer_thread && nexttimer_ev)
					nexttimer_ev->ev_flags = FALSE;
				     descr->nexttimer_thread = t;
				     nexttimer_ev = ev;
				     nexttimer_ev->ev_flags = TRUE;
				     nexttimer_value = tv;
				}
			    }
			}
		    }

		    /* Signal Set for sleep(), register an internal handler for it */
		    else if (ev->ev_type == PTH_EVENT_TIME_SIGS) {
			int wakeup_firstnative = 0;
			for (sig = 1; sig < PTH_NSIG; sig++) {
			    if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
				if (native_is_group_leader(descr)) {
				    if (!sigismember(&t->mctx.sigs, sig)) { 
					sigdelset(&sigs_we_block, sig);
					sigaddset(&sigcatch, sig);
				    }
				} else
				    wakeup_firstnative = 1;
			    }
			}
			if (wakeup_firstnative)
			    pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
		    }

		    /* tag event if it has occurred */
		    if (this_occurred) {
			pth_debug2("pth_sched_eventmanager: [non-I/O] event occurred for thread \"%s\"", t->name);
			ev->ev_occurred = TRUE;
			any_occurred = TRUE;
		    }
		}
	    } while ((ev = ev->ev_next) != evh);
	    tlast = t;
	    t = thread_by_node (__pqueue_get_next_node (&t->node));
	    if (any_occurred) {
		__pqueue_delete_node(&tlast->node);
		tlast->waited_native = 0;
		tlast->state = PTH_STATE_READY;
                thread_eprio_recalculate (tlast, descr);
		pqueue_append_node(RQ, &tlast->node, descr);
		any_event_occurred = TRUE;
		if (native_is_group_leader(descr) && (ds->is_bound == FALSE))
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
	    }
	}
	spin_unlock(&ds->wait_queue.lock, descr);
	if (m_to_n) {
	    if (loop_once || mutex_event || any_event_occurred)
		break;
	} else if (is_bounded && !dopoll)
	    break;
    }

    if (any_event_occurred)
        dopoll = TRUE;

    /* now decide how to poll for fd I/O and timers */
    if (dopoll) {
        /* do a polling with immediate timeout,
           i.e. check the fd sets only without blocking */
	delay_timeval.tv_sec = delay_timeval.tv_usec = 0;
        pdelay = &delay_timeval;
	if (descr->nexttimer_thread != NULL && nexttimer_ev != NULL)
	    nexttimer_ev->ev_flags = FALSE;
    }
    else if (nexttimer_ev != NULL) {
        /* do a polling with a timeout set to the next timer,
           i.e. wait for the fd sets or the next timer */
        delay = nexttimer_value;
        pth_time_sub(&delay, now);
	delay_timeval = pth_time_to_timeval(&delay);
        pdelay = &delay_timeval;
    }
    else {
        /* do a polling without a timeout,
           i.e. wait for the fd sets only with blocking */
        pdelay = NULL;
    }

    /* let select() wait for the read-part of the pipe */
    FD_SET(descr->sigpipe[0], &rfds);

    /* If dopoll is set, handle the thread from ready queue... */
    if (m_to_n && dopoll && fdmax == -1)
	return;
    
    if (fdmax < descr->sigpipe[0])
        fdmax = descr->sigpipe[0];

    /* replace signal actions for signals we've to catch for events */
    if (native_is_group_leader(descr)) {
    	for (sig = 1; sig < PTH_NSIG; sig++) {
	    if (sigismember(&sigcatch, sig)) {
            	sa.sa_handler = pth_sched_eventmanager_sighandler;
            	sigfillset(&sa.sa_mask);
            	sa.sa_flags = 0;
            	__libc_sigaction(sig, &sa, &osa[sig]);
	    }
    	}
    }

    /* allow some signals to be delivered: Either to our
       catching handler or directly to the configured
       handler for signals not catched by events */
    if (native_is_group_leader(descr))
    	pth_sc(sigprocmask)(SIG_SETMASK, &sigs_we_block, &oss);

    if (is_bounded && pqueue_total(RQ))
	return;
    /* now do the polling for filedescriptor I/O and timers
       WHEN THE SCHEDULER SLEEPS AT ALL, THEN HERE!! */
    rc = -1;
    descr->is_bound = 0;
    if (!(dopoll && fdmax == -1)) {
	_pth_release_lock(&(descr->sched_lock), descr->tid);

        while ((rc = pth_sc(select)(fdmax+1, &rfds, &wfds, &efds, pdelay)) < 0
               && errno == EINTR) ;

	_pth_acquire_lock(&(descr->sched_lock), descr->tid);
	if (descr->nexttimer_thread == NULL)
	    nexttimer_ev = NULL;
    }
    descr->is_bound = 1;
    /* clear pipe */
    while (pth_sc(read)(descr->sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;

    /* restore signal mask and actions and handle signals */
    if (native_is_group_leader(descr)) {
    	pth_sc(sigprocmask)(SIG_SETMASK, &oss, NULL);
    	for (sig = 1; sig < PTH_NSIG; sig++) {
	    if (sigismember(&sigcatch, sig))
            	__libc_sigaction(sig, &osa[sig], NULL);
	}
    }

    /* if the timer elapsed, handle it */
    if (!dopoll && nexttimer_ev != NULL) {
	if (rc == 0) {
	    pth_debug2("pth_sched_eventmanger: nexttimer_ev = 0x%lx", nexttimer_ev);
	    if (nexttimer_ev->ev_type == PTH_EVENT_FUNC) {
		/* it was an implicit timer event for a function event,
		   so repeat the event handling for rechecking the function */
		loop_repeat = TRUE;
	    }
	    else {
		/* it was an explicit timer event, standing for its own */
		pth_debug3("pth_sched_eventmanager:[timeout] event occurred for thread 0x%lx \"%s\"",
			   descr->nexttimer_thread, descr->nexttimer_thread->name);
		nexttimer_ev->ev_flags = FALSE;
		nexttimer_ev->ev_occurred = TRUE;

	    }
	} else {
            /* timer has not elapsed, clear flag or it might get lost - aj */
            pth_debug3("pth_sched_eventmanager: resetting non elapsed timer for thread 0x%lx \"%s\"", descr->nexttimer_thread, descr->nexttimer_thread->name);
	    if (descr->nexttimer_thread != NULL && nexttimer_ev != NULL)
            	nexttimer_ev->ev_flags = FALSE;
	}
    }

    /* if the internal signal pipe was used, adjust the select() results */
    if (!dopoll && rc > 0 && FD_ISSET(descr->sigpipe[0], &rfds)) {
        FD_CLR(descr->sigpipe[0], &rfds);
        rc--;
    }

    /* if an error occurred, avoid confusion in the cleanup loop */
    if (rc <= 0) {
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_ZERO(&efds);
    }

    /* if this native is bounded, examine only it's wait queue events */ 
    if (!is_bounded) {
	slot = 0;
	RQ = &pth_RQ;
    } else {
	if (pqueue_total(RQ))
	    return;
	slot = descr->sched_index;
    }

    /* now comes the final cleanup loop where we've to
       do two jobs: first we've to the late handling of the fd I/O events and
       additionally if a thread has one occurred event, we move it from the
       waiting queue to the ready queue */

    while (pth_native_list[slot].is_used) {
	pth_descr_t ds = &pth_native_list[slot++];

	if (m_to_n && (ds->is_bounded || (ds == &pth_first_native))) {
	    continue;
	} else {
	    /* first native examines all natives ... */
	    if (ds->is_bounded || (ds == &pth_first_native))
		RQ = &(ds->ready_queue);
	    else
		RQ = &pth_RQ;
	}

	spin_lock(&ds->wait_queue.lock, descr, NULL);
	t = pqueue_total (&ds->wait_queue) == 0?
          NULL : thread_by_node (__pqueue_first_node(&ds->wait_queue));
    
	while (t != NULL) {

	    /* do the late handling of the fd I/O and signal
	       events in the waiting event ring */
	    any_occurred = FALSE;
	    if (t->events != NULL) {
		ev = evh = t->events;
		do {
		    /*
		     * Late handling for still not occured events
		     */
		    if (!ev->ev_occurred) {
			/* if native is bounded (1:1), check related events only... */
			if (is_bounded) {
			    /* Mutex Release */
			    if (ev->ev_type == PTH_EVENT_MUTEX) {
				if (!(ev->ev_args.MUTEX.mutex->mx_state & PTH_MUTEX_LOCKED))
				    ev->ev_occurred = TRUE;
			    }
			    /* Condition Variable Signal */
			    else if (ev->ev_type == PTH_EVENT_COND) {
				unsigned long state = ev->ev_args.COND.cond->cn_state;
				if (state & PTH_COND_SIGNALED) {
				    if (state & PTH_COND_BROADCAST)
					ev->ev_occurred = TRUE;
				    else {
					if (!(state & PTH_COND_HANDLED)) {
					    ev->ev_args.COND.cond->cn_state |= PTH_COND_HANDLED;
					    ev->ev_occurred = TRUE;
					}
				    }
				}
			    }
			    /* Thread Termination */
			    if (ev->ev_type == PTH_EVENT_TID) {
				if (ev->ev_args.TID.tid != NULL
				    && ev->ev_args.TID.tid->state == ev->ev_goal)
					ev->ev_occurred = TRUE;
			    }
			} else {  /* for M:N */
			    /* Filedescriptor I/O */
			    if (ev->ev_type == PTH_EVENT_FD && (ds == descr)) {
				if (   (ev->ev_goal & PTH_UNTIL_FD_READABLE
					&& FD_ISSET(ev->ev_args.FD.fd, &rfds))
				    || (ev->ev_goal & PTH_UNTIL_FD_WRITEABLE
					&& FD_ISSET(ev->ev_args.FD.fd, &wfds))
				    || (ev->ev_goal & PTH_UNTIL_FD_EXCEPTION
					&& FD_ISSET(ev->ev_args.FD.fd, &efds)) ) {
				    pth_debug3("pth_sched_eventmanager: "
					   "[I/O] event occurred for thread 0x%lx \"%s\"", t, t->name);
				    ev->ev_occurred = TRUE;
				}
			    }
			    /* Filedescriptor Set I/O */
			    else if (ev->ev_type == PTH_EVENT_SELECT) {
				if (pth_util_fds_test(ev->ev_args.SELECT.nfd,
						  ev->ev_args.SELECT.rfds, &rfds,
						  ev->ev_args.SELECT.wfds, &wfds,
						  ev->ev_args.SELECT.efds, &efds)) {
				    n = pth_util_fds_select(ev->ev_args.SELECT.nfd,
							ev->ev_args.SELECT.rfds, &rfds,
							ev->ev_args.SELECT.wfds, &wfds,
							ev->ev_args.SELECT.efds, &efds);
				    if (ev->ev_args.SELECT.n != NULL)
					*(ev->ev_args.SELECT.n) = n;
				    ev->ev_occurred = TRUE;
				    pth_debug3("pth_sched_eventmanager: "
					   "[I/O] event occurred for thread 0x%lx \"%s\"", t, t->name);
				}
			    }
			    /* Signal Set */
			    else if ((ev->ev_type == PTH_EVENT_SIGS) ||
			    	     (ev->ev_type == PTH_EVENT_TIME_SIGS)) {
				for (sig = 1; sig < PTH_NSIG; sig++) {
				    if (sigismember(ev->ev_args.SIGS.sigs, sig)) {
					if (sigispending(descr->sigraised, sig)) {
					    if (ev->ev_args.SIGS.sig != NULL)
						*(ev->ev_args.SIGS.sig) = sig;
					    pth_debug3("pth_sched_eventmanager: "
						   "[signal] event occurred for thread 0x%lx \"%s\"", t, t->name);
					    sigdelpending(descr->sigraised, sig);
					    ev->ev_occurred = TRUE;
					}
				    }
				}
			    }
			}
		    }
		    /* local to global mapping */
		    if (ev->ev_occurred)
			any_occurred = TRUE;
		} while ((ev = ev->ev_next) != evh);
	    }

	    /* cancellation support */
	    if (t->cancelreq == TRUE) {
		pth_debug3("pth_sched_eventmanager: cancellation request pending for thread 0x%lx \"%s\"", t, t->name);
		any_occurred = TRUE;
	    }

	    /* Get the next entry... */
	    tlast = t;
	    t = thread_by_node (__pqueue_get_next_node (&t->node));

	    /*
	     * move last thread to ready queue if any events occurred for it.
	     * we insert it with a slightly increased queue priority to it a
	     * better chance to immediately get scheduled, else the last running
	     * thread might immediately get again the CPU which is usually not
	     * what we want, because we oven use pth_yield() calls to give others
	     * a chance.
	     */
	    if (any_occurred) {
		__pqueue_delete_node(&tlast->node);
		tlast->waited_native = 0;
		tlast->state = PTH_STATE_READY;
                thread_eprio_recalculate (tlast, descr);
                pqueue_append_node (RQ, &tlast->node, descr);
		pth_debug3("pth_sched_eventmanager: thread (0x%lx) \"%s\" moved from waiting "
			   "to ready queue", tlast, tlast->name);
		if (native_is_group_leader(descr) && (ds->is_bound == FALSE))
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
	    }
	}
	spin_unlock(&ds->wait_queue.lock, descr);
	if (is_bounded)
	    break;
    }
    
    /* perhaps we have to internally loop... */
    if (loop_repeat) {
	pth_time_set_now(now);
        goto loop_entry;
    }

    pth_debug1("pth_sched_eventmanager: leaving");
    return;
}

intern void pth_sched_eventmanager_sighandler(int sig)
{
    char c;
    pth_descr_t descr = NULL;

    pth_debug2("pth_sched_eventmanger_sighandler: caught signal %d", sig);

    /* Get the thread descriptor for the running thread... */
    if ((descr = pth_get_native_descr()) == NULL) {
	fprintf(stderr, "pth_sched_eventmanager_sighandler: no scheduler found !?!?!\n");
	abort();
    }

    /* remember raised signal */
    sigaddpending(descr->sigraised, sig);

    /* write signal to signal pipe in order to awake the select() */
    c = (int)sig;
    pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));

    if (descr->tid != descr->pid)
	k_tkill(descr->tid, sig); /* send to a thread */
    return;
}

