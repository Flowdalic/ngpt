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
**  pthread.c: POSIX Thread ("Pthread") API for Pth
*/
                             /* ``The nice thing about standards is that
                                  there are so many to choose from.  And if
                                  you really don't like all the standards you
                                  just have to wait another year until the one
                                  arises you are looking for'' 
                                                 -- Tannenbaum, 'Introduction
                                                    to Computer Networks' */

/*
**  HEADER STUFF
*/

/*
 * Include our own Pthread and then the private Pth header.
 * The order here is very important to get correct namespace hiding!
 */
#define _PTHREAD_PRIVATE
#include "pthread.h"
#include "pth_p.h"
#include "useldt.h"
#include "allocation.h"
#include <netdb.h>
#include <sched.h>
#include <sys/time.h>
#include "priorities.h"
#include "schedule.h"
#ifdef THREAD_DB
#undef _PTHREAD_PRIVATE
#include "td_manager.h"
#endif
#undef _PTHREAD_PRIVATE

/* general success return value */
#ifdef OK
#undef OK
#endif
#define OK 0


  /* Debug switches */

#define MUTEX_DEBUG 0

extern int       __pthread_attr_init_2_1 __P((pthread_attr_t *));
extern int       __pthread_create_2_1 __P((pthread_t *, const pthread_attr_t *, void *(*)(void *), void *));
extern void	_pthread_cleanup_push_defer __P((pth_cleanup_t *, void (*)(void *), void *));
extern void	_pthread_cleanup_pop_restore __P((pth_cleanup_t *, int));

#if !HP_TIMING_AVAIL
typedef unsigned long long int hp_timing_t;
#endif
extern int __pthread_clock_gettime (hp_timing_t f, struct timespec *tp);
extern void __pthread_clock_settime (hp_timing_t off);

/*
**  GLOBAL STUFF
*/

static void pthread_shutdown(void) __attribute__ ((unused));
static void pthread_shutdown(void)
{
    pth_kill();
    return;
}


static int pthread_initialized = FALSE;
static int pthread_concurrency = 0;
static int mutex_index = 0;
static struct pth_mutex_st init_mutex[100];
static pth_qlock_t atfork_lock = pth_qlock_st_INIT_UNLOCKED;
static pth_qlock_t once_init_lock = pth_qlock_st_INIT_UNLOCKED;

void __attribute__ ((constructor)) pthread_initialize(void);
void pthread_initialize(void)
{
    if (pthread_initialized == FALSE) { 
	pthread_initialized = TRUE; 
	if (pth_initialized_minimal != TRUE)
	    pth_initialize_minimal();
	pth_init(); 
    }
}

/*
**  THREAD ATTRIBUTE ROUTINES
*/

int __pthread_attr_init_2_1(pthread_attr_t *attr)
{
    pth_attr_t na;

    CHECK_INITIALIZED;
    if (unlikely (attr == NULL))
        return EINVAL;
    if (unlikely ((na = pth_attr_new()) == NULL))
        return ENOMEM;
    (*attr) = (pthread_attr_t)na;
    return OK;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    pth_attr_t na;

    if (attr == NULL || *attr == NULL)
        return EINVAL;
    na = (pth_attr_t)(*attr);
    pth_attr_destroy(na);
    *attr = NULL;
    return OK;
}

int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched)
{
    if (attr == NULL)
        return EINVAL;
    if (inheritsched != PTHREAD_EXPLICIT_SCHED)
	return ENOSYS;
    
    switch (inheritsched) {
      case PTHREAD_INHERIT_SCHED:
      case PTHREAD_EXPLICIT_SCHED:
        if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_INHERITSCHED, inheritsched))
            return EINVAL;
        break;
      default:
	return EINVAL;
    }
    return 0;
}

int pthread_attr_getinheritsched(__const pthread_attr_t *attr, int *inheritsched)
{
    int _inheritsched;
    if (attr == NULL || inheritsched == NULL)
        return EINVAL;

    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_INHERITSCHED, &_inheritsched))
        return EINVAL;
    *inheritsched = _inheritsched;
    return 0;
}

int pthread_attr_setschedparam(pthread_attr_t *attr,
                               __const struct sched_param *schedparam)
{
    int xprio = schedparam->sched_priority;
    int schedpolicy = 0;
    
    if (attr == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_SCHEDPOLICY, schedpolicy))
	return EINVAL;
    if (prio_validate (schedpolicy, xprio))
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_PRIO, xprio))
	return EINVAL;
    return OK;
}

int pthread_attr_getschedparam(__const pthread_attr_t *attr,
                               struct sched_param *schedparam)
{
    int xprio = 0;
    if (attr == NULL || schedparam == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_PRIO, &xprio))
        return EINVAL;
    schedparam->sched_priority = xprio;
    return OK;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int schedpolicy)
{
    if (attr == NULL)
        return EINVAL;

    switch (schedpolicy) {
      case SCHED_OTHER:
      case SCHED_FIFO:
      case SCHED_RR:
        break;
#ifdef SCHED_SPORADIC
      case SCHED_SPORADIC:
        return ENOTSUP;
#endif /* #ifdef SCHED_SPORADIC */
      default:
        return EINVAL; /* POSIX 1003.1-2001 p989 31448 */
    }

    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_SCHEDPOLICY, schedpolicy))
        return EINVAL;
    return 0;
}

int pthread_attr_getschedpolicy(__const pthread_attr_t *attr,
                                int *schedpolicy)
{
    int _schedpolicy;
    if (attr == NULL || schedpolicy == NULL)
        return EINVAL;

    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_SCHEDPOLICY, &_schedpolicy))
	return EINVAL;
    *schedpolicy = _schedpolicy;
    return 0;
}

int pthread_attr_setscope(pthread_attr_t *attr, int scope)
{
    if (attr == NULL)
        return EINVAL;
    if (scope < PTHREAD_SCOPE_SYSTEM || scope > PTHREAD_SCOPE_PROCESS)
        return EINVAL;
    if (scope == PTHREAD_SCOPE_SYSTEM) {
	if (pth_max_native_threads == 1) 
		return EINVAL;
    }
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_SCOPE, scope))
	return EINVAL;
    return OK;
}

int pthread_attr_getscope(__const pthread_attr_t *attr, int *scope)
{
    if (attr == NULL || scope == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_SCOPE, scope))
	return EINVAL;
    return OK;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    if (attr == NULL)
        return EINVAL;
    if (stacksize < PTHREAD_STACK_MIN || stacksize > 0x10000000)
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_STACK_SIZE, (unsigned int)stacksize))
        return EINVAL;
    return OK;
}

int pthread_attr_getstacksize(__const pthread_attr_t *attr, size_t *stacksize)
{
    if (attr == NULL || stacksize == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_STACK_SIZE, (unsigned int *)stacksize))
        return EINVAL;
    return OK;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr)
{
    if (attr == NULL)
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_STACK_ADDR, (char *)stackaddr))
        return EINVAL;
    return OK;
}

int pthread_attr_getstackaddr(__const pthread_attr_t *attr, void **stackaddr)
{
    if (attr == NULL || stackaddr == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_STACK_ADDR, (char **)stackaddr))
        return EINVAL;
    return OK;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    int s;

    if (attr == NULL)
        return EINVAL;
    if (detachstate == PTHREAD_CREATE_DETACHED)
        s = FALSE;
    else  if (detachstate == PTHREAD_CREATE_JOINABLE)
        s = TRUE;
    else
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_JOINABLE, s))
        return EINVAL;
    return OK;
}

int pthread_attr_getdetachstate(__const pthread_attr_t *attr, int *detachstate)
{
    int s;

    if (attr == NULL || detachstate == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_JOINABLE, &s))
        return EINVAL;
    if (s == TRUE)
        *detachstate = PTHREAD_CREATE_JOINABLE;
    else
        *detachstate = PTHREAD_CREATE_DETACHED;
    return OK;
}

int pthread_attr_setguardsize(pthread_attr_t *attr, size_t stacksize)
{
    return ENOSYS;
}
strong_alias(pthread_attr_setguardsize, pthread_attr_getguardsize)

int pthread_attr_setname_np(pthread_attr_t *attr, char *name)
{
    if (attr == NULL || name == NULL)
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_NAME, name))
        return EINVAL;
    return OK;
}

int pthread_attr_getname_np(__const pthread_attr_t *attr, char **name)
{
    if (attr == NULL || name == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_NAME, name))
        return EINVAL;
    return OK;
}

int pthread_attr_setprio_np(pthread_attr_t *attr, int xprio)
{
    int schedpolicy = 0;
    
    if (attr == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_SCHEDPOLICY, schedpolicy))
	return EINVAL;
    if (prio_validate (schedpolicy, xprio))
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_PRIO, xprio))
	return EINVAL;
    return OK;
}

int pthread_attr_getprio_np(__const pthread_attr_t *attr, int *prio)
{
    int xprio = 0;
    if (attr == NULL || prio == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_PRIO, &xprio))
        return EINVAL;
    *prio = xprio;
    return OK;
}

int pthread_attr_setweight_np(pthread_attr_t *att, int weight)
{
    /* Not relevant to this package at this time */
    return 0;
}

int pthread_attr_setsuspendstate_np(pthread_attr_t *attr, int suspendstate)
{
    if (attr == NULL)
        return EINVAL;
    if (!pth_attr_set((pth_attr_t)(*attr), PTH_ATTR_CREATE_SUSPENDED, TRUE))
        return EINVAL;
    return OK;
}

int pthread_attr_getsuspendstate_np(__const pthread_attr_t *attr, int *suspendstate)
{
    int s = 0;
    if (attr == NULL)
        return EINVAL;
    if (!pth_attr_get((pth_attr_t)(*attr), PTH_ATTR_CREATE_SUSPENDED, &s))
        return EINVAL;
    if (s == TRUE)
	*suspendstate = PTHREAD_CREATE_SUSPENDED;
    return OK;
}
/*end ibm*/

/*
**  THREAD ROUTINES
*/

int __pthread_create_2_1(
    pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *arg)
{
    pth_attr_t na;

    CHECK_INITIALIZED;
    if (thread == NULL || start_routine == NULL)
        return EINVAL;
    if (pth_threads_count >= PTHREAD_THREADS_MAX)
        return EAGAIN;
    if (attr != NULL)
        na = (pth_attr_t)(*attr);
    else
        na = PTH_ATTR_DEFAULT;

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug)) {
	struct pthread_request request;
	/* if not already, get thread manager up and initialized */
	if (__pthread_manager_request < 0)
	    if (__pthread_initialize_manager() < 0) 
		return EAGAIN;
	/* Build a manager request block */
	request.req_thread = PTH_SELF();
	request.req_kind = REQ_CREATE;
	request.req_args.create.attr = na;
	request.req_args.create.fn   = start_routine;
	request.req_args.create.arg  = arg;
	pth_sc(sigprocmask)(SIG_SETMASK, (const sigset_t *) NULL,
				&request.req_args.create.mask);
	/* Submit the request to the thread manager pipe and wait */
	td_debug("pthread_create_2_1: submit REQ_CREATE and suspend\n");
	td_manager_submit_request(&request, &((request.req_thread)->lock));
	/* get the return code from the manager operation */
	*thread = (pthread_t)((request.req_thread)->td_retval);
	td_debug2("pthread_create_2_1: resumed (REQ_CREATE) 0x%x\n", *thread);
	return (request.req_thread)->td_retcode;
    }
#endif
    *thread = (pthread_t)PTH_SPAWN(na, start_routine, arg);
    if (*thread == NULL)
        return EAGAIN;
    return OK;
}

int pthread_detach(pthread_t thread)
{
    pth_attr_t na;
    int s;

    CHECK_INITIALIZED;
    if (thread == NULL)
        return EINVAL;
    if (!thread_exists((pth_t)thread))
	return ESRCH;
    if ((na = pth_attr_of((pth_t)thread)) == NULL)
        return EINVAL;
    if (!pth_attr_get(na, PTH_ATTR_JOINABLE, &s))
	return EINVAL;
    if (s == FALSE)
        return EINVAL;
    if (!pth_attr_set(na, PTH_ATTR_JOINABLE, FALSE))
        return EINVAL;
    pth_attr_destroy(na);
    return OK;
}

pthread_t pthread_self(void)
{
#ifdef NATIVE_SELF
    return (pthread_t)NATIVE_SELF->current;
#else
    return (pthread_t)PTH_SELF();
#endif
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return (t1 == t2);
}

int pthread_yield_np(void)
{
    pth_yield(NULL);
    return OK;
}
strong_alias(pthread_yield_np, pthread_yield)

int pthread_join(pthread_t thread, void **value_ptr)
{
    int rc;

    CHECK_INITIALIZED;
    if ((rc = pth_join((pth_t)thread, value_ptr)))
        return rc;
    if (value_ptr != NULL)
        if (*value_ptr == PTH_CANCELED)
            *value_ptr = PTHREAD_CANCELED;
    return OK;
}

/* If a thread is canceled while calling the init_routine out of
   pthread_once, this handler will reset the once_control variable */
static void pthread_once_handler(void *arg)
{
    pthread_once_t *once_control = arg;
 
    pth_acquire_lock(&once_init_lock);
    *once_control = PTHREAD_ONCE_INIT;
    pth_release_lock(&once_init_lock);
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    int	do_once_init;

    CHECK_INITIALIZED;
    if (once_control == NULL || init_routine == NULL)
        return EINVAL;
    pth_acquire_lock(&once_init_lock);
    do_once_init = (*once_control != 1);
    *once_control = 1;
    pth_release_lock(&once_init_lock);
    if (do_once_init) {
	pthread_cleanup_push(pthread_once_handler, once_control); 
        init_routine();
	pthread_cleanup_pop(0);
    }
    return OK;
}

/*begin ibm*/
int pthread_bindtonative_np(pthread_t thread)
{
    if (!pth_bindtonative((pth_t)thread))
	return EINVAL;
    return OK;
}

/*end ibm*/

/*
**  CONCURRENCY ROUTINES
*/

int pthread_getconcurrency(void)
{
    return pthread_concurrency;
}

int pthread_setconcurrency(int new_level)
{
    pthread_concurrency = new_level;
    return OK;
}

/*
**  CONTEXT ROUTINES
*/

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    CHECK_INITIALIZED;
    if (!pth_key_create((pth_key_t *)key, destructor, NULL))
        return EAGAIN;
    return OK;
}

int pthread_key_delete(pthread_key_t key)
{
    if (!pth_key_delete((pth_key_t)key))
        return EINVAL;
    return OK;
}

/*
**  CANCEL ROUTINES
*/

int pthread_cancel(pthread_t thread)
{
    CHECK_INITIALIZED;
    if (!pth_cancel((pth_t)thread))
        return ESRCH;
    return OK;
}

void pthread_testcancel(void)
{
    CHECK_INITIALIZED;
    pth_cancel_point(TRUE);
    pth_yield(NULL);
    return;
}

int pthread_setcancelstate(int state, int *oldstate)
{
    int s, os;

    CHECK_INITIALIZED;
    if (state < PTHREAD_CANCEL_ENABLE || state > PTHREAD_CANCEL_DISABLE)
    	return EINVAL;

    if (oldstate != NULL) {
        pth_cancel_state(0, &os);
        if (os & PTH_CANCEL_ENABLE)
            *oldstate = PTHREAD_CANCEL_ENABLE;
        else
            *oldstate = PTHREAD_CANCEL_DISABLE;
    }
    pth_cancel_state(0, &s);
    if (state == PTHREAD_CANCEL_ENABLE) {
	s |= PTH_CANCEL_ENABLE;
	s &= ~(PTH_CANCEL_DISABLE);
    } else {
	s |= PTH_CANCEL_DISABLE;
	s &= ~(PTH_CANCEL_ENABLE);
    }
    pth_cancel_state(s, NULL);

    if ((s & PTH_CANCEL_ENABLE) && (s & PTH_CANCEL_ASYNCHRONOUS)) {
    	pth_cancel_point(TRUE);
	pth_yield(NULL);
    }
    return OK;
}

int pthread_setcanceltype(int type, int *oldtype)
{
    int t, ot;

    CHECK_INITIALIZED;
    if (type < PTHREAD_CANCEL_DEFERRED || type > PTHREAD_CANCEL_ASYNCHRONOUS)
    	return EINVAL;

    if (oldtype != NULL) {
        pth_cancel_state(0, &ot);
        if (ot & PTH_CANCEL_DEFERRED)
            *oldtype = PTHREAD_CANCEL_DEFERRED;
        else
            *oldtype = PTHREAD_CANCEL_ASYNCHRONOUS;
    }
    pth_cancel_state(0, &t);
    if (type == PTHREAD_CANCEL_DEFERRED) {
	t |= PTH_CANCEL_DEFERRED;
	t &= ~(PTH_CANCEL_ASYNCHRONOUS);
    } else {
	t |= PTH_CANCEL_ASYNCHRONOUS;
	t &= ~(PTH_CANCEL_DEFERRED);
    }
    pth_cancel_state(t, NULL);

    if ((t & PTH_CANCEL_ENABLE) && (t & PTH_CANCEL_ASYNCHRONOUS)) {
    	pth_cancel_point(TRUE);
	pth_yield(NULL);
    }
    return OK;
}

/*
**  SCHEDULER ROUTINES
*/

int pthread_setschedparam(pthread_t pthread, int policy, const struct sched_param *param)
{
    /* 
     * For now, we only support SCHED_OTHER... 
     *	    And therefore only allow that to be set.
     */    
    if (policy != SCHED_OTHER)
	return ENOSYS;

    return 0;
}

int pthread_getschedparam(pthread_t pthread, int *policy, struct sched_param *param)
{
    /*
     * For, we only support SCHED_OTHER...
     *	    And therefore only return that.
     */
    if (policy != NULL)
	*policy = SCHED_OTHER;
    else
	return EINVAL;
    return 0;
}

/*
**  AT-FORK SUPPORT
*/

struct pthread_atfork_st {
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
};
static struct pthread_atfork_st pthread_atfork_info[PTH_ATFORK_MAX];
static int pthread_atfork_idx = 0;

static void pthread_atfork_cb_prepare(void *_info)
{
    struct pthread_atfork_st *info = (struct pthread_atfork_st *)_info;
    if (info->prepare != NULL)
	info->prepare();
    return;
}
static void pthread_atfork_cb_parent(void *_info)
{
    struct pthread_atfork_st *info = (struct pthread_atfork_st *)_info;
    if (info->parent != NULL)
        info->parent();
    return;
}
static void pthread_atfork_cb_child(void *_info)
{
    struct pthread_atfork_st *info = (struct pthread_atfork_st *)_info;
    if (info->child != NULL)
        info->child();
    return;
}

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    struct pthread_atfork_st *info;
    int rc = 0;

    if (pthread_atfork_idx > PTH_ATFORK_MAX-1)
        return ENOMEM;
    pth_acquire_lock(&atfork_lock);
    info = &pthread_atfork_info[pthread_atfork_idx++];
    info->prepare = prepare;
    info->parent  = parent;
    info->child   = child;
    rc = pth_atfork_push(pthread_atfork_cb_prepare,
                         pthread_atfork_cb_parent,
                         pthread_atfork_cb_child, info);
    pth_release_lock(&atfork_lock);
    if (rc != TRUE)
        return ENOMEM;
    return OK;
}

/*
**  MUTEX ATTRIBUTE ROUTINES
*/

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    pth_mutexattr_t *a;
    CHECK_INITIALIZED;
    if (attr == NULL)
        return EINVAL;
    if ((a = (pth_mutexattr_t *)pth_malloc(sizeof(struct pth_mutexattr_st))) == NULL)
	return ENOMEM;
    a->type = PTHREAD_MUTEX_NORMAL;
    a->priority = 0;
    a->protocol = 0;
    a->pshared = PTHREAD_PROCESS_PRIVATE;
    a->robustness = PTHREAD_MUTEX_STALL_NP;
    (*attr) = (pthread_mutexattr_t)a;
    return OK;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    pth_free_mem (a, sizeof (struct pth_mutexattr_st));
    (attr) = NULL;
    return OK;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    a->priority = prioceiling;
    return OK;
}

int pthread_mutexattr_getprioceiling(pthread_mutexattr_t *attr, int *prioceiling)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL || prioceiling == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    *prioceiling = a->priority;
    return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    a->protocol = protocol;
    return OK;
}

int pthread_mutexattr_getprotocol(pthread_mutexattr_t *attr, int *protocol)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL || protocol == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    *protocol = a->protocol;
    return OK;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    a->pshared = pshared;
    return OK;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr, int *pshared)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL || pshared == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    *pshared = a->pshared;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    a->type = type;
    return OK;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *attr, int *type)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL || type == NULL)
        return EINVAL;
    a = (pth_mutexattr_t *)(*attr);
    *type = a->type;
    return 0;
}

/*
** Robust Mutex Support
*/
 
int pthread_mutexattr_setrobust_np(pthread_mutexattr_t *attr, int robustness)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL)
	return EINVAL;
 
    if ((robustness != PTHREAD_MUTEX_STALL_NP) && (robustness != PTHREAD_MUTEX_ROBUST_NP))
	return EINVAL;
 
    a = (pth_mutexattr_t *)(*attr);
    a->robustness = robustness;
    return OK;
}
 
int pthread_mutexattr_getrobust_np(const pthread_mutexattr_t *attr, int *robustness)
{
    pth_mutexattr_t *a;
    if (attr == NULL || *attr == NULL || robustness == NULL)
	return EINVAL;
 
    a = (pth_mutexattr_t *)(*attr);
    *robustness = a->robustness;
    return 0;
}
 
int pthread_mutex_consistent_np(pthread_mutex_t *mutex)
{
    pth_mutex_t *m;

    if (mutex == NULL || (m = mutex->mx_mutex) == NULL)
	return EINVAL;

    /* if mutex is not consistent, make it consistent and return */
    if (m->mx_state & PTH_MUTEX_NOT_CONSISTENT) {
	m->mx_state &= ~PTH_MUTEX_NOT_CONSISTENT;
	return OK;
    }
    return EINVAL;
}

/*
**  MUTEX ROUTINES
**	NOTE:	We need to use the thread safe libc malloc routine.  Therefore,
**		it is necessary for us to export these entries so that the stock
**		libc will think that we are the stock libpthread and use the
**		thread safe malloc.  However, during initialization, we're single
**		threaded and can use the normal malloc.  So, we 'fake' the mutex
**		operations until we get fully initialized after which we let
**		the normal mutex operations do their stuff.  We also do this
**		during shutdown.
*/

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    pth_mutexattr_t *pattr = (attr != NULL) ? (pth_mutexattr_t *)(*attr) : NULL;
    pth_mutex_t *m;

    __fdebugmsg (MUTEX_DEBUG, "%s (%p): starting [mx_mutex %p]\n",
                 __FUNCTION__, mutex, mutex->mx_mutex);
    if (unlikely (mutex == NULL))
        return EINVAL;
    if (unlikely (pth_initialized_minimal != TRUE))
	pth_initialize_minimal();
    /* We use &mutex as lock owner, we don't want zeroes here :) */
    _pth_acquire_lock(&pth_init_lock, (pid_t) &mutex);
    if (pth_initializing != TRUE && pth_shutdown_inprogress != TRUE) {
	if (pth_init_lock.count > 1) { /* recursive call? */
	    if (mutex_index < 100) 
		m = &init_mutex[mutex_index++];
	    else
		abort();
	} else {
	    if (pattr && pattr->pshared == PTHREAD_PROCESS_SHARED) {
		m = NULL;
		if (mutex->mx_init == TRUE && mutex->mx_mutex != NULL) {
		    /* mutex was initialized and allocated */
		    if (pth_find_shared_mutex(mutex->mx_mutex) == TRUE) {
			/* Existing shared area lock object found */
			m = mutex->mx_mutex;
			if (pattr->robustness == PTHREAD_MUTEX_ROBUST_NP) {
			    if (m->mx_state & PTH_MUTEX_NOT_CONSISTENT) {
			        /* if not consistent, make it consistent */
				m->mx_state &= ~PTH_MUTEX_NOT_CONSISTENT;
			    }
			    /* return with lock object as is */
			    _pth_release_lock(&pth_init_lock, (pid_t) &mutex);
			    return OK;
			}
			/* Fall through to initialize found mutex */
		    }
		}
		if (m == NULL)
		    /* no matching shared area lock found, allocate new */
		    if ((m = (pth_mutex_t *)pth_alloc_shared_mutex()) == NULL) {
			_pth_release_lock(&pth_init_lock, (pid_t) &mutex);
			return ENOMEM;
		    }
	    } else {
		if ((m = (pth_mutex_t *)pth_malloc(sizeof(pth_mutex_t))) == NULL) {
		    _pth_release_lock(&pth_init_lock, (pid_t) &mutex);
		    return ENOMEM;
		}
	    }
	}
    } else {
	if (mutex_index < 100) {
	    m = &init_mutex[mutex_index++];
	    _pth_release_lock(&pth_init_lock, (pid_t) &mutex);
	    if (!pth_mutex_init(m, pattr)) 
		return EINVAL;
	    m->mx_state |= PTH_MUTEX_INTERNAL_LOCKED;
	    mutex->mx_init = TRUE;
	    mutex->mx_mutex = m;
	    return OK;
	} else
	    abort();
    }
    _pth_release_lock(&pth_init_lock, (pid_t) &mutex);
    if (unlikely (!pth_mutex_init(m, pattr))) 
	return EINVAL;
    mutex->mx_init = TRUE;
    mutex->mx_mutex = m;

    return OK;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    pth_mutex_t *mx_mutex;
    if (unlikely (mutex == NULL || mutex->mx_init == 0))
        return EINVAL;
    if (likely((mx_mutex = (pth_mutex_t *)mutex->mx_mutex) != NULL)) {
	pth_acquire_lock(&mx_mutex->mx_lock); 
	if (mx_mutex->mx_state & PTH_MUTEX_LOCKED) {
	    if (mx_mutex->mx_owner == pth_get_current()) {
		pth_release_lock(&mx_mutex->mx_lock); 
		if (mx_mutex->mx_state & PTH_MUTEX_NOT_CONSISTENT)
		    return EBUSY;
		pthread_mutex_unlock(mutex);
	    } else {
		pth_release_lock(&mx_mutex->mx_lock); 
 		return EBUSY;
	    }
	} else
	    pth_release_lock(&mx_mutex->mx_lock);
	if (mx_mutex >= &init_mutex[0] && mx_mutex <= &init_mutex[mutex_index])
 	    return OK;
	if ((mx_mutex)->mx_shared.pshared == TRUE)
	    pth_mutex_destroy(mx_mutex);
	else
	    pth_free_mem(mutex->mx_mutex, sizeof(struct pth_mutex_st));
	mutex->mx_mutex = NULL;
     }
     mutex->mx_init = FALSE;
     return OK;
}

int pthread_mutex_setprioceiling(pthread_mutex_t *mutex, int prioceiling, int *old_ceiling)
{
    if (mutex == NULL)
        return EINVAL;
    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
	switch (mutex->mx_kind) {
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
	    if (pthread_mutex_init(mutex,  NULL) != OK)
		return EINVAL;
	    if (mutex->mx_kind == PTHREAD_MUTEX_RECURSIVE)
		((pth_mutex_t *)mutex->mx_mutex)->mx_type = PTHREAD_MUTEX_RECURSIVE;
	    break;
	default:
	    return EINVAL;
	}
    }
	    
    /* not supported */
    return ENOSYS;
}

int pthread_mutex_getprioceiling(pthread_mutex_t *mutex, int *prioceiling)
{
    if (mutex == NULL)
        return EINVAL;
    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
	switch (mutex->mx_kind) {
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
	    if (pthread_mutex_init(mutex,  NULL) != OK)
		return EINVAL;
	    if (mutex->mx_kind == PTHREAD_MUTEX_RECURSIVE)
		((pth_mutex_t *)mutex->mx_mutex)->mx_type = PTHREAD_MUTEX_RECURSIVE;
	    break;
	default:
	    return EINVAL;
	}
    }
	    
    /* not supported */
    return ENOSYS;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    int rc;

    __fdebugmsg (MUTEX_DEBUG, "%s (%p): starting [mx_mutex %p]\n",
                 __FUNCTION__, mutex, mutex->mx_mutex);
    if (unlikely (mutex == NULL))
        return EINVAL;
    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
	switch (mutex->mx_kind) {
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
	    {
	    /* lock and re-check to prevent double mutex allocation */
	    while (test_and_set(&mutex->mx_init_lock))
		;;
	    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
		if (unlikely (pthread_mutex_init(mutex,  NULL) != OK)) {
		    mutex->mx_init_lock = 0;
		    return EINVAL;
		}
		if (mutex->mx_kind == PTHREAD_MUTEX_RECURSIVE)
		    ((pth_mutex_t *)mutex->mx_mutex)->mx_type = PTHREAD_MUTEX_RECURSIVE;
	    }
	    mutex->mx_init_lock = 0;
	    break;
	    }
	default:
	    return EINVAL;
	}
    }
	    
    rc = pth_mutex_acquire((pth_mutex_t *)(mutex->mx_mutex), FALSE, NULL);
    return rc;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    int rc;

    __fdebugmsg (MUTEX_DEBUG, "%s (%p): starting [mx_mutex %p]\n",
                 __FUNCTION__, mutex, mutex->mx_mutex);
    if (unlikely (mutex == NULL))
        return EINVAL;
    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
	switch (mutex->mx_kind) {
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_RECURSIVE:
	    {
	    /* lock and re-check to prevent double mutex allocation */
	    while (test_and_set(&mutex->mx_init_lock))
		;;
	    if (mutex->mx_mutex == NULL || mutex->mx_init == 0) {
		if (pthread_mutex_init(mutex,  NULL) != OK) {
		    mutex->mx_init_lock = 0;
		    return EINVAL;
		}
		if (mutex->mx_kind == PTHREAD_MUTEX_RECURSIVE)
		    ((pth_mutex_t *)mutex->mx_mutex)->mx_type = PTHREAD_MUTEX_RECURSIVE;
	    }
	    mutex->mx_init_lock = 0;
	    break;
	    }
	default:
	    return EINVAL;
	}
    }
	    
    if (((pth_mutex_t *)mutex->mx_mutex)->mx_state & PTH_MUTEX_INTERNAL_LOCKED) {
	pid_t tid = k_gettid();
	if ( ((pth_mutex_t *)mutex->mx_mutex)->mx_lock.count > 0 &&
	     ((pth_mutex_t *)mutex->mx_mutex)->mx_lock.owner != tid)
		return EBUSY;
	_pth_acquire_lock(&(((pth_mutex_t *)mutex->mx_mutex)->mx_lock), tid);
	return OK;
    }
    rc = pth_mutex_acquire((pth_mutex_t *)(mutex->mx_mutex), TRUE, NULL);
    return rc;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    int rc;

    __fdebugmsg (MUTEX_DEBUG, "%s (%p): starting [mx_mutex %p]\n",
                 __FUNCTION__, mutex, mutex->mx_mutex);
    if (mutex == NULL || mutex->mx_mutex == NULL || mutex->mx_init == 0)
	return EINVAL;
	    
    rc = pth_mutex_release((pth_mutex_t *)(mutex->mx_mutex));
    return rc;
}

/*
**  LOCK ATTRIBUTE ROUTINES
*/

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    attr->pshared = PTHREAD_PROCESS_PRIVATE;
    return OK;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    /* nothing to do for us */
    return OK;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared)
{
    if (attr == NULL)
        return EINVAL;
    attr->pshared = pshared;
    return OK;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attr, int *pshared)
{
    if (attr == NULL)
        return EINVAL;
    *pshared = attr->pshared;
    return OK;
}

/*
**  LOCK ROUTINES
*/

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    int pshared = (attr != NULL) ? attr->pshared : 0;
    int rc;

    if (rwlock == NULL)
	return EINVAL;
    if ((rc = pth_rwlock_init((pth_rwlock_t *)rwlock, pshared)))
	return rc;
    rwlock->rw_init = TRUE;
    return OK;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    int rc;
    if (rwlock == NULL || rwlock->rw_init == 0)
	return EINVAL;
    if ((rc = pth_rwlock_destroy((pth_rwlock_t *)rwlock)))
	return rc;

    rwlock->rw_mutex_rd = NULL;
    rwlock->rw_mutex_rw = NULL;
    rwlock->rw_init = FALSE;
    return OK;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int rc;

    if (rwlock == NULL || rwlock->rw_init == 0)
	return EINVAL;
    /* if it was statically initialized - it can't be shared */  
    if (rwlock->rw_mutex_rd == NULL && rwlock->rw_mutex_rw == NULL)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return EINVAL;
    rc = pth_rwlock_acquire((pth_rwlock_t *)rwlock, PTH_RWLOCK_RD, FALSE, NULL);
    return rc;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    int rc;

    if (rwlock == NULL || rwlock->rw_init == 0)
        return EINVAL;
    if (rwlock->rw_mutex_rd == NULL && rwlock->rw_mutex_rw == NULL)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return EINVAL;
    rc = pth_rwlock_acquire((pth_rwlock_t *)rwlock, PTH_RWLOCK_RD, TRUE, NULL);
    return rc;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    int rc;

    if (rwlock == NULL || rwlock->rw_init == 0)
        return EINVAL;
    if (rwlock->rw_mutex_rd == NULL && rwlock->rw_mutex_rw == NULL)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return EINVAL;
    rc = pth_rwlock_acquire((pth_rwlock_t *)rwlock, PTH_RWLOCK_RW, FALSE, NULL);
    return rc;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    int rc;

    if (rwlock == NULL || rwlock->rw_init == 0)
        return EINVAL;
    if (rwlock->rw_mutex_rd == NULL && rwlock->rw_mutex_rw == NULL)
        if (pthread_rwlock_init(rwlock, NULL) != OK)
            return EINVAL;
    rc = pth_rwlock_acquire((pth_rwlock_t *)rwlock, PTH_RWLOCK_RW, TRUE, NULL);
    return rc;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    int rc;

    if (rwlock == NULL || rwlock->rw_init == 0 ||
        rwlock->rw_mutex_rd == NULL || rwlock->rw_mutex_rw == NULL)
        return EINVAL;
    rc = pth_rwlock_release((pth_rwlock_t *)rwlock);
    return rc;
}

/*
**  CONDITION ATTRIBUTE ROUTINES
*/

int pthread_condattr_init(pthread_condattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    /* nothing to do for us */
    return OK;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    if (attr == NULL)
        return EINVAL;
    /* nothing to do for us */
    return OK;
}

int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{
    if (attr == NULL)
        return EINVAL;
    *attr = (int)pshared;
    return OK;
}

int pthread_condattr_getpshared(const pthread_condattr_t *attr, int *pshared)
{
    if (attr == NULL)
        return EINVAL;
    *pshared = (int) *attr;
    return OK;
}

/*
**  CONDITION ROUTINES
*/

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    pth_cond_t *cn = (cond != NULL)? (pth_cond_t *)cond->cn_cond : NULL;
    int pshared = (attr != NULL) ? (int) *attr : 0;
    int rc = 0;

    if (unlikely (cond == NULL))
        return EINVAL;
    if (pshared == PTHREAD_PROCESS_SHARED) {
	cn = NULL;
	if (cond->cn_init == TRUE && cond->cn_cond != NULL)
	    if (pth_find_shared_mutex((pth_mutex_t *)cond->cn_cond) == TRUE) {
		/* cond was initialized and allocated */
		cn = cond->cn_cond;
		/* TODO: any cases where we'd want to skip reinitialization ? */
	    }
	if (cn == NULL) 
	    if (unlikely ((cn = (pth_cond_t *)pth_alloc_shared_cond()) == NULL))
		return ENOMEM;
	/* initialize the shared condition variable */
	rc = pth_cond_init_shared(cn);
    } else {
	if (unlikely ((cn = (pth_cond_t *)pth_malloc(sizeof(pth_cond_t))) == NULL))
	     return ENOMEM;
	rc = pth_cond_init(cn);
    }
    if (unlikely (!rc))
        return EINVAL;
    cond->cn_cond = cn;
    cond->cn_init = TRUE;
    return OK;
}
int pthread_cond_destroy(pthread_cond_t *cond)
{
    if (unlikely (cond == NULL))
        return EINVAL;
    if (likely (cond->cn_init == TRUE && cond->cn_cond != NULL)) {
	pth_acquire_lock(&((pth_cond_t *)cond->cn_cond)->cn_lock); 
	if (((pth_cond_t *)cond->cn_cond)->cn_waiters > 0) {
	    pth_release_lock(&((pth_cond_t *)cond->cn_cond)->cn_lock); 
	    pth_yield(NULL);
	    return EBUSY;
	}
	pth_release_lock(&((pth_cond_t *)cond->cn_cond)->cn_lock); 
	if (((pth_cond_t *)cond->cn_cond)->cn_shared.pshared == TRUE)
	    pth_cond_destroy(((pth_cond_t *)cond->cn_cond));
	else
	    pth_free_mem(cond->cn_cond, sizeof(struct pth_cond_st));
    }
    cond->cn_cond = NULL;
    cond->cn_init = FALSE;
    return OK;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (unlikely (cond == NULL))
        return EINVAL;
    if (cond->cn_cond == NULL || cond->cn_init == 0)
        if (unlikely (pthread_cond_init(cond, NULL) != OK))
            return EINVAL;
    if (unlikely (!pth_cond_notify((pth_cond_t *)(cond->cn_cond), TRUE)))
        return EINVAL;
    return OK;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    if (unlikely (cond == NULL))
        return EINVAL;
    if (cond->cn_cond == NULL || cond->cn_init == 0)
        if (unlikely (pthread_cond_init(cond, NULL) != OK))
            return EINVAL;
    if (unlikely (!pth_cond_notify((pth_cond_t *)(cond->cn_cond), FALSE)))
        return EINVAL;
    return OK;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL || mutex->mx_mutex == NULL)
        return EINVAL;
    if (cond->cn_cond == NULL || cond->cn_init == 0) {
        if (unlikely (pthread_cond_init(cond, NULL) != OK))
            return EINVAL;
    }
	    
    if (unlikely (!pth_cond_await((pth_cond_t *)(cond->cn_cond),
				  (pth_mutex_t *)(mutex->mx_mutex), NULL)))
        return EINVAL;
    return OK;
}

#ifndef HAVE_STRUCT_TIMESPEC
struct timespec {
    time_t  tv_sec;     /* seconds */
    long    tv_nsec;    /* and nanoseconds */
};
#endif

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_time_t  timeout;

    if (unlikely (cond == NULL || mutex == NULL || abstime == NULL || mutex->mx_mutex == NULL))
        return EINVAL;
#ifdef __amigaos__
    if (abstime->ts_sec < 0 || abstime->ts_nsec < 0 || abstime->ts_nsec >= 1000000000)
#else
    if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000)
#endif
        return EINVAL;
    if (cond->cn_cond == NULL || cond->cn_init == 0) {
        if (pthread_cond_init(cond, NULL) != OK)
            return EINVAL;
    }

#ifdef __amigaos__
    timeout = pth_TOD_time(abstime->ts_sec, abstime->ts_nsec/1000);
#else
    timeout = pth_TOD_time(abstime->tv_sec, abstime->tv_nsec/1000);
#endif
    ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key, timeout);
    if (!pth_cond_await((pth_cond_t *)(cond->cn_cond), (pth_mutex_t *)(mutex->mx_mutex), ev)) {
        __fdebugmsg (0, "await failed mutex %p\n", mutex);
        __fdebugmsg (0, "%s: mutex %p, pthmutex %p\n", __FUNCTION__, mutex, mutex->mx_mutex);
        return EINVAL;
    }
    if (PTH_EVENT_OCCURRED(ev))
        return ETIMEDOUT;
    return OK;
}

/*
**  POSIX 1003.1j
*/

int pthread_abort(pthread_t thread)
{
    if (!pth_abort((pth_t)thread))
        return EINVAL;
    return OK;
}

/*
**  THREAD-SAFE REPLACEMENT FUNCTIONS
*/

void __flockfile(FILE *stream)
{
    if (pth_shutdown_inprogress == TRUE)
	return;
    pthread_mutex_lock(stream->_lock);
}

void __funlockfile(FILE *stream)
{
    if (pth_shutdown_inprogress == TRUE)
	return;
    pthread_mutex_unlock(stream->_lock);
}

int __ftrylockfile(FILE *stream)
{
    if (pth_shutdown_inprogress == TRUE)
	return 0;
    return pthread_mutex_trylock(stream->_lock);
}

/*
 * Linuxthreads compatibility cleanup handlers
 */
void _pthread_cleanup_push(struct _pthread_cleanup_buffer *cleanup_buffer,
				void (*func)(void *), void *arg)
{
    pth_t current;

    CHECK_INITIALIZED;
    current = pth_get_current();

    if (current != NULL) {
#if 0
	pth_cleanup_t *cleanup = (pth_cleanup_t *)cleanup_buffer;
	cleanup->func = func;
	cleanup->arg  = arg;
	cleanup->next = current->cleanups;
	current->cleanups = cleanup;
#else
	pth_cleanup_push(func, arg);
#endif
    }
}

void _pthread_cleanup_push_defer(pth_cleanup_t *cleanup,
			        void (*func)(void *), void *arg)
{
    pth_t current;

    CHECK_INITIALIZED;
    current = pth_get_current();

    if (current != NULL) {
#if 0
        cleanup->func = func;
        cleanup->arg  = arg;
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &(cleanup->canceltype));
        cleanup->next = current->cleanups;
        current->cleanups = cleanup;
#else
	pth_cleanup_push(func, arg);
	if (current->cleanups)
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, 
			&(current->cleanups->canceltype));
#endif
    }
}

void _pthread_cleanup_pop(struct _pthread_cleanup_buffer *cleanup_buffer, int execute)
{
    pth_t current;

    CHECK_INITIALIZED;
    current = pth_get_current();

    if (current != NULL) {
#if 0
	pth_cleanup_t *cleanup = (pth_cleanup_t *)cleanup_buffer;
        if (execute)
            cleanup->func(cleanup->arg);
        current->cleanups = cleanup->next;
#else
	pth_cleanup_pop(execute);
#endif
    }
}

void _pthread_cleanup_pop_restore(pth_cleanup_t *cleanup, int execute)
{
    pth_t current;
    int   restore_canceltype;

    CHECK_INITIALIZED;
    current = pth_get_current();

    if (current != NULL) {
#if 0
        if (execute)
            cleanup->func(cleanup->arg);
        current->cleanups = cleanup->next;
	restore_canceltype = cleanup->canceltype;
#else
	if (current->cleanups == NULL)
	    return;
	restore_canceltype = (current->cleanups)->canceltype;
	pth_cleanup_pop(execute);
#endif
	pthread_setcanceltype(restore_canceltype, NULL);
    }
}

extern int __raise(int sig);
int __raise(int sig)
{
    int retcode = pthread_kill(pthread_self(), sig);
    if (retcode == 0)
	return 0;
    else
	return_errno(-1, retcode);
}

/* glibc settime/gettime internals. 
 *
 * We might want to base this off of the chip/other system means
 * of getting a time stamp in the future like the glibc HP stuff
 * does, but for now this satisfies the APIs and functionality.
 */

/* Note that arg f is ignored, we use our own thread start time */
int __pthread_clock_gettime (hp_timing_t f, struct timespec *tp)
{
    pth_t current = pth_get_current();
    pth_time_t now;

    /* get the difference between now and when we were spawned */
    pth_time_set_now(&now);
    pth_time_sub(&now, &(current->spawned));

    /* pass back the delta time info */
    *tp = pth_time_to_timespec(&now);
    return 0;
}

void __pthread_clock_settime (hp_timing_t off)
{
    /* we set this already in thread->spawned, do nothing */
}

void pthread_kill_other_threads_np (void)
{
    /* 
     * this api does nothing, silently...  
     * For link time compatibility only!!!
     */
}

#ifdef errno
#undef errno
extern int errno;
#endif
int *__errno_location(void)
{
    int *result = &errno;
    pth_descr_t descr = pth_get_native_descr();

    if (descr != NULL)
	result = &(descr->native_errno);
    return result;
}

strong_alias (pthread_initialize, libpthread_initialize)
strong_alias (pthread_shutdown, libpthread_shutdown)

strong_alias (pthread_once, __pthread_once)
strong_alias (pthread_atfork, __pthread_atfork)
strong_alias (pthread_detach, __pthread_detach)
strong_alias (pthread_initialize, __pthread_initialize)
strong_alias (pthread_initialize, __pthread_initialize_minimal)
strong_alias (pthread_initialize, pthread_initialize_minimal)
strong_alias (pthread_mutexattr_init, __pthread_mutexattr_init)
strong_alias (pthread_mutexattr_destroy, __pthread_mutexattr_destroy)
strong_alias (pthread_mutexattr_setprioceiling, __pthread_mutexattr_setprioceiling)
strong_alias (pthread_mutexattr_getprioceiling, __pthread_mutexattr_getprioceiling)
strong_alias (pthread_mutexattr_setprotocol, __pthread_mutexattr_setprotocol)
strong_alias (pthread_mutexattr_getprotocol, __pthread_mutexattr_getprotocol)
strong_alias (pthread_mutexattr_setpshared, __pthread_mutexattr_setpshared)
strong_alias (pthread_mutexattr_getpshared, __pthread_mutexattr_getpshared)
strong_alias (pthread_mutexattr_settype, __pthread_mutexattr_settype)
strong_alias (pthread_mutexattr_gettype, __pthread_mutexattr_gettype)
strong_alias (pthread_mutexattr_gettype, __pthread_mutexattr_getkind_np)
strong_alias (pthread_mutexattr_settype, __pthread_mutexattr_setkind_np)
strong_alias (pthread_mutexattr_gettype, pthread_mutexattr_getkind_np)
strong_alias (pthread_mutexattr_settype, pthread_mutexattr_setkind_np)
strong_alias (pthread_mutex_init, __pthread_mutex_init)
strong_alias (pthread_mutex_destroy, __pthread_mutex_destroy)
strong_alias (pthread_mutex_setprioceiling, __pthread_mutex_setprioceiling)
strong_alias (pthread_mutex_getprioceiling, __pthread_mutex_getprioceiling)
strong_alias (pthread_mutex_lock, __pthread_mutex_lock)
strong_alias (pthread_mutex_trylock, __pthread_mutex_trylock)
strong_alias (pthread_mutex_unlock, __pthread_mutex_unlock)
strong_alias (pthread_rwlock_init, __pthread_rwlock_init)
strong_alias (pthread_rwlock_destroy, __pthread_rwlock_destroy)
strong_alias (pthread_rwlock_rdlock, __pthread_rwlock_rdlock)
strong_alias (pthread_rwlock_rdlock, __pthread_rwlock_rdlock)
strong_alias (pthread_rwlock_tryrdlock, __pthread_rwlock_tryrdlock)
strong_alias (pthread_rwlock_wrlock, __pthread_rwlock_wrlock)
strong_alias (pthread_rwlock_trywrlock, __pthread_rwlock_trywrlock)
strong_alias (pthread_rwlock_unlock, __pthread_rwlock_unlock)
strong_alias (pthread_rwlockattr_destroy, __pthread_rwlockattr_destroy)
strong_alias (pthread_key_create, __pthread_key_create)
strong_alias (pthread_kill_other_threads_np, __pthread_kill_other_threads_np)

strong_alias (__flockfile, flockfile)
strong_alias (__funlockfile, funlockfile)
strong_alias (__ftrylockfile, ftrylockfile)
strong_alias (__flockfile, _IO_flockfile)
strong_alias (__funlockfile, _IO_funlockfile)
strong_alias (__ftrylockfile, _IO_ftrylockfile)

strong_alias (__errno_location, __h_errno_location)

versioned_symbol (__pthread_attr_init_2_1, pthread_attr_init, GLIBC_2.1)
versioned_symbol (__pthread_create_2_1, pthread_create, GLIBC_2.1)

versioned_symbol (__sigaction, sigaction, GLIBC_2.0)
versioned_symbol (__siglongjmp, siglongjmp, GLIBC_2.0)
versioned_symbol (__longjmp, longjmp, GLIBC_2.0)
versioned_symbol (__raise, raise, GLIBC_2.0)
/*end ibm*/
