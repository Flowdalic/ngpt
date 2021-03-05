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
**  pth_event.c: Pth event handling
*/
                             /* ``Those of you who think they
                                  know everything are very annoying
                                  to those of us who do.''
                                                  -- Unknown       */
#include "pth_p.h"
#include "allocation.h"

#if cpp

/* event structure */
struct pth_event_st {
    struct pth_event_st *ev_next;
    struct pth_event_st *ev_prev;
    int ev_occurred;
    int ev_type;
    int ev_goal;
    int ev_flags;
    union {
        struct { int fd; }                                        FD;
        struct { int *n; int nfd; fd_set *rfds, *wfds, *efds; }   SELECT;
        struct { sigset_t *sigs; int *sig; }                      SIGS;
        struct { pth_time_t tv; }                                 TIME;
        struct { pth_msgport_t mp; }                              MSG;
        struct { pth_mutex_t *mutex; }                            MUTEX;
        struct { pth_cond_t *cond; }                              COND;
        struct { pth_t tid; }                                     TID;
        struct { int (*func)(void *); void *arg; pth_time_t tv; } FUNC;
    } ev_args;
};

#endif /* cpp */

static struct pth_keytab_st pth_event_keys[PTH_EVENT_KEY_MAX];

static int __pth_event_key_create(pth_key_t *key, void (*func)(void *))
{
    pth_key_t k;
    for (k = 0; k < PTH_EVENT_KEY_MAX; k++) {
	if (pth_event_keys[k].used == FALSE) {
	    pth_event_keys[k].used = TRUE;
	    pth_event_keys[k].destructor = func;
	    *key = k;
	    return TRUE;
	}
    }
    return FALSE;
}

static inline void *pth_event_key_getdata(pth_key_t key)
{
    pth_t current = pth_get_current();
	     
    if (key >= PTH_EVENT_KEY_MAX)
	return NULL;
    if (!pth_event_keys[key].used)
	return NULL;
    if (current->event_data == NULL)
	return NULL;
    return (void *)current->event_data[key];
}

static int pth_event_key_setdata(pth_key_t key, const void *value)
{
    pth_t current = pth_get_current();
	     
    if (key >= PTH_EVENT_KEY_MAX)
	return EINVAL;
    if (!pth_event_keys[key].used)
	return EINVAL;
    if (current->event_data == NULL) {
	current->event_data = (const void **)pth_malloc(sizeof(void *)*PTH_EVENT_KEY_MAX);
	if (current->event_data == NULL)
	    return ENOMEM;
    }
    if (current->event_data[key] == NULL) {
	if (value != NULL)
	    current->event_count++;
    } else {
	if (value == NULL)
	    current->event_count--;
    }
    current->event_data[key] = value;
    return 0;
}

intern void pth_event_key_destroy(pth_t t)
{
    void *data;
    int key;
    void (*destructor)(void *);

    if (t->event_count > 0)
	for (key = 0; key < PTH_EVENT_KEY_MAX; key++) {
	    if (pth_event_keys[key].used && 
		(data = (void *)t->event_data[key]) != NULL) {
		t->event_data[key] = NULL;
		t->event_count--;
		if ((destructor = pth_event_keys[key].destructor) != NULL)
		    destructor(data);
		if (t->event_count == 0)
		    break;
	    }
	}
    pth_free_mem(t->event_data, sizeof(void *)*PTH_EVENT_KEY_MAX); 
    t->event_data = NULL;
    return;
}

/* event structure destructor */
static void pth_event_destructor(void *vp)
{
    /* free this single(!) event. That it is just a single event is a
       requirement for pth_event(PTH_MODE_STATIC, ...), or else we would
       get into horrible trouble on asychronous cleanups */
    pth_debug2("pth_event_destructor: freeing event 0x%lx", vp);
    pth_event_free((pth_event_t)vp, PTH_FREE_THIS);
    return;
}

#define EVTLOCKING_DEBUG 1

/* event structure constructor */
pth_event_t pth_event(unsigned long spec, ...)
{
    pth_event_t ev;
    pth_key_t *ev_key;
    va_list ap;

    va_start(ap, spec);
    
    /* allocate new or reuse static or supplied event structure */
    if (spec & PTH_MODE_STATIC) {
	int rc;
	pid_t ptid = current_tid();
        /* reuse static event structure */
	_pth_acquire_lock(&pth_key_lock, ptid); 
        ev_key = va_arg(ap, pth_key_t *);
        if (*ev_key == PTH_KEY_INIT) {
            rc = __pth_event_key_create(ev_key, pth_event_destructor);
	    if (rc == FALSE) {
		fprintf(stderr, "Number of event keys exceeded its limit\n");
		_pth_release_lock(&pth_key_lock, ptid);
		goto event_malloc;
	    }
        }
        ev = (pth_event_t)pth_event_key_getdata(*ev_key);
        if (ev == NULL) {
            ev = (pth_event_t)pth_malloc(sizeof(struct pth_event_st));
	    pth_debug2("pth_event: new event allocated 0x%lx", ev);
            pth_event_key_setdata(*ev_key, ev);
        }
        else {
            pth_debug2("pth_event: found 0x%lx in keydata.", ev);
	}
	_pth_release_lock(&pth_key_lock, ptid);
    }
    else if (spec & PTH_MODE_REUSE) {
        /* reuse supplied event structure */
        ev = va_arg(ap, pth_event_t);
    }
    else {
event_malloc:
        /* allocate new dynamic event structure */
        ev = (pth_event_t)pth_malloc(sizeof(struct pth_event_st));
	pth_debug2("pth_event: new event allocated 0x%lx", ev);
    }
    if (unlikely (ev == NULL)) {
	pth_yield(NULL);	/* memory allocation failed, yield, so exit can complete */
        va_end(ap);		/* Clean up and exit with NULL */
	return NULL;
    }

    /* create new event ring out of event or insert into existing ring */
    if (spec & PTH_MODE_CHAIN) {
        pth_event_t ch = va_arg(ap, pth_event_t);
        ev->ev_prev = ch->ev_prev;
        ev->ev_next = ch;
        ev->ev_prev->ev_next = ev;
        ev->ev_next->ev_prev = ev;
    }
    else {
        ev->ev_prev = ev;
        ev->ev_next = ev;
    }

    /* initialize common ingredients */
    ev->ev_flags = FALSE;
    ev->ev_occurred = FALSE;

    /* initialize event specific ingredients */
    if (spec & PTH_EVENT_MUTEX) {
        /* mutual exclusion lock */
        pth_mutex_t *mutex = va_arg(ap, pth_mutex_t *);
        ev->ev_type = PTH_EVENT_MUTEX;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.MUTEX.mutex = mutex;
    }
    else if (spec & PTH_EVENT_FD) {
        /* filedescriptor event */
        int fd = va_arg(ap, int);
        ev->ev_type = PTH_EVENT_FD;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_FD_READABLE|\
                                    PTH_UNTIL_FD_WRITEABLE|\
                                    PTH_UNTIL_FD_EXCEPTION));
        ev->ev_args.FD.fd = fd;
    }
    else if (spec & PTH_EVENT_SELECT) {
        /* filedescriptor set select event */
        int *n = va_arg(ap, int *);
        int nfd = va_arg(ap, int);
        fd_set *rfds = va_arg(ap, fd_set *);
        fd_set *wfds = va_arg(ap, fd_set *);
        fd_set *efds = va_arg(ap, fd_set *);
        ev->ev_type = PTH_EVENT_SELECT;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.SELECT.n    = n;
        ev->ev_args.SELECT.nfd  = nfd;
        ev->ev_args.SELECT.rfds = rfds;
        ev->ev_args.SELECT.wfds = wfds;
        ev->ev_args.SELECT.efds = efds;
    }
    else if ((spec & PTH_EVENT_SIGS) || (spec & PTH_EVENT_TIME_SIGS)) {
        /* signal set event */
        sigset_t *sigs = va_arg(ap, sigset_t *);
        int *sig = va_arg(ap, int *);
        int flag = va_arg(ap, int);
        ev->ev_type = PTH_EVENT_SIGS;
        if (spec & PTH_EVENT_TIME_SIGS)
            ev->ev_type = PTH_EVENT_TIME_SIGS;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.SIGS.sigs = sigs;
        ev->ev_args.SIGS.sig = sig;
        ev->ev_flags = flag;
    }
    else if (spec & PTH_EVENT_TIME) {
        /* interrupt request event */
        pth_time_t tv = va_arg(ap, pth_time_t);
        ev->ev_type = PTH_EVENT_TIME;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.TIME.tv = tv;
    }
    else if (spec & PTH_EVENT_MSG) {
        /* message port event */
        pth_msgport_t mp = va_arg(ap, pth_msgport_t);
        ev->ev_type = PTH_EVENT_MSG;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.MSG.mp = mp;
    }
    else if (spec & PTH_EVENT_COND) {
        /* condition variable */
        pth_cond_t *cond = va_arg(ap, pth_cond_t *);
        ev->ev_type = PTH_EVENT_COND;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.COND.cond = cond;
    }
    else if (spec & PTH_EVENT_TID) {
        /* thread id event */
        pth_t tid = va_arg(ap, pth_t);
        int goal;
        ev->ev_type = PTH_EVENT_TID;
        if (spec & PTH_UNTIL_TID_NEW)
            goal = PTH_STATE_NEW;
        else if (spec & PTH_UNTIL_TID_READY)
            goal = PTH_STATE_READY;
        else if (spec & PTH_UNTIL_TID_WAITING)
            goal = PTH_STATE_WAITING;
        else if (spec & PTH_UNTIL_TID_DEAD)
            goal = PTH_STATE_DEAD;
        else
            goal = PTH_STATE_READY;
        ev->ev_goal = goal;
        ev->ev_args.TID.tid = tid;
    }
    else if (spec & PTH_EVENT_FUNC) {
        /* custom function event */
        ev->ev_type = PTH_EVENT_FUNC;
        ev->ev_goal = (int)(spec & (PTH_UNTIL_OCCURRED));
        ev->ev_args.FUNC.func  = (int (*)(void *))va_arg(ap, void *);
        ev->ev_args.FUNC.arg   = va_arg(ap, void *);
        ev->ev_args.FUNC.tv    = va_arg(ap, pth_time_t);
    }
    else {
	va_end(ap);
	return NULL;
    }

    va_end(ap);

    /* return event */
    return ev;
}

/* determine type of event */
unsigned long pth_event_typeof(pth_event_t ev)
{
    if (ev == NULL)
        return FALSE;
    return (ev->ev_type | ev->ev_goal);
}

/* event extractor */
int pth_event_extract(pth_event_t ev, ...)
{
    va_list ap;

    if (ev == NULL)
        return FALSE;
    va_start(ap, ev);

    /* extract event specific ingredients */
    if (ev->ev_type & PTH_EVENT_FD) {
        /* filedescriptor event */
        int *fd = va_arg(ap, int *);
        *fd = ev->ev_args.FD.fd;
    }
    else if (ev->ev_type & PTH_EVENT_SIGS) {
        /* signal set event */
        sigset_t **sigs = va_arg(ap, sigset_t **);
        int **sig = va_arg(ap, int **);
        *sigs = ev->ev_args.SIGS.sigs;
        *sig = ev->ev_args.SIGS.sig;
    }
    else if (ev->ev_type & PTH_EVENT_TIME) {
        /* interrupt request event */
        pth_time_t *tv = va_arg(ap, pth_time_t *);
        *tv = ev->ev_args.TIME.tv;
    }
    else if (ev->ev_type & PTH_EVENT_MSG) {
        /* message port event */
        pth_msgport_t *mp = va_arg(ap, pth_msgport_t *);
        *mp = ev->ev_args.MSG.mp;
    }
    else if (ev->ev_type & PTH_EVENT_MUTEX) {
        /* mutual exclusion lock */
        pth_mutex_t **mutex = va_arg(ap, pth_mutex_t **);
        *mutex = ev->ev_args.MUTEX.mutex;
    }
    else if (ev->ev_type & PTH_EVENT_COND) {
        /* condition variable */
        pth_cond_t **cond = va_arg(ap, pth_cond_t **);
        *cond = ev->ev_args.COND.cond;
    }
    else if (ev->ev_type & PTH_EVENT_TID) {
        /* thread id event */
        pth_t *tid = va_arg(ap, pth_t *);
        *tid = ev->ev_args.TID.tid;
    }
    else if (ev->ev_type & PTH_EVENT_FUNC) {
        /* custom function event */
        void **func    = va_arg(ap, void **);
        void **arg     = va_arg(ap, void **);
        pth_time_t *tv = va_arg(ap, pth_time_t *);
        *func = (int (**)(void *))ev->ev_args.FUNC.func;
        *arg  = ev->ev_args.FUNC.arg;
        *tv   = ev->ev_args.FUNC.tv;
    }
    else
        return FALSE;
    va_end(ap);
    return TRUE;
}

/* concatenate one or more events or event rings */
pth_event_t pth_event_concat(pth_event_t evf, ...)
{
    pth_event_t evc; /* current event */
    pth_event_t evn; /* next event */
    pth_event_t evl; /* last event */
    pth_event_t evt; /* temporary event */
    va_list ap;

    if (evf == NULL)
        return NULL;

    /* open ring */
    va_start(ap, evf);
    evc = evf;
    evl = evc->ev_next;

    /* attach additional rings */
    while ((evn = va_arg(ap, pth_event_t)) != NULL) {
        evc->ev_next = evn;
        evt = evn->ev_prev;
        evn->ev_prev = evc;
        evc = evt;
    }

    /* close ring */
    evc->ev_next = evl;
    evl->ev_prev = evc;
    va_end(ap);

    return evf;
}

/* isolate one event from a possible appended event ring */
pth_event_t pth_event_isolate(pth_event_t ev)
{
    pth_event_t ring;

    if (ev == NULL)
        return NULL;
    ring = NULL;
    if (!(ev->ev_next == ev && ev->ev_prev == ev)) {
        ring = ev->ev_next;
        ev->ev_prev->ev_next = ev->ev_next;
        ev->ev_next->ev_prev = ev->ev_prev;
        ev->ev_prev = ev;
        ev->ev_next = ev;
    }
    return ring;
}

/* determine whether the event is occurred */
#if cpp
#define PTH_EVENT_OCCURRED(ev)	((int)((ev)->ev_occurred))
#endif

int pth_event_occurred(pth_event_t ev)
{
    if (ev == NULL)
        return FALSE;
    return PTH_EVENT_OCCURRED(ev);
}

/* walk to next or previous event in an event ring */
pth_event_t pth_event_walk(pth_event_t ev, unsigned int direction)
{
    if (ev == NULL)
        return NULL;
    do {
        if (direction & PTH_WALK_NEXT)
            ev = ev->ev_next;
        else if (direction & PTH_WALK_PREV)
            ev = ev->ev_prev;
        else
            return NULL;
    } while ((direction & PTH_UNTIL_OCCURRED) && !(ev->ev_occurred));
    return ev;
}

/* deallocate an event structure */
int pth_event_free(pth_event_t ev, int mode)
{
    pth_event_t evc;
    pth_event_t evn;

    if (ev == NULL)
        return FALSE;
    if (mode == PTH_FREE_THIS) {
	pth_debug2("pth_event_free: freeing event 0x%lx", ev);
        ev->ev_prev->ev_next = ev->ev_next;
        ev->ev_next->ev_prev = ev->ev_prev;
        pth_free_mem (ev, sizeof (*ev));
    }
    else if (mode == PTH_FREE_ALL) {
        evc = ev;
        do {
            evn = evc->ev_next;
	    pth_debug2("pth_event_free: (FREE_ALL) freeing event 0x%lx", evc);
	    pth_free_mem (evc, sizeof (*evc));
            evc = evn;
        } while (evc != ev);
    }
    return TRUE;
}

/* wait for one or more events */
int pth_wait(pth_event_t ev_ring)
{
    int occurred;
    pth_event_t ev;
    pth_t current;

    /* at least a waiting ring is required */
    if (unlikely (ev_ring == NULL))
        return -1;
    current = pth_get_current();
    pth_debug3("pth_wait: enter from thread 0x%lx \"%s\"", current, current->name);

    /* mark all events in waiting ring as still not occurred */
    ev = ev_ring;
    do {
        ev->ev_occurred = FALSE;
        pth_debug3("pth_wait: 0x%lx waiting on event 0x%lx", current, (unsigned long)ev);
        ev = ev->ev_next;
    } while (ev != ev_ring);

    /* link event ring to current thread */
    current->events = ev_ring;

    /* move thread into waiting state
       and transfer control to scheduler */
    current->state = PTH_STATE_WAITING;
    pth_yield(NULL);

    /* check for cancellation */
    if (ev_ring->ev_type != PTH_EVENT_MUTEX && ev_ring->ev_type != PTH_EVENT_FUNC &&
	ev_ring->ev_type != PTH_EVENT_COND)
	pth_cancel_point(FALSE);

    /* unlink event ring from current thread */
    current->events = NULL;

    /* count number of actually occurred events */
    ev = ev_ring;
    occurred = 0;
    do {
        occurred++;
        pth_debug3("pth_wait: 0x%lx occurred event 0x%lx", current, (unsigned long)ev);
        ev = ev->ev_next;
    } while (ev != ev_ring);

    /* leave to current thread with number of occurred events */
    pth_debug3("pth_wait: leave to thread 0x%lx \"%s\"", current, current->name);
    return occurred;
}

