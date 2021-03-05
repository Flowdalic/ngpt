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
**  pth_cancel.c: Pth thread cancellation
*/
                             /* ``Study it forever and you'll still wonder.
                                  Fly it once and you'll know.''
                                                       -- Henry Spencer */
#include "pth_p.h"
#include "queue.h"
#include "pqueue.h"
#include "schedule.h"
#ifdef THREAD_DB
#include "td_manager.h"
#endif

/* set cancellation state */
void pth_cancel_state(int newstate, int *oldstate)
{
    pth_t current = pth_get_current();
    if (oldstate != NULL)
        *oldstate = current->cancelstate;
    if (newstate != 0)
        current->cancelstate = newstate;
    return;
}

/* enter a cancellation point */
void pth_cancel_point(int set_yield)
{
    pth_descr_t descr = pth_get_native_descr();
    pth_t current = descr->current;

    if (   current->cancelreq == TRUE
        && current->cancelstate & PTH_CANCEL_ENABLE) {
        /* avoid looping if cleanup handlers contain cancellation points */
        current->cancelreq = FALSE;
        pth_debug2("pth_cancel_point: terminating cancelled thread \"%s\"", current->name);
        pth_exit(PTH_CANCELED, FALSE);
    }
    if (set_yield)
	descr->do_yield = 1;
    return;
}

/* cancel a thread (the friendly way) */
int pth_cancel(pth_t thread)
{
    struct pqueue_st *q;
    pth_descr_t descr = pth_get_native_descr();
    pth_t current = descr->current;
    pth_descr_t ds;
    char c = (int)1;

    if (thread == NULL || !thread_exists(thread))
        return FALSE;

    /* the thread has to be at least still alive */
    if (thread->state == PTH_STATE_DEAD || thread->cancelreq == TRUE)
	return TRUE;

    /* now mark the thread as cancelled */
    thread->cancelreq = TRUE;

    if ((ds = thread->boundnative) && ds->is_bounded) {
	if (ds->is_bound == FALSE)
	    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
    }

    /* when cancellation is enabled in async mode we cancel the thread immediately */
    if (   thread->cancelstate & PTH_CANCEL_ENABLE
        && thread->cancelstate & PTH_CANCEL_ASYNCHRONOUS) {

	if (thread == current) {
	    pth_cancel_point(FALSE);
	    return TRUE;
	}

	if ((ds = thread->boundnative) && (ds->is_bounded)) {
	    if (thread->state == PTH_STATE_NEW) {
		q = &ds->ready_queue;
		goto found;
	    }
	    return TRUE;
	}

        /* remove thread from its queue */
        switch (thread->state) {
            case PTH_STATE_NEW:    
		 q = (pth_number_of_natives > 1) ? &pth_NQ : &(descr->new_queue); break;
            case PTH_STATE_READY:   return TRUE;
            case PTH_STATE_WAITING: return TRUE;
            case PTH_STATE_MUTEX_WAITING: return TRUE;
            default:                q = NULL;
        }
        if (q == NULL)
            return FALSE;

found:
	spin_lock (&q->lock, descr, NULL);
	if (!__pqueue_contains (q, &thread->node)) {
            spin_unlock (&q->lock, descr);
            return FALSE;
        }
	__pqueue_delete_node (&thread->node);
        spin_unlock (&q->lock, descr);
    
        /* execute cleanups */
        pth_thread_cleanup(thread);

#ifdef THREAD_DB
	/* if thread debugging is enabled, alert the thread manager */
	if (unlikely (__pthread_doing_debug && __pthread_threads_debug))
	    __pthread_signal_thread_event(current, thread, TD_DEATH);
#endif

        /* and now either kick it out or move it to dead queue */
        if (!thread->joinable) {
            pth_debug2("pth_cancel: kicking out cancelled thread \"%s\" immediately", thread->name);
            pth_tcb_free(thread);
        }
        else {
            pth_debug2("pth_cancel: moving cancelled thread \"%s\" to dead queue", thread->name);
            thread->join_arg = PTH_CANCELED;
	    spin_lock (&pth_DQ.lock, descr, NULL);
            thread->state = PTH_STATE_DEAD;
            __pqueue_append_node (&pth_DQ, &thread->node);
	    if (thread->joined_native)
                pth_sc(write)(thread->joined_native->sigpipe[1], &c, sizeof(char));
	    spin_unlock (&pth_DQ.lock, descr);
        }
    }
    return TRUE;
}

/* abort a thread (the cruel way) */
int pth_abort(pth_t thread)
{
    if (thread == NULL)
        return FALSE;

    /* the current thread cannot be aborted */
    if (thread == pth_get_current())
        return FALSE;

    if (thread->state == PTH_STATE_DEAD && thread->joinable) {
        /* if thread is already terminated, just join it */
        if (!pth_join(thread, NULL))
            return FALSE;
    }
    else {
        /* else force it to be detached and cancel it asynchronously */
        thread->joinable = FALSE;
        thread->cancelstate = (PTH_CANCEL_ENABLE|PTH_CANCEL_ASYNCHRONOUS);
        if (!pth_cancel(thread))
            return FALSE;
    }
    return TRUE;
}

