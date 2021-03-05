
  /* -*-c-*- NGPT: O(1) scheduling helpers
  ** 
  ** $Id: schedule.h,v 1.2 2003/01/09 18:21:07 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** This module contains stuff for handling and assisting in the
  ** scheduling process, basically wrapping up stuff in pqueue.h.
  **
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** Some parts based on portions (C) The Linux Kernel Hackers
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **      - Implementation
  */

#ifndef __ngpt__schedule_h__
#define __ngpt__schedule_h__

#include "priorities.h"
#include "allocation.h"
#include "pqueue.h"

  /* Debug switches [0/1 disable/enable] */

#define SCHEDULE_CHECK_CONSISTENCY 1


  /* Return the thread where node is embedded */

static __inline__
pth_t thread_by_node (struct node_st *node)
{
  return node? list_entry (node, struct pth_st, node) : NULL;
}


  /* Return the thread's priority [unlocking/locking]
  **
  ** This is just the priority, per se, it is used by the scheduler as
  ** a reference on what to set the effective priority to - that one,
  ** the effective priority, is what actually mandates how it
  ** behaves.
  */

static __inline__
int __thread_prio_get (pth_t thread)
{
    return thread->priority;
}

static __inline__
int thread_prio_get (pth_t thread, pth_descr_t locker)
{
    int priority;
    _pth_acquire_lock (&thread->lock, locker->tid);
    priority = __thread_prio_get (thread);
    _pth_release_lock (&thread->lock, locker->tid);
    return priority;
}


  /* Set the thread's priority [unlocking/locking] */

static __inline__
void __thread_prio_set (pth_t thread, int new_priority)
{
    thread->priority = new_priority;
}

static __inline__
void thread_prio_set (pth_t thread, int new_priority, pth_descr_t locker)
{
    _pth_acquire_lock (&thread->lock, locker->tid);
    __thread_prio_set (thread, new_priority);
    _pth_release_lock (&thread->lock, locker->tid);
}


  /* Effective priority
  **
  ** The effective priority is the one that really counts on the
  ** scheduler - it marks where a task is in the execution list.
  */

  /* Return the thread's effective priority [unlocking/locking] */

static __inline__
int __thread_eprio_get (pth_t thread)
{
    return __pqueue_node_priority_get (&thread->node);
}

static __inline__
int thread_eprio_get (pth_t thread, pth_descr_t locker)
{
    int priority;
    _pth_acquire_lock (&thread->lock, locker->tid);
    priority = __thread_eprio_get (thread);
    _pth_release_lock (&thread->lock, locker->tid);
    return priority;
}


  /* Set the thread's effective priority [unlocking/locking]
  **
  ** WARNING! No locking is done on the runqueue where the thread is.
  **
  ** WARNING! No checks are done on the priority bounds! For checking
  ** if it is ok, use 'prio_validate (SCHED_INTERNAL, new_priority)'
  */

static __inline__
void __thread_eprio_set (pth_t thread, int new_priority)
{
    __pqueue_node_priority_set (&thread->node, new_priority);
}

static __inline__
void thread_eprio_set (pth_t thread, int new_priority, pth_descr_t locker)
{
    _pth_acquire_lock (&thread->lock, locker->tid);
    __thread_eprio_set (thread, new_priority);
    _pth_release_lock (&thread->lock, locker->tid);
}


  /* Recalculate a thread's effective priority
  **
  ** WARNING! Scheduling policies other than SCHED_OTHER don't have
  **          the effective priority adjusted.
  **
  ** WARNING! No locking is done on the runqueue where the thread is.
  **
  ** As always, there are two versions of this function; one that
  ** locks the thread and one that does not (prefixes it with __).
  **
  ** param thread Thread whose priority we want to modify.
  **
  ** param delta Amount by which to increase/decrease the priority.
  **
  ** return Errno error code if problems arise or 0 if ok.
  */

static __inline__
int __thread_eprio_recalculate (pth_t thread)
{
    if (thread->schedpolicy == SCHED_OTHER)
        __thread_eprio_set (thread, __thread_prio_get (thread));
    else {
/* #warning FIXME: __thread_eprio_recalculate() */
        __thread_eprio_set (thread, __thread_prio_get (thread));
    }
    
    return 0;
}

static __inline__
int thread_eprio_recalculate (pth_t thread, pth_descr_t locker)
{
    int result;
    _pth_acquire_lock (&thread->lock, locker->tid);
    result = __thread_eprio_recalculate (thread);
    _pth_release_lock (&thread->lock, locker->tid);
    return result;
}


  /* Add a thread to the thread queue [global list of all threads]
  **
  ** Update the magic pointer to point back to the head of the threads
  ** queue. This is used to accelerate thread_exists(), if it points
  ** ot the right place, it exists and is a valid thread.
  */

static __inline__
void __thread_queue_add (pth_t thread)
{
  list_insert(&thread->thread_queue, &pth_threads_queue);
  thread->magic = &pth_threads_queue;
}


  /* Remove a thread from the thread queue [global list of all threads]
  **
  ** NULLify the magic pointer. This is used to accelerate
  ** thread_exists(), if it points ot the right place, it exists and
  ** is a valid thread.
  */

static __inline__
void __thread_queue_remove (pth_t thread)
{
  list_del(&thread->thread_queue);
  thread->magic = NULL;
}


  /* Check whether a thread exists
  **
  ** Dirty hack and quick trick. This function is a CPU buster if it
  ** looks up in the pth_threads_queue list the given pointer [seriously
  ** limiting scalability of the library].
  **
  ** Thus, the following approach is taken. The pth_st contains a
  ** backpointer to the head of the pth_threads_queue. When given a
  ** pointer, first it is tested to check _if_ the pointer can be
  ** dereferenced; then if so, the magic is tested. If all OK, we
  ** consider the thread to exist and be valid. Otherwise, out of luck.
  **
  ** Yes, it can be fooled. However, I don't consider this a security
  ** risk - any program that fools with the internal state of the
  ** library deserves segfault or a security breach. Any program that
  ** loads untrusted binaries into it's adress space deserves segfault
  ** or a security breach. 
  */

static __inline__
int thread_exists(pth_t thread)
{
    if (thread == NULL)
        return 0;
    if (cal_verify_pointer (&gcal, thread, sizeof (struct pth_st)))
        return 0;
    return thread->magic == &pth_threads_queue;
}

#endif /* __ngpt_schedule_h__ */
