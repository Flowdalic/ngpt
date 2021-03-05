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
**  pth_lib.c: Pth main library code
*/
                             /* ``It took me fifteen years to discover
                                  I had no talent for programming, but
                                  I couldn't give it up because by that
                                  time I was too famous.''
                                            -- Unknown                */
#include "pth_p.h"
#include "allocation.h"
#include "useldt.h"
#include "list.h"
#include "pqueue.h"
#include "schedule.h"
#ifdef THREAD_DB
#include "td_manager.h"
#endif

  /* Debugging facilities */

#define NGPT_DEBUG_JOIN  0
#define NGPT_DEBUG_YIELD  0


/* implicit initialization support */
intern int pth_initialized = FALSE;
intern int pth_initializing = TRUE;
intern int pth_shutdown_inprogress = FALSE;

intern int  pth_max_native_threads;	/*ibm*/
intern int  pth_number_of_natives;	/*ibm*/
intern int  pth_threads_per_native;	/*ibm*/
intern int  pth_default_stacksize;

extern time_t time(time_t *) __THROW;
extern int sigisemptyset(__const sigset_t *) __THROW;

/* return the hexadecimal Pth library version number */
long pth_version(void)
{
    return PTH_VERSION;
}



static void main_complete(pth_t t)
{
#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Set any info written by gdb to the dummy tcb */
	t->td_report_events = (__pthread_handles[0].h_descr)->td_report_events;
	t->td_events = (__pthread_handles[0].h_descr)->td_events;

	pth_main = t;	/* covers sync_handles before spawn completes */
    }
#endif
}

/* initialize the package */
int pth_init(void)
{
    int slot = 0;
    char *c_ratio = NULL;
    char *c_numcpus = NULL;
    char *c_stacksize = NULL;
    int	 cpu_based = 0;
    pth_attr_t t_attr;
    pth_descr_t descr = NULL;
    int main_sched_policy, main_priority;
    
    /* support for implicit initialization calls
       and to prevent multiple explict initialization, too */
    if (pth_initialized || pth_shutdown_inprogress)
        return FALSE;

    if (unlikely (pth_initialized_minimal != TRUE))
	pth_initialize_minimal();
    /* Initialize the allocator
     *
     * When we fork, we want to reuse the allocator, as it is
     * the same one. 
     */
        
    pth_initialized         = TRUE;
    debug_initialize();
    if (pth_fork_initialized != TRUE) {
	cal_initialize();
	cal_init (&gcal);
    }

    pth_time_init();

    pth_initializing        = TRUE;
    pth_shutdown_inprogress = FALSE;

    /* Initialize the native thread list... */
    for (slot = 1; slot < PTH_MAX_NATIVE_THREADS; slot++)
	memset(&pth_native_list[slot], 0x0, sizeof(struct pth_descr_st));

#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
    /* An' the trampoline for thread 0 if needed... */
    pth_native_list[0].mctx_trampoline = pth_malloc (sizeof(jmp_buf));
    if (pth_native_list[0].mctx_trampoline == NULL) {
	fprintf(stderr,"pth_init: unable to retrieve initial descriptor !?!?!?\n");
	abort();
    }
#endif


    pth_debug1("pth_init: enter");
    
    /* initialize the scheduler */
    pth_scheduler_init();

    /* the current descriptor... */
    if ((descr = pth_get_native_descr()) == NULL) {
	fprintf(stderr,"pth_init: unable to retrieve initial descriptor !?!?!?\n");
	abort();
    }

    /* determine the default thread stack size. */
    pth_default_stacksize = 64;
    c_stacksize = getenv("DEFAULTTHREADSTACKSIZE");
    if (c_stacksize != NULL) {
	long stacksize = strtol(c_stacksize, (char **)NULL, 10);
	if (errno != ERANGE && stacksize >= (PTHREAD_STACK_MIN / 1024) 
#ifdef ARCH_STACK_MAX_SIZE
		            && stacksize <= (ARCH_STACK_MAX_SIZE / 1024))
#else
		            && stacksize <= (PTH_MAX_STACK_SIZE))
#endif
	    pth_default_stacksize = (int)stacksize;
    }

    /* spawn the scheduler thread */
    t_attr = pth_attr_new();
    if (t_attr == NULL) {
	fprintf(stderr,"pth_init: unable to allocate initial attribute !?!?!?!\n");
	abort();
    }
    /* Schedulers always get maximum priority :), actually, real-time */
    pth_attr_set(t_attr, PTH_ATTR_SCHEDPOLICY,  SCHED_FIFO);
    pth_attr_set(t_attr, PTH_ATTR_PRIO,         xprio_get_max (SCHED_FIFO));
    pth_attr_set(t_attr, PTH_ATTR_NAME,         "**SCHEDULER**");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE,     FALSE);
    pth_attr_set(t_attr, PTH_ATTR_CANCEL_STATE, PTH_CANCEL_DISABLE);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE,   32*1024);
    pth_attr_set(t_attr, PTH_ATTR_STACK_ADDR,   NULL);
    descr->sched = PTH_SPAWN(t_attr, pth_scheduler, NULL);
    if (descr->sched == NULL) {
        pth_attr_destroy(t_attr);
        close(pth_first_native.sigpipe[0]);
        close(pth_first_native.sigpipe[1]);
        return FALSE;
    }
    descr->sched->lastrannative = descr->tid;

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Setup thread debug/restart signal handlers prior to starting 
	   the main thread. This establishes the handlers for all threads. */
	struct sigaction sa;
	sigset_t mask;
	sa.sa_handler = __pthread_handle_sigrestart;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	__libc_sigaction(__pthread_sig_restart, &sa, NULL);
	sa.sa_handler = __pthread_handle_sigdebug;
	sigemptyset(&sa.sa_mask);
	// sa.sa_flags = 0;
	__libc_sigaction(__pthread_sig_debug, &sa, NULL);
	/* Initially, block __pthread_sig_restart. Will be unblocked on
	   demand later when needed to get debugger restarts.         */
	sigemptyset(&mask);
	sigaddset(&mask, __pthread_sig_restart);
	pth_sc(sigprocmask)(SIG_BLOCK, &mask, NULL);

	/* Get lock to lock manager until initialized.                */
	pth_acquire_lock(&manager_setup_lock);
	td_debug("pth_init: manager_setup_lock taken\n");
    }
#endif

    /* spawn a thread for the main program
    **
    ** We need to get it's policy and priorities first - getpriority()
    ** and sched_getparam() should not fail at all, so we don't
    ** check.
    */
    main_sched_policy = sched_getscheduler (0);
    if (main_sched_policy == SCHED_OTHER)
        main_priority = getpriority (PRIO_PROCESS, 0);
    else {
        struct sched_param param;
        sched_getparam (0, &param);
        main_priority = param.sched_priority;
    }
        
    pth_attr_set(t_attr, PTH_ATTR_SCHEDPOLICY,  main_sched_policy);
    pth_attr_set(t_attr, PTH_ATTR_PRIO,         main_priority);
    pth_attr_set(t_attr, PTH_ATTR_NAME,         "main");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE,     TRUE);
    pth_attr_set(t_attr, PTH_ATTR_CANCEL_STATE, PTH_CANCEL_ENABLE|PTH_CANCEL_DEFERRED);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE,   0 /* special */);
    pth_attr_set(t_attr, PTH_ATTR_STACK_ADDR,   NULL);
    pth_main = (pth_t)pth_spawn_cb((pth_attr_t)t_attr, (void *(*)(void *))(-1), NULL, main_complete);
    if (pth_main == NULL) {
        pth_attr_destroy(t_attr);
        pth_tcb_free(descr->sched);
        close(pth_first_native.sigpipe[0]);
        close(pth_first_native.sigpipe[1]);
        return FALSE;
    }
    pth_attr_destroy(t_attr);

    /* The last and only native main runs on needs to be set here */
    pth_main->lastrannative = descr->tid;

    /*begin ibm*/
    pth_threads_per_native  = 1;
    pth_max_native_threads  = 0;
    pth_number_of_natives   = 1;

    /* determine the number of native threads per cpu. */
    c_ratio = getenv("MAXTHREADPERCPU");
    if (c_ratio != NULL) {
	long ratio = strtol(c_ratio, (char **)NULL, 10);
	if (errno != ERANGE)
	    pth_threads_per_native = (int)ratio;
    }
    
    /* 
     * See if the MAXNATIVETHREADS environment variable is set.
     * We'll use this instead of the number of cpus if this
     * is set since the user wants to override the default behavior
     * which is based on the number of CPUs in the host.
     */
    c_numcpus = getenv("MAXNATIVETHREADS");
    if (c_numcpus != NULL) {
        long numcpus = strtol(c_numcpus, (char **)NULL, 10);
        if (errno != ERANGE)
	    pth_max_native_threads = (int)numcpus;
    }

    /*
     * We check to see if we've gotten an override...
     *	If not, we'll base it off of CPU and set a
     *	max number of threads per cpu to 1.
     */
    if (pth_max_native_threads == 0) {
	pth_max_native_threads = sysconf(_SC_NPROCESSORS_CONF); 
	pth_threads_per_native = 1;
	cpu_based = 1;
    }

    if (pth_max_native_threads > 1) {
	pth_main->boundnative = &pth_first_native;
	pth_max_native_threads++;
    }

    pth_debug4("pth_init: Maximum # of native threads: %i, Threshold: %i, Basis: %s", 
		pth_max_native_threads, pth_threads_per_native, (cpu_based) ? "CPU" : "ENV");
    /*end ibm*/
    /*
     * The first time we've to manually switch into the scheduler to start
     * threading. Because at this time the only non-scheduler thread is the
     * "main thread" we will come back immediately. We've to also initialize
     * the pth_current variable here to allow the pth_spawn_trampoline
     * function to find the scheduler.
     */
    descr->current = descr->sched;
    pth_mctx_switch(&pth_main->mctx, &descr->sched->mctx);
    pth_debug1("pth_init: after manual switch into scheduler\n");

    /* Create the watchdog... */
    if (pth_max_native_threads > 1)
#ifdef THREAD_DB
	if (likely (__pthread_doing_debug == 0))
#endif
	pth_new_watchdog();

    /* Initialize/open shared memory... */
    if (pth_initialize_shared() != TRUE) {
	fprintf(stderr, "pth_init: opening/mapping shared memory failed....\n");
	abort();
    }

    memset(&pth_atfork_list, 0x0, PTH_ATFORK_MAX * sizeof(struct pth_atfork_st));
#ifdef USE_TSD
    /* activate C library entry points */
    __libc_internal_tsd_set = _libc_internal_tsd_set;
#endif

    /* Now we're done initializing... */
    pth_initializing = FALSE;

    /* came back, so let's go home... */
    pth_debug1("pth_init: leave");
    return TRUE;
}

/* kill the package internals */
int pth_kill(void)
{
    volatile pth_descr_t descr = NULL;
    pth_t current;

    pth_debug1("pth_kill: enter");
    
    /* Don't die twice... */
    if (pth_initialized != TRUE)
	return TRUE;

    descr = pth_get_native_descr();

    _pth_acquire_lock(&pth_exit_lock, descr->tid);
    if (pth_exit_count > 0) {
	pth_exit_count++;
	_pth_release_lock(&pth_exit_lock, descr->tid);
	return TRUE;
    }
    pth_exit_count++;
    _pth_release_lock(&pth_exit_lock, descr->tid);

    if ((current = descr->current) == NULL) {
	/* should be NON-null! */
	abort();
    }

    /* if this is not first native, bound to it and wait */ 
    current->boundnative = &pth_first_native;
    pth_first_native.bounded_thread = current;

    /* This "goto" is necessary to workaround a problem with gcc 3.2 loop optimization */
try_again:
    if (pth_first_native.tid != descr->tid) {
        current->state = PTH_STATE_WAITING;
        pth_yield(NULL);
	descr = pth_get_native_descr();
	goto try_again;
    }

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug))
    {
	struct pthread_request request;
	td_debug("pth_kill: doing REQ_PROCESS_EXIT request submit");
	/* Build a thread manager exit request and submit it, to notify
	   the manager that the jig is up and it should shutdown.        */
	request.req_thread = current;
	request.req_kind = REQ_PROCESS_EXIT;
	td_manager_submit_request(&request, NULL);
    }
#endif

    pth_drop_natives(); 
    pth_scheduler_drop();
    pth_tcb_free(current);
    pth_initialized = FALSE;
    pth_shutdown_inprogress = TRUE;
    pth_number_of_natives = 0;
    pth_first_native.is_used = FALSE;
    pth_debug1("pth_kill: leave");
    if (pth_exit_count > 1)
	syscall(SYS_exit, 0);
    return TRUE;
}

/* scheduler control/query */
long pth_ctrl(unsigned long query, ...)
{
    long rc;
    va_list ap;

    rc = 0;
    va_start(ap, query);
    if (query == PTH_CTRL_GETTHREADS) {
	rc = pth_threads_count;
    } else if (query & PTH_CTRL_GETTHREADS_NEW) {
	if (pth_max_native_threads > 1)
	    rc += pqueue_total (&pth_NQ);
	else
	    rc += pqueue_total (&pth_native_list[0].new_queue);
    } else if (query & PTH_CTRL_GETTHREADS_READY) {
	if (pth_max_native_threads > 1)
	    rc += pqueue_total (&pth_RQ);
	else
	    rc += pqueue_total (&pth_native_list[0].ready_queue);
    } else if (query & PTH_CTRL_GETTHREADS_RUNNING) {
	int slot = 0;
	pth_t t;
	pth_acquire_lock(&pth_native_lock);
	while (pth_native_list[slot].is_used) {
	    if ((t = pth_native_list[slot].current) && (t != pth_native_list[slot].sched))
	         rc++;
	    slot++;
	}
	pth_release_lock(&pth_native_lock);
    } else if (query & PTH_CTRL_GETTHREADS_WAITING)
        rc += pth_WQ_count();
    else if (query & PTH_CTRL_GETTHREADS_SUSPENDED) 
        rc += pqueue_total (&pth_SQ);
    else if (query & PTH_CTRL_GETTHREADS_DEAD)
        rc += pqueue_total (&pth_DQ);
    else if (query & PTH_CTRL_GETAVLOAD) {
        float *pload = va_arg(ap, float *);
        *pload = pth_loadval;
    } else if (query & PTH_CTRL_GETPRIO) {
        pth_t t = va_arg(ap, pth_t);
        rc = __thread_prio_get (t);
    } else if (query & PTH_CTRL_GETNAME) {
        pth_t t = va_arg(ap, pth_t);
        rc = (long)t->name;
    } else if (query & PTH_CTRL_DUMPSTATE) {
        pth_dumpstate();
    } else
        rc = -1;
    va_end(ap);
    if (rc == -1)
        return -1;
    return rc;
}

/* create a new thread of execution by spawning a cooperative thread */
static void pth_spawn_trampoline(void)
{
    void *data;
    pth_t current = pth_get_current();

    pth_assert(current != NULL);

    pth_debug2("pth_spawn_trampoline: enter w/current 0x%x", current);

#ifdef THREAD_DB
    /* Make gdb aware of new thread, skipping the scheduler for debug */
    if (unlikely ((__pthread_doing_debug && current->state != PTH_STATE_SCHEDULER))) {
	if (unlikely (__pthread_threads_debug)) {
	    struct pthread_request request;
	    td_debug("pth_spawn_trampoline: doing REQ_DEBUG request submit");

	    /* Build a thread manager debug request and submit it */
	    request.req_thread = current;
	    request.req_kind = REQ_DEBUG;
	    td_manager_submit_request(&request, &(current->lock));
	}
    }
#endif

    /* just jump into the start routine */
    data = (*current->start_func)(current->start_arg);
    /* and do an implicit exit of the tread with the result value */
    pth_exit(data, FALSE);
    /* no return! */
    abort();
}


/* Original pth thread spawn API */
pth_t pth_spawn(pth_attr_t attr, void *(*func)(void *), void *arg)
{
    return pth_spawn_cb(attr, func, arg, NULL);
}

/* Intern version with additional arg for completion callout */
pth_t pth_spawn_cb(pth_attr_t attr, void *(*func)(void *), void *arg,
				    void (*thr_complete)(pth_t))
{
    pth_t thread;
    int create_suspended = 0;
    struct pqueue_st *pth_CQ = &pth_NQ;
    unsigned int stacksize;
    void *stackaddr;
    pth_t req_current;
    pth_descr_t req_descr;
    int scope = PTH_SCOPE_PROCESS;
    char c = (int)1;

    pth_debug1("pth_spawn: enter");

    /* allocate a new thread control block */
    stacksize = (attr == PTH_ATTR_DEFAULT ? pth_default_stacksize*1024 :
						     attr->a_stacksize); /*ibm*/
    stackaddr = (attr == PTH_ATTR_DEFAULT ? NULL : attr->a_stackaddr);
    if ((thread = pth_tcb_alloc(stacksize, stackaddr)) == NULL)
        return NULL;
    pth_debug2("pth_spawn_cb: new thread is 0x%x\n", thread);

    /* In certain cases, we need the requestor's current thread and
       descriptor... */
#ifdef THREAD_DB
    if (unlikely ((__pthread_doing_debug && thr_complete != NULL && arg != NULL))) {
	/* Pick up the current and descr from requesting thread */
	req_current = ((struct pthread_request *)arg)->req_thread;
	req_descr   = (req_current->boundnative != NULL) ?
				req_current->boundnative :
		      pth_find_native_by_tid(req_current->lastrannative);
    }
    else
#endif
    {
	/* Pick up current and descr from calling thread */
	req_descr   = pth_get_native_descr();
	req_current = req_descr->current;
    }

    /* configure remaining attributes */
    if (attr != PTH_ATTR_DEFAULT) {
            /* overtake from the attribute structure */
        if (attr->a_inheritsched && req_current) {
            /* unless inheriting, then get scheduling stuff from current  */
            thread->scope = scope = req_current->scope;
            thread->schedpolicy = req_current->schedpolicy;
            __thread_prio_set(thread, thread_prio_get (req_current, req_descr));
        }
        else {
            thread->scope = scope = attr->a_scope;
            thread->schedpolicy = attr->a_schedpolicy;
            __thread_prio_set(thread, xprio_to_iprio (
                                  thread->schedpolicy, attr->a_prio));
        }
        thread->joinable    = attr->a_joinable;
        thread->cancelstate = attr->a_cancelstate;
	create_suspended = attr->a_suspendstate; /*ibm*/
        pth_util_cpystrn(thread->name, attr->a_name, PTH_TCB_NAMELEN);
    }
    else if (req_current != NULL) {
        /* overtake some fields from the parent thread */
        thread->scope = scope;
        thread->schedpolicy = SCHED_OTHER;
        __thread_prio_set (thread, __thread_prio_get (req_current));
        thread->joinable    = req_current->joinable;
        thread->cancelstate = req_current->cancelstate;
        pth_snprintf(thread->name, PTH_TCB_NAMELEN, "%s.child@%d=0x%lx", 
                     req_current->name, (unsigned int)time(NULL), 
                     (unsigned long)req_current);
    }
    else {
        /* defaults */
        thread->scope = PTH_SCOPE_PROCESS;
        thread->schedpolicy = SCHED_OTHER;
        __thread_prio_set (thread, iprio_get_default (SCHED_OTHER));
        thread->joinable    = TRUE;
        thread->cancelstate = PTH_CANCEL_DEFAULT;
        pth_snprintf(thread->name, PTH_TCB_NAMELEN,
                     "user/%x", (unsigned int)time(NULL));
    }

    /* initialize the time points and ranges */
    pth_time_set_now(&thread->spawned);
    thread->lastran = thread->spawned;
    thread->running = pth_time_zero;

    /* initialize events */
    thread->events = NULL;

    if (req_current != NULL) {
	if (sigisemptyset(&req_current->sigactionmask))
	    sigemptyset(&thread->sigactionmask);
	else
	    memcpy((void *)&thread->sigactionmask,
		   &req_current->sigactionmask, sizeof(sigset_t));
    } else
	sigemptyset(&thread->sigactionmask);

    /* clear raised signals */
    memset(&thread->sigpending, 0x0, sizeof(thread->sigpending));
    thread->sigpendcnt = 0;
    thread->sigthread  = 0;

    /* remember the start routine and arguments for our trampoline */
    thread->start_func = func;
    thread->start_arg  = arg;

    /* initialize join argument */
    thread->join_arg = NULL;

    /* initialize thread specific storage */
    thread->data_value = NULL;
    thread->data_count = 0;

    /* initialize cancellaton stuff */
    thread->cancelreq   = FALSE;
    thread->cleanups    = NULL;

    /* initialize mutex stuff */
    pth_ring_init(&thread->mutexring);

    /*begin ibm*/
    /* for M:1, use first native's queues */
    if (pth_max_native_threads == 1) {
        pth_CQ = &req_descr->new_queue;
    }

#ifdef THREAD_DB
    /* callout for actions to take prior to starting the thread */
    if (thr_complete != NULL) {
	thr_complete (thread);
    }

    /* Set the thread id for debug */
    thread->td_tid = thread;
#endif

    /* initialize the machine context of this new thread */
    if (thread->stacksize > 0) { /* the "main thread" (indicated by == 0) is special! */
        if (!pth_mctx_set(&thread->mctx, pth_spawn_trampoline,
                          thread->stack, ((char *)thread->stack+thread->stacksize))) {
            pth_free_mem (thread->true_stack, thread->stacksize + 8);
	    if (thread->ptrfixed)
		thread = (pth_t)((char *)((char *)thread - 8));
	    pth_free_mem (thread, sizeof (struct pth_st) + 8);
	    return NULL;
        }
	pth_debug2("after pth_mctx_set for t0x%lx\n", thread);
    } else {
	/* if pth_main, use first native's queue */
	pth_CQ = &req_descr->new_queue;
    }

    /* set the signal mask for this context... */
    pth_sc(sigprocmask)(SIG_SETMASK, NULL, &(thread->mctx.sigs));
#if PTH_MCTX_MTH(mcsc)
    memcpy((void *)&(thread->mctx.uc.uc_sigmask), &(thread->mctx.sigs), sizeof(sigset_t));
#endif

    if (func != pth_scheduler) {
	/* Add in the global threads list */
	_pth_acquire_lock(&pth_native_lock, req_descr->tid);
        __thread_queue_add (thread);
	pth_threads_count++;
	_pth_release_lock(&pth_native_lock, req_descr->tid);
    } else
	return thread;

    /* if we're creating suspended, we need to put it on the "suspend queue"
     * instead of the "new queue". */
    if (create_suspended == TRUE) {
	pth_CQ = &pth_SQ;
    }

    if (scope == PTH_SCOPE_PROCESS) {
	/* 
	 * Check to see if we're allowed to create additional native
	 * threads and we've reached the threshold...
	 */
	if ( pth_max_native_threads > 1 && (pth_active_threads > 1) && 
	     (((pth_active_threads % pth_threads_per_native) == 0)
		|| (pth_active_threads-1 == 1) )) { 
	    /* 
	     * We are, now check to see if we've reached the max number of natives an'
	     * we've reached the threshold...
	     */
	    if ((pth_number_of_natives < pth_max_native_threads) &&
		(pth_number_of_natives < pth_active_threads)) {
		/*
		 * We're not yet at the maximum number of natives so it's time
		 * to create another native thread and start scheduling on it.
		 */
		pth_debug3("pth_spawn: Active natives (%i) less than allowed natives (%i), spawning new native.",
			pth_number_of_natives, pth_max_native_threads);

		if (pth_new_native(scope, 0) == NULL) {
		    pth_tcb_free (thread);
		    return NULL;
		}

		pth_debug2("pth_spawn: New native thread created, number of active native thread is %i.",
			pth_number_of_natives);

		/* switch pth_main running from first_native to newly created native */
		if (native_is_group_leader(req_descr) && pth_get_current() == pth_main) {
		    req_descr->is_bounded = TRUE;
		    pth_main->state = PTH_STATE_WAITING;
		    pth_main->boundnative = 0;
		    pth_yield(NULL);
		    req_descr = pth_get_native_descr();
		    if (native_is_group_leader(req_descr)) abort();
		    pth_main->state = PTH_STATE_READY;
		}
		/* Create the watchdog for fork'ed process... */
		if ( pth_fork_thread && !pth_watchdog_enabled && 
		    ((pth_max_native_threads == pth_number_of_natives) ||
		     (pth_threads_per_native > 1 && pth_number_of_natives > 2)) )
			pth_new_watchdog();
	    }
	}
    } else {
        if ((thread->boundnative = pth_new_native(scope, thread)) == NULL) {
            pth_tcb_free(thread);
            return NULL;
        }
        pth_CQ = &(thread->boundnative->ready_queue);
        pth_max_native_threads++;
    }

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	/* Signal the thread create event. Placement is after the 
	   new thread is built, put on global list, and native is
	   set up. The debugger sees it at this point and so all
	   needs to be in place for the thread discovery to work.    */

	/* Update the debug thread handles array to match global 
	   thread list, which should include just spawned thread.    */
	__td_sync_handles();

	/* Give the debugger control to process new thread event.    */
	__pthread_signal_thread_event(req_current, thread, TD_CREATE);
    }
#endif

    /* finally insert it into the "new queue" where
       the scheduler will pick it up for dispatching.
       Do this after creating new native, so bounded native
       can run this new thread.	
    */
      /* FIXME: look up which is the native with less load, add the
      **        thread to the ready queue of that native; get rid of
      **        the schedulers getting stuff from the global list;
      **        remove the global list. Implement a load balancer.
      */
    thread->state = PTH_STATE_NEW;
    pqueue_append_node(pth_CQ, &thread->node, req_descr);
    if (thread->boundnative) {
	thread->state = PTH_STATE_READY;
	if (thread->boundnative->is_running && thread->boundnative->is_bound == FALSE) {
	    _pth_acquire_lock(&(thread->boundnative->sched_lock), req_descr->tid);
	    pth_sc(write)(thread->boundnative->sigpipe[1], &c, sizeof(char));
	    _pth_release_lock(&(thread->boundnative->sched_lock), req_descr->tid);
	}
    }

    /*end ibm*/

    pth_debug1("pth_spawn: leave");

    /* the returned thread id is just the pointer
       to the thread control block... */
    return thread;
}

/* returns the current thread */
#if cpp
#define PTH_SELF()	pth_get_current()
#endif

pth_t pth_self(void)
{
    return pth_get_current();
}

/*begin ibm*/
/* return specified thread's stacksize */
int pth_getstacksize(pth_t thread, int *size)
{
    /* First, a quick check for a null... */
    if (size == NULL || thread == NULL)
	return FALSE;

    /* Now make sure the thread exists... */
    if (thread_exists (thread) == FALSE)
	return FALSE;

    /* Update the size... */
    *size = thread->stacksize;

    return (thread->stacksize != 0) ? thread->stacksize : (2048 * 1024);
}
strong_alias(pth_getstacksize, pthread_getstacksize_np)

/* return the specified thread's context */
void * pth_getcontext(pth_t t)
{
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
    /* First, a quick check for a null... */
    if (t == NULL)
	return NULL;

    /* 
     * Check to see if it is the current thread
     * and if so, update the context...
     */
    if (t == pth_get_current())
	pth_sigsetjmp(t->mctx.jb);
    
    /* Now make sure the thread exists... */
    else if (thread_exists(t) == FALSE)
	return NULL;
    
    return (void *)(t->mctx.jb);
#elif PTH_MCTX_MTH(mcsc)
    /* First, a quick check for a null... */
    if (t == NULL)
	return NULL;

    /* 
     * Check to see if it is the current thread
     * and if so, update the context...
     */
    if (t == pth_get_current())
	getcontext(&(t->mctx.uc));
    
    /* Now make sure the thread exists... */
    else if (thread_exists(t) == FALSE)
	return NULL;
    
    return (void *)(&(t->mctx.uc));
#else
    return NULL;
#endif
}
strong_alias(pth_getcontext, pthread_getcontext_np)

/* return the specified thread's context */
void * pth_getsp(pth_t t)
{
    /* First, a quick check for a null... */
    if (t == NULL)
	return NULL;

    /* Now make sure the thread exists... */
    if (thread_exists(t) == FALSE)
	return NULL;
    
    return (t->stack != NULL) ? (void *)(t->stack) : (void *)(t->sp);
}
strong_alias(pth_getsp, pthread_getsp_np)

/* return the current location of errno */
int * pth_geterrno(void)
{
    if (pth_get_current() == NULL) {
	pth_errno_storage = 0;
	return &pth_errno_storage;
    }
    return &(pth_get_current()->mctx.error);
}

/* bind the current thread to the last native thread it ran on */
int pth_bindtonative(pth_t t)
{
    pth_descr_t descr = pth_get_native_descr();

    /* If we're running M:1 mode, just return... */
    if (pth_max_native_threads == 1)
	return FALSE;

    /* already bounded native thread... */
    if (descr->is_bounded)
        return TRUE;

    /* Can't bind a null thread... */
    if (t == NULL || native_is_group_leader(descr))
	return FALSE;

    /* should not have any element in wait_queue.... */
    if (pqueue_total (&descr->wait_queue))
        return FALSE;

    t->boundnative = descr;
    descr->is_bounded = TRUE;
    descr->bounded_thread = t;
    pth_max_native_threads++;

    return TRUE;
}

/*end ibm*/

/* raise a signal for a thread */
int pth_raise(pth_t t, int sig)
{
    struct sigaction sa;

    if (t == NULL || !thread_exists(t))
        return ESRCH;
    if (sig < 0 || sig > PTH_NSIG)
        return EINVAL;
    if (sig == 0)
        /* thread exists... */
        return 0;
    else {
	pth_descr_t descr = pth_get_native_descr();
	pth_t current = descr->current;
	pth_descr_t save_native;
	
        /* raise signal for thread */
        if (__libc_sigaction(sig, NULL, &sa) != 0)
            return EINVAL;
        if (sa.sa_handler == SIG_IGN)
            return 0; /* fine, nothing to do, sig is globally ignored */
	_pth_acquire_lock(&(t->lock), descr->tid);
	if (!sigispending(t->sigpending, sig)) {
            sigaddpending(t->sigpending, sig);  /* add to sigpending list */
            t->sigpendcnt++;
	    t->sigthread = current; /* save signalled thread */
        }
	_pth_release_lock(&(t->lock), descr->tid);
	save_native = current->boundnative;
	if (sig == SIGSTOP && !native_is_group_leader(descr) && !current->boundnative)
		current->boundnative = &pth_first_native;
	if (t != current)
	    pth_yield(t);
	else 			/* if sending signal to current thread */
	    pth_yield(NULL);

	t->sigthread = 0;
	current->boundnative = save_native;
        return 0;
    }
}
strong_alias(pth_raise, pthread_kill)

/* Get the current thread for the current native. */
#ifndef NATIVE_SELF
intern pth_t _pth_get_current(void)
{
    pth_descr_t descr = NULL;

    if ((descr = pth_get_native_descr()) == NULL)
	return NULL;

    return descr->current;
}
#endif

/* Set the current thread for a the current native. */
intern pth_t pth_set_current(pth_t new_current)
{
    pth_descr_t descr = pth_get_native_descr();

    if (descr != NULL) {
	descr->current = new_current;
	return new_current;
    }

    return NULL;
}

/* Get the native thread by it's id */
intern pth_descr_t pth_find_native_by_tid(pid_t tid)
{
    int slot = 0;
    while (slot < pth_number_of_natives) {
    	if (pth_native_list[slot].is_used && (pth_native_list[slot].tid == tid))
	    return &pth_native_list[slot];
	slot++;
    }
    return NULL;
}

/* Get the current native thread info */
#ifndef NATIVE_SELF
intern pth_descr_t _pth_get_native_descr(void)
{
    char *sp = (char *)&sp;
    int slot  = 0;
    pid_t tid = 0;

    if (pth_initialized_minimal != TRUE)
	pth_initialize_minimal();

    /* Do it the really easy way... */
    if (pth_first_native.current == NULL) {
	return &pth_first_native;
    }

    /* Do it the easy way... */
    while (slot < pth_number_of_natives) {
	if ( pth_native_list[slot].is_used && pth_native_list[slot].current != NULL) {
	     if ( sp <= (char *)(pth_native_list[slot].current->stackbottom) &&
	          sp >= (char *)(pth_native_list[slot].current->stack) ) {
		return &pth_native_list[slot];
	     }
	}
	slot++;
	pth_assert(slot >= 0 && slot <= PTH_MAX_NATIVE_THREADS);
    }

    /* Do it the hard way... */
    slot = 0;
    tid = k_gettid();
    _pth_acquire_lock(&pth_native_lock, tid);
    while (slot < pth_number_of_natives) {
    	if (pth_native_list[slot].is_used && (pth_native_list[slot].tid == tid)) {
    	    _pth_release_lock(&pth_native_lock, tid);
	    pth_assert(pth_native_list[slot].sched_index == slot);
	    return &pth_native_list[slot];
	}
	slot++;
    }
    _pth_release_lock(&pth_native_lock, tid);
    return NULL;
}
#endif

/* Get the primordial thread descriptor */
intern pth_descr_t pth_primordial_thread(void)
{
    if (pth_first_native.is_used == FALSE)
	return NULL;
    return &pth_first_native;
}

/* cleanup a particular thread */
intern void pth_thread_cleanup(pth_t thread)
{
    /* run the cleanup handlers */
    if (thread->cleanups != NULL)
        pth_cleanup_popall(thread, TRUE);

    /* release still acquired mutex variables */
    pth_mutex_releaseall(thread);

    return;
}

static int pth_exit_cb(void *arg) __attribute__ ((unused));
static int pth_exit_cb(void *arg)
{
    int rc;

    /* count of current running threads on all natives */
    rc = pth_active_threads;

    pth_debug2("pth_exit_cb: thread terminating, there are %d threads remaining...", rc);
    if (rc == 1 /* just our main thread */)
        return TRUE;
    else
        return FALSE;
}

intern void pth_exit_wrapper(int code)
{
    char c = (int)1;
    if (pth_first_native.tid != current_tid())
	pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
    _exit(code);
}

void pth_exit(void *value, int wait_flag)
{
    pth_descr_t descr = pth_get_native_descr();
    pth_t dying_thread = descr->current;

    pth_debug3("pth_exit: marking thread 0x%lx \"%s\" as dead", dying_thread, dying_thread->name);

    if (dying_thread == pth_main && !wait_flag)  /* return from main() */
        goto exit_process;
 
    /* exit the process only when thread count becomes 1 */
    if (pth_active_threads > 1) {
	/* if not canceled ... */
	if (dying_thread->state != PTH_STATE_DEAD) {
		/* execute cleanups */
		pth_thread_cleanup(dying_thread);

		/* cleanup may have changed the native thread... */
		descr = pth_get_native_descr();
	
#ifdef THREAD_DB
		/* if thread debugging is enabled, alert the thread manager */
		if (__builtin_expect (__pthread_doing_debug, 0))
		    __pthread_signal_thread_event(dying_thread, dying_thread,
								   TD_DEATH);
#endif
		/* mark the current thread as dead, so the scheduler removes us */
		dying_thread->join_arg = value;
		dying_thread->state = PTH_STATE_EXIT;
	}

        /*
         * Now we explicitly switch into the scheduler and let it
         * reap the current thread structure; we can't free it here,
         * or we'd be running on a stack which malloc() regards as
         * free memory, which would be a somewhat perilous situation.
         */
        pth_debug3("pth_exit: switching from thread 0x%lx \"%s\" to scheduler", dying_thread, dying_thread->name);
	_pth_acquire_lock(&descr->sched_lock, descr->tid);
        pth_mctx_switch(&dying_thread->mctx, &descr->sched->mctx);
        abort(); /* not reached! */
    } else {
exit_process:
        /* 
         * Now, exit the _process_ 
         */ 
        pth_debug3("pth_exit: main 0x%lx \"%s\" exiting", dying_thread, dying_thread->name);

        pth_thread_cleanup(dying_thread);

#ifdef THREAD_DB
	/* if thread debugging is enabled, alert the thread manager */
	if (unlikely (__pthread_doing_debug))
	    if (unlikely (__pthread_threads_debug))
		__pthread_signal_thread_event(dying_thread, dying_thread, TD_DEATH);
#endif

        exit(0);
        abort(); /* not reached! */
    }
}
strong_alias(pth_exit, pthread_exit)
strong_alias(pth_exit, __pthread_exit)

/* waits for the termination of the specified thread */
int pth_join(pth_t thread, void **value)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_descr_t descr = pth_get_native_descr();
    pid_t ptid = descr->tid;
    
    fdebugmsg (NGPT_DEBUG_JOIN, "%s (%p, %p): joining thread (\"%s\")\n",
               __FUNCTION__, thread, value,
               thread == NULL ? "-ANY-" : thread->name);

    /* Null thread? Ok, pickup the next dead one */    
    if (thread == descr->current)
        return EDEADLK;
    if (thread == NULL) {
        thread = thread_by_node (pqueue_first_node (&pth_DQ, descr));
    	if (thread == NULL)
	    return ESRCH;
    }

    spin_lock (&pth_DQ.lock, descr, NULL);
    if (!thread_exists(thread)) {
        spin_unlock (&pth_DQ.lock, descr);
	return ESRCH;
    }
    if (!thread->joinable) {
        spin_unlock (&pth_DQ.lock, descr);
	return EINVAL;
    }
    if (pth_ctrl(PTH_CTRL_GETTHREADS) == 1) {
        spin_unlock (&pth_DQ.lock, descr);
        return EDEADLK;
    }
    if (thread->state != PTH_STATE_DEAD) {
        ev = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD|PTH_MODE_STATIC, &ev_key, thread);
	if (descr->is_bounded || 
	    (pth_number_of_natives > 1 && native_is_group_leader(descr))) 
	    thread->joined_native = descr;           /* save for wakeup call */
	else
	    thread->joined_thread = descr->current;  /* save current */
	thread->joined_count++;

        spin_unlock (&pth_DQ.lock, descr);
        pth_wait(ev);
        descr = pth_get_native_descr();
	ptid = descr->tid;
        spin_lock (&pth_DQ.lock, descr, NULL);
    }

    if (!__pqueue_contains (&pth_DQ, &thread->node)
        || --thread->joined_count > 0) {
        spin_unlock (&pth_DQ.lock, descr);
	return ESRCH;
    }
    if (thread->state != PTH_STATE_DEAD) {
        spin_unlock (&pth_DQ.lock, descr);
	return ESRCH;
    }
    if (value != NULL)
        *value = thread->join_arg;
    __pqueue_delete_node (&thread->node);
    spin_unlock (&pth_DQ.lock, descr);
    pth_tcb_free (thread);
    return 0;
}

/* delegates control back to scheduler for context switches */
int pth_yield(pth_t to)
{
    volatile pth_descr_t descr = pth_get_native_descr();
    pth_t current = descr->current;

    fdebugmsg (NGPT_DEBUG_YIELD, "%s (%p): entering from thread %p (\"%s\")\n",
               __FUNCTION__, to, current, current->name);

    /* We cannot yield to ourselves ... well, we can, makes no
    ** sense, though :) */
    if (unlikely (to == current))
        return TRUE;
    
    /* If we are yielding to favor somebody, make that somebody head
    ** of its priority list. BTW, if the thread is not in any pqueue,
    ** we just do nothing. FIXME: may be we want to check for that? */
    if (to != NULL) {
         /* If 'to' is bounded, we'll need to favor it if it is not
         ** the current on it's native and then signal its scheduler. */
	if (to->boundnative && (to->boundnative->is_bounded)) {
	    char c = (int)1;

            _pth_acquire_lock (&to->boundnative->sched_lock, descr->tid); 
            if (to != descr->current)
                pqueue_favor (&to->node, descr);
            pth_sc(write)(to->boundnative->sigpipe[1], &c, sizeof(char));
            _pth_release_lock (&to->boundnative->sched_lock, descr->tid); 
            return TRUE;
        }
        else
            pqueue_favor (&to->node, descr);
    }

    /* Now, go to the scheduler, we'll come back :) */
    fdebugmsg (NGPT_DEBUG_YIELD,
               "%s (%p): current %p (%s) yielding (schedule) to %p (%s)\n",
               __FUNCTION__, to, current, current->name, to,
               to? to->name : "any thread");

    _pth_acquire_lock (&descr->sched_lock, descr->tid); 
    pth_mctx_switch(&current->mctx, &descr->sched->mctx);

    fdebugmsg (NGPT_DEBUG_YIELD,
               "%s (%p): current %p (%s) came back from yield (scheduler)\n",
               __FUNCTION__, to, current, current->name);
    return TRUE;
}

/* suspend a thread until its again manually resumed */
int pth_suspend(pth_t t)
{
    int slot = 0;
    struct pqueue_st *q;
    pth_descr_t ds = pth_get_native_descr();

    if (t == NULL)
        return EINVAL;
    if (t == ds->current)
        return EPERM;
    if (ds->sched == t)
        return EPERM;

    /* 
     * Check to see if t is running on another native.
     * If it is, mark the thread for suspension and return EINPROGESS,
     *	otherwise, just continue...
     */
    while (slot < pth_number_of_natives) {
	if (t == pth_native_list[slot].current) {
	    t->should_suspend = TRUE;
	    return EINPROGRESS;
	}
	slot++;
    }

    if (t->boundnative && (t->boundnative->is_bounded)) {
	ds = t->boundnative;
	if (t->state == PTH_STATE_NEW)
	    q = &ds->ready_queue;
	else if (t->state == PTH_STATE_WAITING)
	    q = &ds->wait_queue;
	else
	    return EPERM;
	goto found;
    }
    switch (t->state) {
        case PTH_STATE_NEW:
	    q = (pth_number_of_natives > 1) ? &pth_NQ : &(ds->new_queue); break;
        case PTH_STATE_READY:
	    q = (pth_number_of_natives > 1) ? &pth_RQ : &(ds->ready_queue); break;
        case PTH_STATE_WAITING: q = &pth_WQ; break;
        default:                q = NULL;
    }
    if (q == NULL)
        return EPERM;
    
    if (t->state == PTH_STATE_WAITING) {
	if (!pth_WQ_delete(t))
	    return ESRCH;
    } else {
found:
	if (!__pqueue_contains (q, &t->node))
	    return ESRCH;
	pqueue_delete_node (&t->node, ds);
    }
    pqueue_append_node (&pth_SQ, &t->node, ds);
    pth_debug3("pth_suspend: suspending thread 0x%lx %s\n", t, t->name);
    return 0;
}
strong_alias(pth_suspend, pthread_suspend_np)

/* resume a previously suspended thread */
int pth_resume(pth_t t)
{
    struct pqueue_st *q = NULL;
    pth_descr_t ds = pth_get_native_descr();

    if (t == NULL || ds == NULL)
        return EINVAL;
    if (t == ds->current)
        return EPERM;
    if (ds->sched == t)
        return EPERM;
    if (!__pqueue_contains (&pth_SQ, &t->node))
        return EPERM;
    
    pqueue_delete_node (&t->node, ds);

    if (t->boundnative && (t->boundnative->is_bounded)) {
	ds = t->boundnative;
	if (t->state == PTH_STATE_NEW)
	    q = &ds->ready_queue;
	else if (t->state == PTH_STATE_WAITING)
	    q = &ds->wait_queue;
	else
	    q = NULL;
    } else {
	switch (t->state) {
	    case PTH_STATE_NEW:
		q = (pth_number_of_natives > 1) ? &pth_NQ : &(ds->new_queue); break;
	    case PTH_STATE_READY:
		q = (pth_number_of_natives > 1) ? &pth_RQ : &(ds->ready_queue); break;
	    case PTH_STATE_WAITING: q = &(ds->wait_queue); break;
	    default:                q = NULL;
	}
    }
    if (q == NULL)
        return EPERM;
    pqueue_append_node (q, &t->node, ds);
    pth_debug3("pth_resume: resuming thread 0x%lx %s\n", t, t->name);
    pth_yield (t);
    return 0;
}
strong_alias(pth_resume, pthread_resume_np)

/* switch a filedescriptor's I/O mode */
int pth_fdmode(int fd, int newmode)
{
    int fdmode = 0;
    int oldmode = 0;

    /* retrieve old mode (usually cheap) */
    if ((fdmode = __libc_fcntl(fd, F_GETFL, NULL)) == -1)
        return PTH_FDMODE_ERROR;
    if (fdmode & O_NONBLOCKING) {
        oldmode = PTH_FDMODE_NONBLOCK;
	if (newmode == PTH_FDMODE_BLOCK)
	    __libc_fcntl(fd, F_SETFL, (fdmode & ~(O_NONBLOCKING)));
    } else {
        oldmode = PTH_FDMODE_BLOCK;
	if (newmode == PTH_FDMODE_NONBLOCK)
	    __libc_fcntl(fd, F_SETFL, (fdmode | O_NONBLOCKING));
    }

    /* return old mode */
    return oldmode;
}

/* wait for specific amount of time */
int pth_nap(pth_time_t naptime)
{
    pth_time_t until;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;

    if (naptime == pth_time_zero)
        return FALSE;
    pth_time_set_now(&until);
    pth_time_add(&until, &naptime);
    ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key, until);
    pth_wait(ev);
    return TRUE;
}

/* runs a constructor once */
int pth_once(pth_once_t *oncectrl, void (*constructor)(void *), void *arg)
{
    static pth_qlock_t once_init_lock;
    pid_t tid = k_gettid();
    int	do_once_init;

    if (oncectrl == NULL || constructor == NULL)
        return EINVAL;
    _pth_acquire_lock(&once_init_lock, tid);
    do_once_init = (*oncectrl != TRUE);
    *oncectrl = TRUE;
    _pth_release_lock(&once_init_lock, tid);
    if (do_once_init)
        constructor(arg);
    return 0;
}

