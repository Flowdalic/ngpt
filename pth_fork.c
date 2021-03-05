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
**  pth_fork.c: Pth forking support
*/
                             /* ``Every day of my life
                                  I am forced to add another
                                  name to the list of people
                                  who piss me off!''
                                            -- Calvin          */
#include "pth_p.h"
#include "allocation.h"
#include "list.h"
#include "priorities.h"
#include "pqueue.h"
#include "schedule.h"
#include <sched.h>

intern struct pth_atfork_st pth_atfork_list[PTH_ATFORK_MAX];
static int pth_atfork_idx = 0;
intern int pth_fork_initialized = FALSE;

int pth_atfork_push(void (*prepare)(void *), void (*parent)(void *),
                    void (*child)(void *), void *arg)
{
    if (pth_atfork_idx > PTH_ATFORK_MAX-1)
        return FALSE;
    pth_atfork_list[pth_atfork_idx].prepare = prepare;
    pth_atfork_list[pth_atfork_idx].parent  = parent;
    pth_atfork_list[pth_atfork_idx].child   = child;
    pth_atfork_list[pth_atfork_idx].arg     = arg;
    pth_atfork_idx++;
    return TRUE;
}

int pth_atfork_pop(void)
{
    if (pth_atfork_idx <= 0)
        return FALSE;
    pth_atfork_idx--;
    return TRUE;
}

intern int pth_fork_init(void)
{
    int slot = 0;
    pth_attr_t t_attr;

#ifdef NATIVE_SELF
    pth_descr_t descr = NATIVE_SELF;
#else
    pth_descr_t descr = pth_get_native_descr();
#endif

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
 
    /* initialize the scheduler */
    pth_scheduler_init();
 
    /* spawn the scheduler thread */
    t_attr = pth_attr_new();
    if (t_attr == NULL) {
        fprintf(stderr,"pth_fork_init: unable to allocate initial attribute !?!?!?!\n");
        abort();
    }
    /* Schedulers always get maximum priority :), actually, real-time */
    pth_attr_set(t_attr, PTH_ATTR_PRIO,         xprio_get_max (SCHED_FIFO));
    pth_attr_set(t_attr, PTH_ATTR_SCHEDPOLICY,  SCHED_FIFO);
    pth_attr_set(t_attr, PTH_ATTR_NAME,         "**SCHEDULER**");
    pth_attr_set(t_attr, PTH_ATTR_JOINABLE,     FALSE);
    pth_attr_set(t_attr, PTH_ATTR_CANCEL_STATE, PTH_CANCEL_DISABLE);
    pth_attr_set(t_attr, PTH_ATTR_STACK_SIZE,   pth_default_stacksize*1024);
    pth_attr_set(t_attr, PTH_ATTR_STACK_ADDR,   NULL);
    descr->sched = PTH_SPAWN(t_attr, pth_scheduler, NULL);
    if (descr->sched == NULL) {
        pth_attr_destroy(t_attr);
        close(pth_native_list[0].sigpipe[0]);
        close(pth_native_list[0].sigpipe[1]);
        abort();
    }
    descr->sched->lastrannative = descr->tid;
    pth_attr_destroy(t_attr);
    descr->current = descr->sched;

    memset(&pth_atfork_list, 0x0, PTH_ATFORK_MAX * sizeof(struct pth_atfork_st));
    return TRUE;
}

pid_t pth_fork(void)
{
    pid_t pid;
    int i;
    pth_descr_t descr;
    pth_t current;

    /* above prepare routine might have changed our native, so check here... */
    if ((descr = pth_get_native_descr()) == NULL) {
	fprintf(stderr, "pth_fork: unable to find native task for pid %i.\n", k_gettid());
	return -1;
    }
    current = descr->current;
    /* if not running on first native, bound and run under first native.... */
    if (!descr->is_bounded) {
try_again:
	if (pth_first_native.tid != descr->tid) {
	    current->boundnative = &pth_first_native;
	    pth_yield(NULL);
	    descr = pth_get_native_descr();
	    goto try_again;
	}
    }

    /* run preparation handlers in LIFO order */
    for (i = pth_atfork_idx-1; i >= 0; i--)
        if (pth_atfork_list[i].prepare != NULL)
            pth_atfork_list[i].prepare(pth_atfork_list[i].arg);

    /* fork the process */
    if ((pid = pth_sc(fork)()) == -1)
        return pid;

    /* handle parent and child contexts */
    if (pid != 0) {
        /* Parent: */

        /* run parent handlers in FIFO order */
        for (i = 0; i <= pth_atfork_idx-1; i++)
            if (pth_atfork_list[i].parent != NULL)
                pth_atfork_list[i].parent(pth_atfork_list[i].arg);
    } else {
        /* Child: */

	/* update the descriptor values... */
	descr->pid	= getpid();
	descr->tid	= k_gettid();
	descr->current	= current;
	pth_fork_thread = current;
	pth_fork_initialized = TRUE;

	INIT_NATIVE_SELF(descr, 0);

	/* initialize global locks to avoid any deadlock */
	pth_lock_init(pth_key_lock);
	pth_lock_init(pth_usr1_lock);
	pth_lock_init(pth_native_lock);
	pth_lock_init(pth_init_lock);

	cal_fork_init(&gcal);

        /* kick out all threads except for the current one and the scheduler */
        pth_fork_drop_threads(current);

        /* run child handlers in FIFO order */
        for (i = 0; i <= pth_atfork_idx-1; i++)
            if (pth_atfork_list[i].child != NULL)
                pth_atfork_list[i].child(pth_atfork_list[i].arg);

	/* re-initialize the file locks for this process */
	pth_mutex_init(*(pth_mutex_t **)(stdin->_lock), NULL);		
	pth_mutex_init(*(pth_mutex_t **)(stdout->_lock), NULL);
	pth_mutex_init(*(pth_mutex_t **)(stderr->_lock), NULL);

	/* drop the watchdog and native threads (other than currently running) */ 
	pth_fork_drop_natives();

	sigemptyset(&current->sigactionmask);
	if (current->sigpendcnt > 0) { 
		current->sigpendcnt = 0;
		memset(&current->sigpending, 0x0, sizeof(current->sigpending));
	}

	/* initialize, create a new scheduler for this process */
	FREE_NATIVE(descr, 0);
	pth_initialized_minimal = FALSE;
	pth_initialize_minimal();
	pth_fork_init();

	memset(&current->mctx, 0x0, sizeof(struct pth_mctx_st));
	descr = &pth_first_native;

	/* Add this thread to global thread list queue */
	_pth_acquire_lock(&pth_native_lock, descr->tid);
        __thread_queue_add (current);
	pth_threads_count++;
	_pth_release_lock(&pth_native_lock, descr->tid);

	/* add current to pth_NQ list, so it can run from this scheduler */
	current->state = PTH_STATE_NEW;
	pqueue_append_node (&descr->new_queue, &current->node, descr);
	if (pth_max_native_threads > 1)
            current->boundnative = &pth_first_native;
	/* Run this thread under new scheduler for this process */
	pth_mctx_switch(&current->mctx, &descr->sched->mctx);
	pth_main = current; /* set pth_main for this process */
	pth_fork_initialized = FALSE;
    }
    return pid;
}
strong_alias(pth_fork, __pthread_fork)

intern void pth_fork_key_destroydata(pth_t t)
{
    /* Do not call the thread-specific destructor routine ... */

    pth_free_mem(t->data_value, sizeof(void *)*PTH_KEY_MAX);
    t->data_value = NULL;
    return;
}

intern void pth_fork_event_key_destroy(pth_t t)
{
     pth_free_mem(t->event_data, sizeof(void *)*PTH_EVENT_KEY_MAX);
     t->event_data = NULL;
     return;
}

intern void pth_fork_tcb_free(pth_t t)
{
    if (t == NULL)
	return;

    /* Cleanup the stack. */
    if (t->true_stack != NULL && !t->stackloan) 
        pth_free_mem (t->true_stack, t->stacksize + 8);

    /* free thread specific data */
    if (t->data_value != NULL)
        pth_fork_key_destroydata(t);

    if (t->event_data != NULL)
        pth_fork_event_key_destroy(t);
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

intern void pth_fork_drop_natives(void)
{
    int     slot = 0;
    int     this_slot = 0;
    pid_t   tid = k_gettid();
 
    /* Signal the watchdog to terminate... */
    pth_watchdog_enabled = FALSE;

    /* Finish watchdog termination... */
    if (pth_watchdog_descr.is_used) {
        pth_watchdog_descr.is_used = FALSE;
        pth_free_mem (pth_watchdog_descr.true_stack, pth_watchdog_descr.stacksize);
    }
 
    do {
    	if (pth_native_list[slot].is_used && (tid != pth_native_list[slot].tid)) {
	 
        	/* Clean up the scheduler */
		pth_fork_tcb_free(pth_native_list[slot].sched);
 
        	/* Now close the signal pipe... */
        	close(pth_native_list[slot].sigpipe[0]);
        	close(pth_native_list[slot].sigpipe[1]);
 
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
		if (slot != 0) {
                    pth_free_mem (pth_native_list[slot].true_stack, pth_native_list[slot].stacksize);
                }
                pth_native_list[slot].is_used = FALSE;
	} else
		this_slot = slot;  /* save the current slot */
    } while (pth_native_list[++slot].is_used == TRUE);
 
    /* Now close the signal pipe for current native... */
    close(pth_native_list[this_slot].sigpipe[0]);
    close(pth_native_list[this_slot].sigpipe[1]);
#if PTH_MCTX_MTH(sjlj)     &&\
    !PTH_MCTX_DSP(sjljlx)  &&\
    !PTH_MCTX_DSP(sjljisc) &&\
    !PTH_MCTX_DSP(sjljw32)
	pth_free_mem (pth_native_list[this_slot].mctx_trampoline, sizeof(jmp_buf));
#endif
}
 
/* No locks are held (no contention here) */
intern void pth_fork_drop_threads(pth_t current)
{
   pth_t t;
   int slot = 0;
   struct lnode_st *lnode;
 
   lnode = list_next (&pth_threads_queue);
   while (!list_node_is_tail (lnode, &pth_threads_queue)) {
	t = list_entry (lnode, struct pth_st, thread_queue);
	lnode = list_next (lnode);
	if (t != current)
	    pth_fork_tcb_free(t);
   }

   pqueue_init (&pth_NQ);
   pqueue_init (&pth_RQ);
   pqueue_init (&pth_WQ);
   pqueue_init (&pth_DQ);
   pqueue_init (&pth_SQ);
   while (pth_native_list[slot].is_used) {
	pth_descr_t ds = &pth_native_list[slot++];
	pqueue_init (&ds->wait_queue);
	pqueue_init (&ds->new_queue);
	pqueue_init (&ds->ready_queue);
   }
   
   return;
}

