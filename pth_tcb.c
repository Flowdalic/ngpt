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
**  pth_tcb.c: Pth thread control block handling
*/
                             /* Patient: Doctor, it hurts when I do this!
                                Doctor: Well, then don't do it. */
#include "pth_p.h"
#include "allocation.h"
#include "pqueue.h"
#include "schedule.h"

#if cpp

enum __libc_tsd_key_t {
    _LIBC_TSD_KEY_MALLOC = 0,
    _LIBC_TSD_KEY_DL_ERROR,
    _LIBC_TSD_KEY_RPC_VARS,
    _LIBC_TSD_KEY_LOCALE,
    _LIBC_TSD_KEY_CTYPE_B,
    _LIBC_TSD_KEY_CTYPE_TOLOWER,
    _LIBC_TSD_KEY_CTYPE_TOUPPER,
    _LIBC_TSD_KEY_N
};

#define THREAD_SETMEM_NC(tid, member, value) tid->member = (value)
#define THREAD_GETMEM_NC(tid, member) tid->member

extern void *(*__libc_internal_tsd_get)(enum __libc_tsd_key_t) __THROW;
extern int (*__libc_internal_tsd_set)(enum __libc_tsd_key_t, __const void *) __THROW;

#define PTH_TCB_NAMELEN 16
typedef pid_t	pth_native_t;	/*ibm*/

    /* thread control block */
struct pth_st {
    /* machine context - LEAVE mctx AS FIRST MEMBER OF THIS STRUCTURE                   */
    pth_mctx_t     mctx;                 /* last saved machine state of thread          */
    void           *magic;               /* Speed up to verify if this thread exists    */
    pth_descr_t    boundnative;		 /* Native thread this tcb is bound to or NULL  */
    pth_native_t   lastrannative;        /* Last native thread this tcb ran on          */
    char          *stack;                /* pointer to thread stack                     */
    char	  *true_stack;		 /* pointer to the true stack                   */
    char	  *sp;			 /* pointer to "guessed" stack if stack is 0    */
#if PTH_NEED_SEPARATE_REGISTER_STACK
    char	  *regstack;		 /* pointer to register stack on IA64           */
#endif
    unsigned int   stacksize;            /* size of thread stack                        */
    long          *stackguard;           /* stack overflow guard                        */
    char	  *stackbottom;		 /* stack bottom				*/
    void        *(*start_func)(void *);  /* start routine                               */
    void          *start_arg;            /* start argument                              */

    /* priority, priority queue and scheduling handling */
    struct node_st node;
    unsigned       priority:8;           /* Static priority [Internal! check priorities.h] */
    unsigned       schedpolicy:2;        /* Scheduling policy                           */
    unsigned       scope:1;              /* Scheduling scope                            */
    unsigned       inheritsched:1;       /* Inherit scheduler parameters                */
    
    /* standard thread control block ingredients */
    char           name[PTH_TCB_NAMELEN];/* name of thread (mainly for debugging)       */
    pth_state_t    state;                /* current state indicator for thread          */
    unsigned       stackloan:1;          /* stack type                                  */
    unsigned	   should_suspend:1;	 /* True if thread should suspend at earliest   */
    unsigned       cancelreq:1;          /* cancellation request is pending             */
    unsigned	   ptrfixed:1;		 /* alignment: 1 if ptr was adjusted, 0 otherwise  */ 

    /* timing                                                                           */
    pth_time_t     spawned;              /* time point at which thread was spawned      */
    pth_time_t     lastran;              /* time point at which thread was last running */
    pth_time_t     running;              /* time range the thread was already running   */

    /* event handling */
    pth_event_t    events;               /* events the tread is waiting for             */

    /* per-thread signal handling */
    int            sigpending[2];        /* internal set for 64 pending signals         */
    int            sigpendcnt;           /* number of pending signals                   */
    pth_t	   sigthread;		 /* thread who sent signal			*/

    /* thread joining */
    int            joinable;             /* whether thread is joinable                  */
    void          *join_arg;             /* joining argument                            */

    /* per-thread specific storage */
    const void   **data_value;           /* thread specific  values                     */
    int            data_count;           /* number of stored values                     */

#ifdef USE_TSD
    /* per-thread specific storage for libc */
    void	  *libc_specific[_LIBC_TSD_KEY_N]; /* libc thread specific values	*/
#endif

    /* cancellation support */
    unsigned int   cancelstate;          /* cancellation state of thread                */
    pth_cleanup_t  *cleanups;             /* stack of thread cleanup handlers           */

    /* mutex ring */
    pth_ring_t     mutexring;            /* ring of aquired mutex structures            */

    /* lock */
    pth_qlock_t	   lock;		 /* used to lock the thread if needed           */

    /* threads queue elements */
    struct lnode_st thread_queue;	 /* for insertion in a global threads list 	*/
    sigset_t       sigactionmask;        /* set for sigaction mask                      */
    pth_list_t	   mutex_cond_wait;	 /* waiting for mutex/cond variable		*/
    int            joined_count;	 /* keep track of count waiting for join	*/
    pth_cleanup_t  cleanup_buf[PTH_CLEANUP_COUNT]; /* cleanup buffers to use first	*/
    int		   cleanup_count;	 /* cleanup record counts			*/
    pth_descr_t    joined_native;	 /* waiting for join and it's bounded		*/
    pth_t          joined_thread;	 /* store thread which is waiting for join	*/
    pth_descr_t    waited_native;	 /* thread is in this native's wait queue	*/
    pth_mutex_t	   *mutex_owned;	 /* currently holding this mutex		*/	
    const void     **event_data;         /* thread specific event data                  */
    int            event_count;          /* number of stored events                     */
#ifdef THREAD_DB
    pth_t	   td_tid;		 /* thread identifier                 */
    int		   td_nr;		 /* thread handle index               */
    int		   td_retcode;		 /* thread manager return status      */
    void *	   td_retval;		 /* thread manager return value       */
    int		   td_report_events;	 /* thread eventing on/off            */
    td_eventbuf_t  td_events;            /* thread event struct for thread_db */
#endif
};

#endif /* cpp */

intern const char *pth_state_names[] = {
    "scheduler", "new", "ready", "running", "waiting", "dead"
};

#if defined(MINSIGSTKSZ) && !defined(SIGSTKSZ)
#define SIGSTKSZ MINSIGSTKSZ
#endif
#if !defined(SIGSTKSZ)
#define SIGSTKSZ 8192
#endif

#if cpp
#define PTH_ELEMENT_INSERT(tq, all_tq) \
do { \
    (tq)->th_next = (all_tq)->th_next; \
    (tq)->th_prev = (all_tq); \
    (all_tq)->th_next->th_prev = (tq); \
    (all_tq)->th_next = (tq); \
} while (0)
#endif
void pth_element_insert(pth_list_t *tq, pth_list_t *all_tq)
{
    PTH_ELEMENT_INSERT(tq, all_tq);
}

#if cpp
#define PTH_ELEMENT_DELETE(tq) \
do { \
    (tq)->th_prev->th_next = (tq)->th_next; \
    (tq)->th_next->th_prev = (tq)->th_prev; \
    (tq)->th_next = 0; \
} while (0)
#endif
void pth_element_delete(pth_list_t *tq)
{
    PTH_ELEMENT_DELETE(tq);
}

/* allocate a thread control block */
intern pth_t pth_tcb_alloc(unsigned int stacksize, void *stackaddr)
{
    pth_t t;
    char *stack = NULL;

    if (stacksize > 0 && stacksize < SIGSTKSZ)
        stacksize = SIGSTKSZ;
    /*
     * Some platforms (IA64) require 16 byte alignment for the
     * jmpbuf.  The following code will force the tcb to be
     * aligned on a 16 byte boundary.  Worst case on a platform
     * that doesn't require it, we waste 8 bytes.
     */
    
    t = pth_malloc (sizeof (struct pth_st) + 8);
    if (t == NULL)
	return(NULL);

    if ((long)t % 16L != 0L) {                                          /*ibm*/
        t = (pth_t)((char *)((char *)t + 8));                           /*ibm*/
	t->ptrfixed = 1;						/*ibm*/
    } else								/*ibm*/
	t->ptrfixed = 0;						/*ibm*/

    lnode_init (&t->thread_queue);
    node_init (&t->node);
    t->stacksize  = stacksize;
    t->stack      = NULL;
    t->stackguard = NULL;
    t->stackloan  = (stackaddr != NULL ? TRUE : FALSE);
    pth_lock_init(t->lock);
    t->boundnative   = 0; /*ibm*/
    t->lastrannative = 0; /*ibm*/
    t->joined_native = 0; /*ibm*/
    t->joined_count  = 0; /*ibm*/
    t->mutex_cond_wait.th_next = 0;
    t->cleanup_count  = 0; /*ibm*/
    t->joined_thread = 0; /*ibm*/
    t->waited_native = 0; /*ibm*/
    t->mutex_owned = 0; /*ibm*/
    t->should_suspend = FALSE;
    if (stacksize > 0) { /* stacksize == 0 means "main" thread */
        if (stackaddr != NULL) {
            t->stack = (char *)(stackaddr);
	    t->true_stack = t->stack;
	} else {
#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
	    size_t  pagesize = getpagesize();				/*ibm*/
	    size_t  npages = stacksize / pagesize;			/*ibm*/

	    /* Both ends of the stack need to page aligned */		/*ibm*/
	    t->stacksize = (stacksize % pagesize == 0)			/*ibm*/
			 ? npages * pagesize				/*ibm*/
			 : (++npages) * pagesize;			/*ibm*/
#endif
            stack = pth_malloc (t->stacksize + 8);
            if (stack == NULL) {
                pth_free_mem (t, sizeof(struct pth_st)+8);
                return(NULL);
	    }

	    t->true_stack = stack;

	    /* Align it if necessary... */
	    if ((long)stack % 16L != 0L)
		stack = ((char *)(stack + 8));
	    t->stack = stack;

        }
#if PTH_NEED_SEPARATE_REGISTER_STACK > 0
	/*									      ibm 
	 * On IA64 the stack will always grow down.  The register stack will          ibm
	 * grow up towards the main stack.                                            ibm
	 */
	 
	/* Guard is at the lowest address, alignment is guaranteed.        */	    /*ibm*/
	/* Note: this guard address is actually in the middle.  Corruption */	    /*ibm*/
	/* could mean that either the main stack or the the register stack */       /*ibm*/
	/* has overflowed.                                                 */       /*ibm*/
	t->stackguard = (long *)((char *)t->stack + (t->stacksize / 2));	    /*ibm*/

	/* Register stack is at the highest address, alignment guaranteed. */	    /*ibm*/
	t->regstack = (char *)(t->stack + t->stacksize);			    /*ibm*/
#else
#if PTH_STACKGROWTH < 0
        /* guard is at lowest address (alignment is guarrantied) */
        t->stackguard = (long *)((long)t->stack); /* double cast to avoid alignment warning */
	t->stackbottom = (char *)(t->stack + stacksize);
#else
        /* guard is at highest address (be careful with alignment) */
        t->stackguard = (long *)(t->stack+(((stacksize/sizeof(long))-1)*sizeof(long)));
	t->stackbottom = (char *)(t->stack - stacksize);
#endif
#endif
        *t->stackguard = 0xDEAD;
    } else {
	t->true_stack = 0;
	t->sp = (char *)&stack;
    }
    return t;
}

/* free a thread control block */
intern void pth_tcb_free(pth_t t)
{
    int i = 0;
    pth_descr_t descr = pth_get_native_descr();
    pid_t tid = (descr != NULL) ? descr->tid : k_gettid();
    pth_descr_t ds;
    
    pth_debug2("pth_tcb_free: free called for 0x%lx", t);
    if (t == NULL)
        return;
    _pth_acquire_lock(&(t->lock), tid);

    /* Cleanup the stack. (can't unmap the stack for current running thread) */
    if (t->true_stack != NULL && !t->stackloan && t != descr->current)
    {
        pth_free_mem (t->true_stack, t->stacksize + 8);
    }

    /* Run the specific data destructors */
    if (t->data_value != NULL)
	pth_key_destroydata(t);

    if (t->event_data != NULL)
	pth_event_key_destroy(t);

    /* finish the cleanups... */
    if (t->cleanups != NULL)
        pth_cleanup_popall(t, FALSE);

    /*begin ibm*/
    if ((ds = t->boundnative) && ds->is_bounded) {
	/*clean up bound thread*/;
	ds->bounded_thread = 0; 
    }

    if (t->mutex_cond_wait.th_next != 0)
	pth_element_delete(&t->mutex_cond_wait);
    
    _pth_release_lock(&(t->lock), tid);

    /*
     * The final step is to remove ourselves from any other native
     * thread that happens to be waiting on a timer event for this
     * thread...
     */
    while (pth_native_list[i].is_used) {
	descr = &pth_native_list[i++];
	if (descr->is_bounded)
	    continue;
	if (descr->nexttimer_thread == t && descr->tid != tid)
	    descr->nexttimer_thread = NULL;
    }
    
    /* Delete from the global threads list */
    if (t->start_func != pth_scheduler) {
	_pth_acquire_lock(&pth_native_lock, tid);
	__thread_queue_remove (t);
	pth_threads_count--;
	_pth_release_lock(&pth_native_lock, tid);
    }

#ifdef THREAD_DEBUG
    /* clean up __pthread_handles as global list changed */
    if (__builtin_expect (__pthread_doing_debug, 0)) {
	td_debug("pth_tcb_free: thread teardown\n");
	__td_sync_handles();
    }
#endif

    if (pth_exit_count > 0 && (t == descr->current) && (descr == &pth_first_native))
	descr->current = NULL;
     
    /* 
     * If we adjusted the pointer previously to align on 16 byte boundary, 
     * we need to put it back the proper way now.
     */
    if (t->ptrfixed)
	t = (pth_t)((char *)((char *)t - 8));
    /*ibm end*/

    pth_free_mem (t, sizeof (struct pth_st) + 8);
    return;
}

#ifdef USE_TSD
/* Thread specific data for libc. */

intern int _libc_internal_tsd_set(enum __libc_tsd_key_t key, const void * p)
{
    pth_t tid = pth_get_current();

    if (tid == NULL)
	return 1;

    THREAD_SETMEM_NC(tid, libc_specific[key], (void *)p);
    __libc_internal_tsd_get = _libc_internal_tsd_get; 

    return 0;
}

int (*__libc_internal_tsd_set)(enum __libc_tsd_key_t key, const void * p)
	= NULL;

intern void * _libc_internal_tsd_get(enum __libc_tsd_key_t key)
{
    pth_t tid = pth_get_current();

    if (tid == NULL)
	return NULL;

    return THREAD_GETMEM_NC(tid, libc_specific[key]);

}

void * (*__libc_internal_tsd_get)(enum __libc_tsd_key_t key)
	= NULL;
#endif
