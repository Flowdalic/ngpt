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
**  pth_sync.c: Pth synchronization facilities
*/
                             /* ``It is hard to fly with
                                  the eagles when you work
                                  with the turkeys.''
                                          -- Unknown  */

/************************************************************************************
**   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING          
************************************************************************************
**
**   DO NOT PUT PTH_DEBUG or PRINTF STATEMENTS IN THIS FILE.
**
**	The debug/print routines and their variations use the pthread_mutex routines
**	which subsequently find their way into the routines in this source file.  This
**	can and will cause recursive calling and eventual stack overflow.
**
************************************************************************************
**   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING   WARNING          
************************************************************************************/
#ifdef PTH_DEBUG			 /* Protection if warning is not heeded... */
#undef PTH_DEBUG
#endif

#include "pth_p.h"
#include "allocation.h"
#include "schedule.h"
#include <sys/stat.h>
#include <errno.h>

  /* Debug switches [0/1 disable/enable] */

#define SYNC_DEBUG_SHARED_INIT 1


#if cpp
#define PTH_SHARED_FILENAME	    "/ngpt"
#ifndef PTH_DEFAULT_MAX_SHARED_OBJECTS
#define PTH_DEFAULT_MAX_SHARED_OBJECTS	    256
#endif
#ifndef PTH_DEFAULT_SHARED_ADDRESS
#define PTH_DEFAULT_SHARED_ADDRESS  0
#endif
#define PTH_SHARED_TYPE_MUTEX	    0
#define PTH_SHARED_TYPE_COND	    1
#define PTH_VERSION_CURRENT
typedef struct pth_shared_sync_st   pth_shared_sync_t;
struct pth_shared_sync_st {
    int		used;
    union {
	pth_mutex_t	mx;
	pth_cond_t	cn;
    } u;
};
typedef struct pth_shared_area_st   pth_shared_area_t;
struct pth_shared_area_st {
    int			verid;
    size_t		addr;
    size_t		num_objs;
    pth_qlock_t		lock;
    pth_shared_sync_t	o[0];
};

#define COMPUTE_PTH_SHARED_SIZE(nobj)  ((size_t)(sizeof(pth_shared_area_t) + (nobj) * sizeof(pth_shared_sync_t)))

#endif

intern int		    pth_shared_fd = 0;
intern pth_shared_area_t    *pth_shared_area;
intern size_t		    PTH_SHARED_SIZE;
intern size_t		    PTH_MAX_SHARED_OBJECTS;

/*
**  Mutual Exclusion Locks
*/

intern int pth_initialize_shared(void)
{
    struct pth_shared_area_st file_shared_area;
    unsigned long pth_shared_address;

    if (pth_shared_fd == 0) {
	/* Open the shared area file that we'll mmap */
	pth_shared_fd = shared_area_file_open(O_RDWR, (S_IRWXU | S_IRWXG | S_IRWXO));
	if (pth_shared_fd == -1) {
            __fdebugmsg (SYNC_DEBUG_SHARED_INIT,
                         "%s(): cannot open shared area: %s\n",
                         __FUNCTION__, strerror (errno));
            return FALSE;
        }

	/* read in the shared file so that we can get init params */
	if (pth_sc(read)(pth_shared_fd, &file_shared_area,
				      sizeof(file_shared_area)) == -1) {
            __fdebugmsg (SYNC_DEBUG_SHARED_INIT,
                         "%s(): cannot read shared area file: %s\n",
                         __FUNCTION__, strerror (errno));
            return FALSE;
        }

	if (file_shared_area.verid != PTH_INTERNAL_VERSION) {
            __fdebugmsg (SYNC_DEBUG_SHARED_INIT,
                         "%s(): shared area file version wrong\n", __FUNCTION__);
            return FALSE;
        }

	/* access the shared area address and number of lock objects */
	pth_shared_address = file_shared_area.addr;
	PTH_MAX_SHARED_OBJECTS = file_shared_area.num_objs;
	PTH_SHARED_SIZE = COMPUTE_PTH_SHARED_SIZE(PTH_MAX_SHARED_OBJECTS);

	/* Do the mmap of the shared area */
	pth_shared_area = (pth_shared_area_t *)mmap((void *)pth_shared_address,
						    PTH_SHARED_SIZE,
						    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, 
						    pth_shared_fd, 0);
	if (pth_shared_area == MAP_FAILED) {
            __fdebugmsg (SYNC_DEBUG_SHARED_INIT,
                         "%s(): cannot map shared area: %s\n",
                         __FUNCTION__, strerror (errno));
	    return FALSE;
        }
        futex_region(pth_shared_area, PTH_SHARED_SIZE);
	pth_lock_init(pth_shared_area->lock);
    }
    return TRUE;
}

intern int pth_find_shared_mutex(pth_mutex_t *mutex)
{
    int indx;
    int retval;

    pth_acquire_lock(&(pth_shared_area->lock));
    /* search the in-use objects in the pth_shared_area for mutex addr */
    for (indx = 0, retval = FALSE; indx < PTH_MAX_SHARED_OBJECTS; ++indx) {
	/* if not in use, skip to next */
	if (pth_shared_area->o[indx].used == FALSE)
	    continue;
	/* if in use and equal to value passed we found it, break out */
	if (mutex == &pth_shared_area->o[indx].u.mx) {
	    retval = TRUE;
	    break;
	}
    }
    /* release shared area lock and return with search status */
    pth_release_lock(&(pth_shared_area->lock));
    return retval;
}

intern pth_mutex_t *pth_alloc_shared_mutex(void)
{
    int indx;
    pth_mutex_t *rmx;

    pth_acquire_lock(&(pth_shared_area->lock));
    
    for (indx = 0, rmx = NULL; indx < PTH_MAX_SHARED_OBJECTS; ++indx) {
	/* If slot is in use skip over it */
	if (pth_shared_area->o[indx].used == TRUE)
	    continue;
	/* Found available slot, allocate it */
	rmx = &(pth_shared_area->o[indx].u.mx);
	rmx->mx_index = indx;
	rmx->mx_shared.type = PTH_SHARED_TYPE_MUTEX;
	pth_shared_area->o[indx].used = TRUE;
	msync(pth_shared_area, PTH_SHARED_SIZE, MS_SYNC | MS_INVALIDATE);
	break;
    }

    pth_release_lock(&(pth_shared_area->lock));
    return rmx;
}

intern pth_cond_t *pth_alloc_shared_cond(void)
{
    int indx;
    pth_cond_t *rcn;
    
    pth_acquire_lock(&(pth_shared_area->lock));

    for (indx = 0, rcn = NULL; indx < PTH_MAX_SHARED_OBJECTS; ++indx) {
	/* If slot is in use skip over it */
	if (pth_shared_area->o[indx].used == TRUE)
	    continue;
	/* Found available slot, allocate it */
	rcn = &(pth_shared_area->o[indx].u.cn);
	rcn->cn_index = indx;
	rcn->cn_shared.type = PTH_SHARED_TYPE_COND;
	pth_shared_area->o[indx].used = TRUE;
	msync(pth_shared_area, PTH_SHARED_SIZE, MS_SYNC | MS_INVALIDATE);
	break;
    }

    pth_release_lock(&(pth_shared_area->lock));
    return rcn;
}

int pth_mutex_init(pth_mutex_t *mutex, pth_mutexattr_t *pattr)
{
    int pshared = (pattr != NULL) ? pattr->pshared : FALSE;
    if (mutex == NULL)
	return FALSE;
    rfutex_init(&mutex->mx_shared, mutex, pshared);
    if (pshared == FALSE)
	mutex->mx_index = -1;
    else {
	if ((mutex->mx_count > 0) && (mutex->mx_owner != pth_get_current()))
	    return FALSE;
	else if (pattr && (pattr->robustness == PTH_MUTEX_ROBUST_NP))
	    mutex->mx_shared.type |= PTH_MUTEX_ROBUST_NP;
    }

    mutex->mx_type = (pattr != NULL) ? pattr->type : PTH_MUTEX_NORMAL;
    mutex->mx_node.rn_next = NULL;
    mutex->mx_node.rn_prev = NULL;
    mutex->mx_state = PTH_MUTEX_INITIALIZED;
    mutex->mx_owner = NULL;
    mutex->mx_count = 0;
    mutex->mx_owner_pid = 0; 
    pth_qlock_init(&mutex->mx_lock);
    mutex->mx_waitlist.th_next = &mutex->mx_waitlist;
    mutex->mx_waitlist.th_prev = &mutex->mx_waitlist;
    mutex->mx_waitcount = 0;
    return TRUE;
}

static inline void _pth_acquire(pth_qlock_t *spinlock, pid_t tid)
{
    spin_lock(&spinlock->spinlock, (void *) tid, NULL);
}
 
static inline void _pth_release(pth_qlock_t *spinlock, pid_t tid)
{
    spin_unlock(&spinlock->spinlock, (void *) tid);
}

int pth_mutex_acquire(pth_mutex_t *mutex, int tryonly, pth_event_t ev_extra)
{
    pth_descr_t descr = pth_get_native_descr();
    pth_t current;
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_event_t ev;
    int retcode = 0;

    if (mutex->mx_state & PTH_MUTEX_INTERNAL_LOCKED) {
	_pth_acquire_lock(&(mutex->mx_lock), 0);
	return 0;
    }
    if (unlikely (descr == NULL))
	return EINVAL;

    if (mutex->mx_shared.pshared == TRUE)
	return pth_shared_mutex_acquire(mutex, tryonly, ev_extra, descr);

    current = descr->current;
    _pth_acquire(&(mutex->mx_lock), descr->tid);

    /* still not locked, so simply acquire mutex? */
    if ((!(mutex->mx_state & PTH_MUTEX_LOCKED)) && !mutex->mx_waitcount) {

	/* 
	 * Should be no contention here...
	 *  But only try to acquire it if we don't already have it...
	 */

        mutex->mx_state |= PTH_MUTEX_LOCKED;
        mutex->mx_owner = current;
        mutex->mx_owner_pid = descr->pid;
        mutex->mx_count = 1;
	if (!current->mutex_owned)
	    current->mutex_owned = mutex;
	else
	    pth_ring_append(&(current->mutexring), &(mutex->mx_node));
	_pth_release(&(mutex->mx_lock), descr->tid);
        return 0;
    }

    /* already locked by caller? */
    if (mutex->mx_owner == current && mutex->mx_count >= 1) {
	if (mutex->mx_type == PTH_MUTEX_RECURSIVE_NP) {
	    /* recursive lock */
            mutex->mx_count++;
	} else
	    retcode = tryonly ? EBUSY : EDEADLK;
	_pth_release(&(mutex->mx_lock), descr->tid);
        return retcode;
    }

    /* should we just tryonly?			  ibm*/
    if (tryonly) {				/*ibm*/
	_pth_release(&(mutex->mx_lock), descr->tid);
        return EBUSY;				/*ibm*/
    }

    if (pth_number_of_natives > 1)
	PTH_ELEMENT_INSERT(&current->mutex_cond_wait, &mutex->mx_waitlist);

    if (!descr->is_bounded) {

	/* wait for mutex to become unlocked.. */
	for (;;) {

	    _pth_release(&(mutex->mx_lock), descr->tid);
	    /* Set up the event handling...waiting for mutex */
	    ev = pth_event(PTH_EVENT_MUTEX|PTH_MODE_STATIC, &ev_key, mutex);
	    if (ev_extra != NULL)
    		pth_event_concat(ev, ev_extra, NULL);

	    pth_wait(ev);
	    if (ev_extra != NULL) {
		pth_event_isolate(ev);
		if (!PTH_EVENT_OCCURRED(ev))
		    return EINTR;
	    }
	    descr = pth_get_native_descr();
	    _pth_acquire(&(mutex->mx_lock), descr->tid);

	    if (!(mutex->mx_state & PTH_MUTEX_LOCKED)) {
		if (pth_number_of_natives > 1)
		    PTH_ELEMENT_DELETE(&current->mutex_cond_wait); 
		break;   /* for non-shared mutex, process original way */
	    }
	}
    } else {
	fd_set rfds;
	char minibuf[64];
	int rc, fdmax;
	
	for (;;) {
	    FD_ZERO(&rfds);
	    FD_SET(descr->sigpipe[0], &rfds);
	    fdmax = descr->sigpipe[0]; 

	    mutex->mx_waitcount++;
	    descr->is_bound = 0;
	    current->state = PTH_STATE_WAITING;
	    _pth_release(&(mutex->mx_lock), descr->tid);

	    if (mutex->mx_state & PTH_MUTEX_LOCKED) {
		while ((rc = pth_sc(select)(fdmax+1, &rfds, NULL, NULL, NULL)) < 0
			&& errno == EINTR) ;
		if (rc > 0 && FD_ISSET(descr->sigpipe[0], &rfds))
		    FD_CLR(descr->sigpipe[0], &rfds);
		while (pth_sc(read)(descr->sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;
	    }

	    _pth_acquire(&(mutex->mx_lock), descr->tid);
	    mutex->mx_waitcount--;
	    descr->is_bound = 1;
	    current->state = PTH_STATE_READY;
	    if (!(mutex->mx_state & PTH_MUTEX_LOCKED)) {
		if (pth_number_of_natives > 1)
		    PTH_ELEMENT_DELETE(&current->mutex_cond_wait);
		break;   /* for non-shared mutex, process original way */
	    }
	}
    }
    /* now it's again unlocked, so acquire mutex */ 
    mutex->mx_state |= PTH_MUTEX_LOCKED;
    mutex->mx_owner = current;
    mutex->mx_owner_pid = descr->pid;
    mutex->mx_count = 1;
    if (!current->mutex_owned)
	current->mutex_owned = mutex;
    else
	pth_ring_append(&(current->mutexring), &(mutex->mx_node));
    _pth_release(&(mutex->mx_lock), descr->tid);
    return retcode;
}

int pth_shared_mutex_acquire(pth_mutex_t *mutex, int tryonly, pth_event_t ev_extra, 
				pth_descr_t descr)
{
    pth_t current;
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_event_t ev;
    int need_futex = TRUE;
    int futx_fd = 0;
    int retcode = 0;

    current = descr->current;
    _pth_acquire(&(mutex->mx_lock), descr->tid);

    /* still not locked, so simply acquire mutex? */
    if ((!(mutex->mx_state & PTH_MUTEX_LOCKED)) && !mutex->mx_waitcount) {

	/* 
	 * Should be no contention here...
	 *  But only try to acquire it if we don't already have it...
	 */
	/* check the return value of futex_acquire and lock the mutex
	 * only if it return success.
	 * no need to hold mx_lock for futex calls .... 
	 */
	_pth_release(&(mutex->mx_lock), descr->tid);
        futx_fd = futex_acquire(&mutex->mx_shared.futex, FALSE);
	_pth_acquire(&(mutex->mx_lock), descr->tid);
	/* if new request for acquire and waiting for futex.... */
	if (futx_fd > 0) { 
	    /* don't acquire the futex again, it's already waiting! */
	    need_futex = FALSE;
	    goto wait; 
	}
	if (futx_fd == -1) {
	    _pth_release(&(mutex->mx_lock), descr->tid);
	    return EINVAL;
	}

        mutex->mx_state |= PTH_MUTEX_LOCKED;
        mutex->mx_owner = current;
        mutex->mx_owner_pid = descr->pid;
        mutex->mx_count = 1;
	if (!current->mutex_owned)
	    current->mutex_owned = mutex;
	else
	    pth_ring_append(&(current->mutexring), &(mutex->mx_node));
	_pth_release(&(mutex->mx_lock), descr->tid);
        return 0;
    }

wait:
    /* already locked by caller? */
    if (mutex->mx_owner == current && mutex->mx_count >= 1 && mutex->mx_owner_pid==descr->pid) {
	if (mutex->mx_type == PTH_MUTEX_RECURSIVE_NP) {
	    /* recursive lock */
            mutex->mx_count++;
	} else
	    retcode = tryonly ? EBUSY : EDEADLK;
	_pth_release(&(mutex->mx_lock), descr->tid);
        return retcode;
    }

    /* should we just tryonly?			  ibm*/
    if (tryonly) {				/*ibm*/
	_pth_release(&(mutex->mx_lock), descr->tid);
        return EBUSY;				/*ibm*/
    }

    /* Check whether the current owner is dead or alive for robust mutex */
    if ((mutex->mx_shared.type & PTH_MUTEX_ROBUST_NP) &&
	(kill(mutex->mx_owner_pid, 0) == -1)) {
	/* if current owner is dead, acquire the mutex and return EOWNERDEAD */
	mutex->mx_node.rn_next = NULL;
	mutex->mx_node.rn_prev = NULL;
	mutex->mx_state |= PTH_MUTEX_NOT_CONSISTENT;
	retcode = EOWNERDEAD;
	goto done;
    }

    if (!descr->is_bounded) {

	/* wait for mutex to become unlocked.. */
	for (;;) {

	    _pth_release(&(mutex->mx_lock), descr->tid);
	    /* 
	     * Before we set up event handling, set up the async wait for the futex 
	     * if this is a shared mutex...
	     * This may give a us the mutex, in which case, we'll acquire mutex...
	     */
	    if (need_futex) {
		futx_fd = futex_acquire(&mutex->mx_shared.futex, FALSE);
		if (futx_fd == 0) {	/* got the futex...now acquire mutex */
		    _pth_acquire(&(mutex->mx_lock), descr->tid);
		    break;
		}
		if (futx_fd == -1)	/* got an error? */
		    return EINVAL;  /* TODO: check what we should return!!! */
	    }
	    /* Set up the event handling...waiting for futex... */
	    ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, futx_fd);
	    if (ev_extra != NULL)
    		pth_event_concat(ev, ev_extra, NULL);

	    pth_wait(ev);
	    if (ev_extra != NULL) {
		pth_event_isolate(ev);
		if (!PTH_EVENT_OCCURRED(ev))
		    return EINTR;
	    }
	    if (futx_fd > 0)
		close(futx_fd);
	    descr = pth_get_native_descr();
	    _pth_acquire(&(mutex->mx_lock), descr->tid);

	    /* for shared-mutex, futex acquire is needed */
	    need_futex = TRUE;
	    if (mutex->mx_state & PTH_MUTEX_NOT_CONSISTENT) {
		/* if mutex is not consistent, return error */
		_pth_release(&(mutex->mx_lock), descr->tid);
		return ENOTRECOVERABLE;
	    }
	}
    } else {
	fd_set rfds;
	char minibuf[64];
	int rc, fdmax;
	
	for (;;) {
	    /*
	     * Before we set up event handling, set up the async wait for the futex 
	     * if this is a shared mutex...
	     * This may give a us the mutex, in which case, we'll acquire mutex....
	     */
	    if (need_futex) {
		_pth_release(&(mutex->mx_lock), descr->tid);
                futx_fd = futex_acquire(&mutex->mx_shared.futex, FALSE);
		_pth_acquire(&(mutex->mx_lock), descr->tid);
		if (futx_fd == 0) 	/* got the futex...now acquire mutex */
		   break;
	        if (futx_fd == -1) {	/* got an error? */
		   _pth_release(&(mutex->mx_lock), descr->tid);
		   return EINVAL;  /* TODO: check what we should return!!! */
		}
	    }
	    FD_ZERO(&rfds);
	    FD_SET(futx_fd, &rfds);
	    fdmax = futx_fd;
	    FD_SET(descr->sigpipe[0], &rfds);
	    if (fdmax < descr->sigpipe[0])
		fdmax = descr->sigpipe[0];

	    mutex->mx_waitcount++;
	    descr->is_bound = 0;
	    current->state = PTH_STATE_WAITING;
	    _pth_release(&(mutex->mx_lock), descr->tid);

	    if (futx_fd > 0 || mutex->mx_state & PTH_MUTEX_LOCKED) {
		while ((rc = pth_sc(select)(fdmax+1, &rfds, NULL, NULL, NULL)) < 0
			&& errno == EINTR) ;
		if (rc > 0 && FD_ISSET(descr->sigpipe[0], &rfds))
		    FD_CLR(descr->sigpipe[0], &rfds);
		while (pth_sc(read)(descr->sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;
	    }
	    if (futx_fd > 0)
		close(futx_fd);

	    _pth_acquire(&(mutex->mx_lock), descr->tid);
	    mutex->mx_waitcount--;
	    descr->is_bound = 1;
	    current->state = PTH_STATE_READY;
	    /* for shared-mutex, futex acquire is needed */
	    need_futex = TRUE;
	    if (mutex->mx_state & PTH_MUTEX_NOT_CONSISTENT) {
		/* if mutex is not consistent, return error */
		_pth_release(&(mutex->mx_lock), descr->tid);
		return ENOTRECOVERABLE;
	    }
	}
    }
done:
    /* now it's again unlocked, so acquire mutex */ 
    mutex->mx_state |= PTH_MUTEX_LOCKED;
    mutex->mx_owner = current;
    mutex->mx_owner_pid = descr->pid;
    mutex->mx_count = 1;
    if (!current->mutex_owned)
	current->mutex_owned = mutex;
    else
	pth_ring_append(&(current->mutexring), &(mutex->mx_node));
    _pth_release(&(mutex->mx_lock), descr->tid);
    return retcode;
}

int pth_mutex_release(pth_mutex_t *mutex)
{
    pth_t current, tmp;
    pth_descr_t descr = pth_get_native_descr();
    pth_list_t *mq;

    if (!(mutex->mx_state & PTH_MUTEX_LOCKED)) {
	if (mutex->mx_state & PTH_MUTEX_INTERNAL_LOCKED) {
	    _pth_release_lock(&(mutex->mx_lock), 0);
	    return 0;
	}
	return EPERM;
    }
    if ((current = descr->current) != mutex->mx_owner)
	return EPERM;
	
    _pth_acquire(&(mutex->mx_lock), descr->tid);
    /* decrement recursion counter and release mutex */
    mutex->mx_count--;
    if (mutex->mx_count <= 0) {
        mutex->mx_state &= ~(PTH_MUTEX_LOCKED);
        mutex->mx_owner = NULL;
        mutex->mx_owner_pid = 0;
        mutex->mx_count = 0;
	if (current->mutex_owned == mutex)
	    current->mutex_owned = 0;
	else
	    pth_ring_delete(&(current->mutexring), &(mutex->mx_node));
	if (!mutex->mx_shared.pshared) {
	    if (pth_number_of_natives > 1) {
		mq = mutex->mx_waitlist.th_prev;
		if (mq != &mutex->mx_waitlist) {
		    char c = (int)1;
		    pth_descr_t ds;
		    tmp = (pth_t)((char *)mq - (int)&((pth_t)0)->mutex_cond_wait);
		    if ((ds = tmp->boundnative)) {
			_pth_release(&(mutex->mx_lock), descr->tid);
			pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
			return 0;
		    } else {
			ds = tmp->waited_native;
			if (ds != NULL) {
			    /* move waiting thread from wait-queue to ready-queue */
			    spin_lock (&ds->wait_queue.lock, descr, NULL);
			    if (tmp->waited_native) { /* to avoid race with eventmgr */
				__pqueue_delete_node(&tmp->node);
				tmp->waited_native = NULL;
				spin_unlock(&ds->wait_queue.lock, descr);

				spin_lock(&pth_RQ.lock, descr, NULL);
				tmp->state = PTH_STATE_READY;
				__thread_eprio_recalculate (tmp);
				__pqueue_append_node(&pth_RQ, &tmp->node);
				spin_unlock(&pth_RQ.lock, descr);
			    } else
				spin_unlock(&ds->wait_queue.lock, descr);
			}
			_pth_release(&(mutex->mx_lock), descr->tid);
		    }
		    if (descr->current->boundnative)
			pth_wakeup_anative();
		    return 0;
		}
	    }
	    _pth_release(&(mutex->mx_lock), descr->tid);
	} else {
	    _pth_release(&(mutex->mx_lock), descr->tid);
            futex_release(&mutex->mx_shared.futex);
	}
	return 0;
    }
    _pth_release(&(mutex->mx_lock), descr->tid);
    return 0;
}

intern void pth_mutex_releaseall(pth_t thread)
{
    pth_ringnode_t *rn, *rnf;
    pth_mutex_t *m;

    if (thread == NULL)
        return;
    /* iterate over all mutexes of thread */
    pth_acquire_lock(&(thread->lock));
    if (thread->mutex_owned)
	pth_mutex_release(thread->mutex_owned);
    rn = rnf = pth_ring_first(&(thread->mutexring));
    while (rn != NULL) {
	m = (pth_mutex_t *)((char *)rn - (int)&((pth_mutex_t *)0)->mx_node);
        pth_mutex_release(m);

	/* 
	 * make sure that if mutex was grabbed recursively,
	 * it's released recursively...
	 */
	if (m->mx_count > 0)
	    continue;

        rn = pth_ring_next(&(thread->mutexring), rn);
        if (rn == rnf)
            break;
    }
    pth_release_lock(&(thread->lock));
    return;
}


int pth_mutex_destroy(pth_mutex_t *mutex)
{
    /* 
     * Sanity check... 
     *	This only valid for shared mutexes.
     */
    if (mutex->mx_shared.pshared != TRUE)
	return EINVAL;

    /* Clean up the area in shared memory for this mutex... */
    pth_shared_area->o[mutex->mx_index].used = FALSE;
    mutex->mx_state	= 0;
    mutex->mx_index	= 0;
    mutex->mx_owner	= 0;
    mutex->mx_owner_pid	= 0;
    mutex->mx_count	= 0;
    mutex->mx_waitcount	= 0;
    pth_lock_init(mutex->mx_lock);

    /* Clean up the futex as well... */
    futex_destroy(&mutex->mx_shared.futex);

    return 0;
}
    
/*
**  Read-Write Locks
*/

int pth_rwlock_init(pth_rwlock_t *rwlock, int pshared)
{
    rwlock->rw_state = PTH_RWLOCK_INITIALIZED;
    rwlock->rw_readers = 0;

    if (pshared == TRUE) {
	pth_mutexattr_t a;
	a.type = 0;
	a.priority = 0;
	a.protocol = 0;
	a.pshared = TRUE;
	a.robustness = 0;
	if ((rwlock->rw_mutex_rd = pth_alloc_shared_mutex()) == NULL)
	    return ENOMEM;
	pth_mutex_init(rwlock->rw_mutex_rd, &a);
	if ((rwlock->rw_mutex_rw = pth_alloc_shared_mutex()) == NULL) {
	    pth_mutex_destroy(rwlock->rw_mutex_rd);
	    return ENOMEM;
	}
	pth_mutex_init(rwlock->rw_mutex_rw, &a);
	rwlock->rw_pshared = TRUE;
    } else {
	/* we allocate two pth_mutex_t at once to save a page */
	if ((rwlock->rw_mutex_rd = (pth_mutex_t *)pth_malloc(sizeof(pth_mutex_t)<<1)) == NULL)
	    return ENOMEM;
	rwlock->rw_mutex_rw = &rwlock->rw_mutex_rd[1];
	pth_mutex_init(rwlock->rw_mutex_rd, NULL);
	pth_mutex_init(rwlock->rw_mutex_rw, NULL);
	rwlock->rw_pshared = FALSE;
    }
    return 0;
}

int pth_rwlock_destroy(pth_rwlock_t *rwlock)
{

    if ((rwlock->rw_mutex_rd->mx_state & PTH_MUTEX_LOCKED) ||
	(rwlock->rw_mutex_rw->mx_state & PTH_MUTEX_LOCKED))
	return EBUSY;

    if (rwlock->rw_pshared == FALSE){
	pth_free_mem(rwlock->rw_mutex_rd, sizeof(pth_rwlock_t)<<1);
    } else {
	pth_mutex_destroy(rwlock->rw_mutex_rd);
	pth_mutex_destroy(rwlock->rw_mutex_rw);
    }
    return 0;
}

int pth_rwlock_acquire(pth_rwlock_t *rwlock, int op, int tryonly, pth_event_t ev_extra)
{
    int rc;
    if (!(rwlock->rw_state & PTH_RWLOCK_INITIALIZED))
        return EINVAL;

    /* acquire lock */
    if (op == PTH_RWLOCK_RW) {
        /* read-write lock is simple */
        if ((rc = pth_mutex_acquire(rwlock->rw_mutex_rw, tryonly, ev_extra)))
            return rc;
        rwlock->rw_mode = PTH_RWLOCK_RW;
    }
    else {
        /* read-only lock is more complicated to get right */
        if ((rc = pth_mutex_acquire(rwlock->rw_mutex_rd, tryonly, ev_extra)))
            return rc;
        rwlock->rw_readers++;
        if (rwlock->rw_readers == 1) {
            if ((rc = pth_mutex_acquire(rwlock->rw_mutex_rw, tryonly, ev_extra))) {
                rwlock->rw_readers--;
                pth_mutex_release(rwlock->rw_mutex_rd);
                return rc;
            }
        }
        rwlock->rw_mode = PTH_RWLOCK_RD;
        pth_mutex_release(rwlock->rw_mutex_rd);
    }
    return 0;
}

int pth_rwlock_release(pth_rwlock_t *rwlock)
{
    int rc;

    if (!(rwlock->rw_state & PTH_RWLOCK_INITIALIZED))
        return EINVAL;

    /* release lock */
    if (rwlock->rw_mode == PTH_RWLOCK_RW) {
        /* read-write unlock is simple */
        if ((rc = pth_mutex_release(rwlock->rw_mutex_rw)))
            return rc;
    }
    else {
        /* read-only unlock is more complicated to get right */
        if ((rc = pth_mutex_acquire(rwlock->rw_mutex_rd, FALSE, NULL)))
            return rc;
        rwlock->rw_readers--;
        if (rwlock->rw_readers == 0) {
            if ((rc = pth_mutex_release(rwlock->rw_mutex_rw))) {
                rwlock->rw_readers++;
                pth_mutex_release(rwlock->rw_mutex_rd);
                return rc;
            }
        }
        rwlock->rw_mode = PTH_RWLOCK_RD;
        pth_mutex_release(rwlock->rw_mutex_rd);
    }
    return 0;
}

/*
**  Condition Variables
*/

#define PTH_COND_BOUNDED _BIT(4)

int _pth_cond_init(pth_cond_t *cond, int pshared)
{
    rfutex_init(&cond->cn_shared, cond, pshared);
    if (pshared != TRUE)
	cond->cn_index = -1;

    cond->cn_state   = PTH_COND_INITIALIZED;
    cond->cn_waiters = 0;
    cond->cn_wakecnt = 0;
    pth_lock_init(cond->cn_lock);
    cond->cn_waitlist.th_next = &cond->cn_waitlist;
    cond->cn_waitlist.th_prev = &cond->cn_waitlist;
    return TRUE;
}

int pth_cond_destroy(pth_cond_t *cond)
{
    /*
     * Sanity check...
     * This only valid for shared cond variables.
     */
    if (cond->cn_shared.pshared != TRUE)
       return EINVAL;

    /* Clean up the area in shared memory for this cond var... */
    pth_shared_area->o[cond->cn_index].used = FALSE;
    cond->cn_state     = 0;
    cond->cn_index     = 0;
    cond->cn_waiters   = 0;
    cond->cn_wakecnt = 0;
    pth_lock_init(cond->cn_lock);

    /* Clean up the futex as well... */
    rfutex_destroy(&cond->cn_shared);

    return 0;
}

static void pth_cond_cleanup_handler(void *_cleanvec) __attribute__ ((unused));
static void pth_cond_cleanup_handler(void *_cleanvec)
{
    pth_mutex_t *mutex = (pth_mutex_t *)(((void **)_cleanvec)[0]);
    pth_cond_t  *cond  = (pth_cond_t  *)(((void **)_cleanvec)[1]);
    pth_t current = pth_get_current();

    /* fix number of waiters */
    pth_acquire_lock(&(cond->cn_lock));
    cond->cn_waiters--;
    if (pth_number_of_natives > 1 && !cond->cn_shared.pshared)
	PTH_ELEMENT_DELETE(&current->mutex_cond_wait);
    if (cond->cn_waiters == 0 || (cond->cn_state & PTH_COND_HANDLED)) {
	/* clean signal */
	if (cond->cn_state & PTH_COND_SIGNALED) {
	    cond->cn_state &= ~(PTH_COND_SIGNALED);
	    cond->cn_state &= ~(PTH_COND_BROADCAST);
	    cond->cn_state &= ~(PTH_COND_HANDLED);
	}
    }
    pth_release_lock(&(cond->cn_lock));

    /* re-acquire mutex when pth_cond_await() is cancelled
       in order to restore the condition variable semantics */
    pth_mutex_acquire(mutex, FALSE, NULL);

    return;
}

int pth_cond_await(pth_cond_t *cond, pth_mutex_t *mutex, pth_event_t ev_extra)
{
    static pth_key_t ev_key = PTH_KEY_INIT;
    pth_event_t ev;
    pth_descr_t descr = pth_get_native_descr();
    pid_t tid = descr->tid;
    pth_t current= descr->current;
    int futx_fd = 0;

    /* consistency checks */
    if ((!(cond->cn_state & PTH_COND_INITIALIZED)) || (mutex->mx_owner != current))
        return FALSE;

    _pth_acquire(&(cond->cn_lock), tid);

    /* add us to the number of waiters */
    cond->cn_waiters++;
    if (cond->cn_shared.pshared == TRUE) {
        futx_fd = futex_add_waiter(&cond->cn_shared.futex);
	if (futx_fd < 0) {
	    _pth_release(&(cond->cn_lock), tid);
	    return FALSE;
	}
    } else if (pth_number_of_natives > 1) 
	PTH_ELEMENT_INSERT(&current->mutex_cond_wait, &cond->cn_waitlist);

    if (descr->is_bounded) {
	fd_set rfds;
	char minibuf[64];
	int rc, fdmax;
	pth_time_t delay;
	struct timeval delay_timeval;
	struct timeval *pdelay = NULL;
	pth_time_t now;
	
	FD_ZERO(&rfds);
	if (futx_fd > 0) {
	    FD_SET(futx_fd, &rfds);
	    fdmax = futx_fd;
	    FD_SET(descr->sigpipe[0], &rfds);
	    if (fdmax < descr->sigpipe[0])
		fdmax = descr->sigpipe[0];
	} else {
	    FD_SET(descr->sigpipe[0], &rfds);
	    fdmax = descr->sigpipe[0]; 
	}
	if (ev_extra != NULL) {
	    pth_time_set_now(&now);
	    delay = ev_extra->ev_args.TIME.tv;
	    pth_time_sub(&delay, &now);
	    delay_timeval = pth_time_to_timeval(&delay);
	    pdelay = &delay_timeval;
	}

	descr->is_bound = 0;
	current->state = PTH_STATE_WAITING;
        _pth_release(&(cond->cn_lock), tid);
	pth_mutex_release(mutex);

	while ((rc = pth_sc(select)(fdmax+1, &rfds, NULL, NULL, pdelay)) < 0
		&& errno == EINTR) ;

	if (rc == 0 && ev_extra != NULL)
	    ev_extra->ev_occurred = TRUE;
	if (rc > 0 && FD_ISSET(descr->sigpipe[0], &rfds))
	    FD_CLR(descr->sigpipe[0], &rfds);
	while (pth_sc(read)(descr->sigpipe[0], minibuf, sizeof(minibuf)) > 0) ;
	if (futx_fd > 0)
	    close(futx_fd);

	_pth_acquire(&(cond->cn_lock), tid);
	descr->is_bound = 1;
	current->state = PTH_STATE_READY;
    } else {
	_pth_release(&(cond->cn_lock), tid);

	/* wait until the condition is signaled */
	if (futx_fd > 0)
	    ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, futx_fd);
	else
	    ev = pth_event(PTH_EVENT_COND|PTH_MODE_STATIC, &ev_key, cond);
	if (ev_extra != NULL)
	    pth_event_concat(ev, ev_extra, NULL);

	/* release mutex (caller had to acquire it first) */
	pth_mutex_release(mutex);

    wait:
	/* now wait... */
	pth_wait(ev);
	tid = current_tid();
	if (ev_extra != NULL)
	    pth_event_isolate(ev);
	if (futx_fd > 0)
	    close(futx_fd);

	_pth_acquire(&(cond->cn_lock), tid);

	if ((!(cond->cn_state & PTH_COND_SIGNALED)) && 
	    PTH_EVENT_OCCURRED(ev) && !cond->cn_shared.pshared) {
	    _pth_release(&(cond->cn_lock), tid);
	    goto wait;
	}
    }

    if (pth_number_of_natives > 1 && !cond->cn_shared.pshared)
	PTH_ELEMENT_DELETE(&current->mutex_cond_wait);

    /* remove us from the number of waiters */ 
    cond->cn_waiters--;

    if (cond->cn_waiters == 0 || (cond->cn_state & PTH_COND_HANDLED)) {
	/* clean signal */
	if (cond->cn_state & PTH_COND_SIGNALED) {
	    cond->cn_state &= ~(PTH_COND_BROADCAST);
	    cond->cn_state &= ~(PTH_COND_HANDLED);
	    if (--cond->cn_wakecnt == 0)
		cond->cn_state &= ~(PTH_COND_SIGNALED);
	}
    }

    _pth_release(&(cond->cn_lock), tid);

    /* reacquire mutex */
    pth_mutex_acquire(mutex, FALSE, NULL);

    if (current->cancelreq)
	pth_cancel_point(FALSE);
    return TRUE;
}

static int pth_check_waiters(void *arg)
{
    pth_cond_t *cond = (pth_cond_t *)arg;

    if (!cond->cn_waiters)
	return TRUE;
    else
	return FALSE;
}

int pth_cond_notify(pth_cond_t *cond, int broadcast)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT; 
    pth_descr_t descr = pth_get_native_descr();
    char c = (int)1;
    pth_descr_t ds = NULL;
    
    /* consistency checks */
    if (!(cond->cn_state & PTH_COND_INITIALIZED))
        return FALSE;

    _pth_acquire(&(cond->cn_lock), descr->tid);
    /* do something only if there is at least one waiters (POSIX semantics) */
    if (cond->cn_waiters > 0) {
	/* signal the condition */
	cond->cn_state |= PTH_COND_SIGNALED;
	if (broadcast)
	    cond->cn_state |= PTH_COND_BROADCAST;
	else
	    cond->cn_state &= ~(PTH_COND_BROADCAST);
	cond->cn_state &= ~(PTH_COND_HANDLED);

	if (pth_number_of_natives > 1 && !cond->cn_shared.pshared) {
	    pth_t tmp;
	    pth_list_t *cq;
	    cq = cond->cn_waitlist.th_prev;
	    while (cq != &cond->cn_waitlist) {
		tmp = (pth_t)((char *)cq - (int)&((pth_t)0)->mutex_cond_wait);
		if ((ds = tmp->boundnative)) {
		    if (!broadcast) {
			cond->cn_wakecnt++;
			_pth_release(&(cond->cn_lock), descr->tid);
		 	pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
			return TRUE;
		    }
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
		    ds = NULL;
		} else {
		    ds = tmp->waited_native;
		    if (ds != NULL) {
			/* move waiting thread from wait-queue to ready-queue */
			spin_lock (&ds->wait_queue.lock, descr, NULL);
			if (tmp->waited_native) { /* to avoid race with eventmgr */
			    if (!broadcast) cond->cn_state |= PTH_COND_HANDLED;
			    __pqueue_delete_node(&tmp->node);
			    tmp->waited_native = NULL;
			    spin_unlock(&ds->wait_queue.lock, descr);

			    spin_lock(&pth_RQ.lock, descr, NULL);
			    tmp->state = PTH_STATE_READY;
			    __thread_eprio_recalculate (tmp);
			    __pqueue_append_node(&pth_RQ, &tmp->node);
			    spin_unlock(&pth_RQ.lock, descr);
			} else
			    spin_unlock(&ds->wait_queue.lock, descr);
                    }
		    if (!broadcast) break;
		}
		cq = cq->th_prev;
	    }
	}
	cond->cn_wakecnt++;
	_pth_release(&(cond->cn_lock), descr->tid);
	if (cond->cn_shared.pshared == TRUE) {
	    /* wakeup the waiters */
	    if (broadcast) {
                futex_notify_all(&cond->cn_shared.futex);
	    } else {
                futex_notify(&cond->cn_shared.futex);
	    }
	    if (broadcast && (cond->cn_waiters > 0)) {
		pth_wakeup_anative();
		while (cond->cn_waiters > 0)
		    pth_yield(NULL);
	    }
	} else {
	    /* and give other threads a chance to awake */
	    if (!broadcast) {
		if (descr->is_bounded)
		    pth_wakeup_anative();
		else if (ds != NULL && !ds->is_bound)
		    pth_sc(write)(ds->sigpipe[1], &c, sizeof(char));
	    } else if (cond->cn_waiters > 0) {
	        if (!descr->current->boundnative) {
		    /* wait until all waiters are awake... */
		    ev = pth_event(PTH_EVENT_FUNC|PTH_MODE_STATIC, &ev_key, pth_check_waiters, 
				   (void *)cond, pth_time_zero);
		    pth_wait(ev);
		} else {
		    pth_wakeup_anative();
		    while (cond->cn_waiters > 0)
			pth_yield(NULL);
		}
	    }
	}
    } else {
        _pth_release(&(cond->cn_lock), descr->tid);
    }

    /* return to caller */
    return TRUE;
}

/*
**  Barriers
*/

int pth_barrier_init(pth_barrier_t *barrier, int threshold)
{
    if (barrier == NULL || threshold <= 0)
        return FALSE;
    if (!pth_mutex_init(&(barrier->br_mutex), NULL))
        return FALSE;
    if (!pth_cond_init(&(barrier->br_cond)))
        return FALSE;
    barrier->br_state     = PTH_BARRIER_INITIALIZED;
    barrier->br_threshold = threshold;
    barrier->br_count     = threshold;
    barrier->br_cycle     = FALSE;
    return TRUE;
}

int pth_barrier_reach(pth_barrier_t *barrier)
{
    int cancel, cycle;
    int rv;

    if (barrier == NULL)
        return FALSE;
    if (!(barrier->br_state & PTH_BARRIER_INITIALIZED))
        return FALSE;

    if (!pth_mutex_acquire(&(barrier->br_mutex), FALSE, NULL))
        return FALSE;
    cycle = barrier->br_cycle;
    if (--(barrier->br_count) == 0) {
        /* last thread reached the barrier */
        barrier->br_cycle   = !(barrier->br_cycle);
        barrier->br_count   = barrier->br_threshold;
        if ((rv = pth_cond_notify(&(barrier->br_cond), TRUE)))
            rv = PTH_BARRIER_TAILLIGHT;
    }
    else {
        /* wait until remaining threads have reached the barrier, too */
        pth_cancel_state(PTH_CANCEL_DISABLE, &cancel);
        if (barrier->br_threshold == barrier->br_count)
            rv = PTH_BARRIER_HEADLIGHT;
        else
            rv = TRUE;
        while (cycle == barrier->br_cycle) {
            if (!(rv = pth_cond_await(&(barrier->br_cond), &(barrier->br_mutex), NULL)))
                break;
        }
        pth_cancel_state(cancel, NULL);
    }
    pth_mutex_release(&(barrier->br_mutex));
    return rv;
}

