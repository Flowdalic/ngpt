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
**  pth_attr.c: Pth thread attributes
*/
                             /* ``Unix -- where you can do anything
                                  in two keystrokes, or less...'' 
                                                     -- Unknown  */
#include "pth_p.h"
#include "allocation.h"
#include "schedule.h"

#if cpp

enum {
    PTH_ATTR_GET,
    PTH_ATTR_SET
};

struct pth_attr_st {
    pth_t        a_tid;
    int          a_prio; /* External priority */
    int          a_schedpolicy;
    int          a_inheritsched;
    char         a_name[PTH_TCB_NAMELEN];
    int          a_joinable;
    unsigned int a_cancelstate;
    unsigned int a_stacksize;
    unsigned int a_suspendstate;
    char        *a_stackaddr;
    int          a_scope;
};

#endif /* cpp */

pth_attr_t pth_attr_of(pth_t t)
{
    pth_attr_t a;

    if (t == NULL)
        return NULL;
    if ((a = (pth_attr_t)pth_malloc(sizeof(struct pth_attr_st))) == NULL)
        return NULL;
    a->a_tid = t;
    return a;
}

pth_attr_t pth_attr_new(void)
{
    pth_attr_t a;

    if ((a = (pth_attr_t)pth_malloc(sizeof(struct pth_attr_st))) == NULL)
        return NULL;
    a->a_tid = NULL;
    pth_attr_init(a);
    return a;
}

int pth_attr_destroy(pth_attr_t a)
{
    if (a == NULL)
        return FALSE;
    pth_free_mem (a, sizeof (struct pth_attr_st));
    return TRUE;
}

int pth_attr_init(pth_attr_t a)
{
    if (a == NULL)
        return FALSE;
    if (a->a_tid != NULL)
        return FALSE;
    a->a_prio = xprio_get_default(SCHED_OTHER);
    a->a_schedpolicy = SCHED_OTHER;
    pth_util_cpystrn(a->a_name, "unknown", PTH_TCB_NAMELEN);
    a->a_joinable = TRUE;
    a->a_cancelstate = PTH_CANCEL_DEFAULT;
    a->a_stacksize = pth_default_stacksize*1024;
    a->a_stackaddr = NULL;
    a->a_scope = PTH_SCOPE_PROCESS;  /* default is M:N */
    return TRUE;
}

int pth_attr_get(pth_attr_t a, int op, ...)
{
    va_list ap;
    int rc;

    va_start(ap, op);
    rc = pth_attr_ctrl(PTH_ATTR_GET, a, op, ap);
    va_end(ap);
    return rc;
}

int pth_attr_set(pth_attr_t a, int op, ...)
{
    va_list ap;
    int rc;

    va_start(ap, op);
    rc = pth_attr_ctrl(PTH_ATTR_SET, a, op, ap);
    va_end(ap);
    return rc;
}

intern int pth_attr_ctrl(int cmd, pth_attr_t a, int op, va_list ap)
{
    if (a == NULL)
        return FALSE;
    switch (op) {
        case PTH_ATTR_PRIO: {
            /* priority */
            int val;
            if (cmd == PTH_ATTR_SET) {
                val = va_arg(ap, int);
                if (a->a_tid != NULL)
                  thread_prio_set (a->a_tid, val, NULL);
                else
                  a->a_prio = val;
            }
            else {
              int *dst, src;
                dst = va_arg(ap, int *);                
                src = a->a_tid != NULL?
                  thread_prio_get(a->a_tid, NULL) : a->a_prio;
                *dst = src;
            }
            break;
        }
        case PTH_ATTR_SCHEDPOLICY: {
            /* scheduler policy */
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid)
                  a->a_tid->schedpolicy = va_arg(ap, int);
                else
                  a->a_schedpolicy = va_arg(ap, int);
            }
            else {
                int *p = va_arg(ap, int *);
                if (a->a_tid)
                  *p = a->a_tid->schedpolicy;
                else
                  *p = a->a_schedpolicy;
            }
            break;
        }
        case PTH_ATTR_INHERITSCHED: {
            /* inherit scheduler policy/params */
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid)
                  a->a_tid->inheritsched = va_arg(ap, int);
                else
                  a->a_inheritsched = va_arg(ap, int);
            }
            else {
                int *p = va_arg(ap, int *);
                if (a->a_tid)
                  *p = a->a_tid->inheritsched;
                else
                  *p = a->a_inheritsched;
            }
            break;
        }
        case PTH_ATTR_NAME: {
            /* name */
            if (cmd == PTH_ATTR_SET) {
                char *src, *dst;
                src = va_arg(ap, char *);
                dst = (a->a_tid != NULL ? a->a_tid->name : a->a_name);
                pth_util_cpystrn(dst, src, PTH_TCB_NAMELEN);
            }
            else {
                char *src, **dst;
                src = (a->a_tid != NULL ? a->a_tid->name : a->a_name);
                dst = va_arg(ap, char **);
                *dst = src;
            }
            break;
        }
        case PTH_ATTR_JOINABLE: {
            /* detachment type */
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = (a->a_tid != NULL ? &a->a_tid->joinable : &a->a_joinable);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->joinable : &a->a_joinable);
                dst = va_arg(ap, int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_CANCEL_STATE: {
            /* cancellation state */
            unsigned int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, unsigned int);
                dst = (a->a_tid != NULL ? &a->a_tid->cancelstate : &a->a_cancelstate);
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->cancelstate : &a->a_cancelstate);
                dst = va_arg(ap, unsigned int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_STACK_SIZE: {
            /* stack size */
            unsigned int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid != NULL)
                    return FALSE;
                src = &val; val = va_arg(ap, unsigned int);
                dst = &a->a_stacksize;
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->stacksize : &a->a_stacksize);
                dst = va_arg(ap, unsigned int *);
            }
            *dst = *src;
            break;
        }
        case PTH_ATTR_STACK_ADDR: {
            /* stack address */
            char *val, **src, **dst;
            if (cmd == PTH_ATTR_SET) {
                if (a->a_tid != NULL)
                    return FALSE;
                src = &val; val = va_arg(ap, char *);
                dst = &a->a_stackaddr;
            }
            else {
                src = (a->a_tid != NULL ? &a->a_tid->stack : &a->a_stackaddr);
                dst = va_arg(ap, char **);
            }
            *dst = *src;
            break;
        }
	/*start ibm@new*/
	case PTH_ATTR_CREATE_SUSPENDED: {
            /* create suspended */
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = &a->a_suspendstate;
            }
            else {
                src = &a->a_suspendstate;
                dst = va_arg(ap, int *);
            }
            *dst = *src;
            break;
        }
	/*end ibm@new*/
        case PTH_ATTR_TIME_SPAWN: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            dst = va_arg(ap, pth_time_t *);
            *dst = (a->a_tid != NULL) ? a->a_tid->lastran : pth_time_zero;
            break;
        }
        case PTH_ATTR_TIME_LAST: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            dst = va_arg(ap, pth_time_t *);
            *dst = (a->a_tid != NULL) ? a->a_tid->lastran : pth_time_zero;
            break;
        }
        case PTH_ATTR_TIME_RAN: {
            pth_time_t *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            dst = va_arg(ap, pth_time_t *);
            *dst = (a->a_tid != NULL) ? a->a_tid->lastran : pth_time_zero;
            break;
        }
        case PTH_ATTR_START_FUNC: {
            void *(**dst)(void *);
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            if (a->a_tid == NULL)
                return FALSE;
            dst = (void *(**)(void *))va_arg(ap, void *);
            *dst = a->a_tid->start_func;
            break;
        }
        case PTH_ATTR_START_ARG: {
            void **dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            if (a->a_tid == NULL)
                return FALSE;
            dst = va_arg(ap, void **);
            *dst = a->a_tid->start_arg;
            break;
        }
        case PTH_ATTR_STATE: {
            pth_state_t *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            if (a->a_tid == NULL)
                return FALSE;
            dst = va_arg(ap, pth_state_t *);
            *dst = a->a_tid->state;
            break;
        }
        case PTH_ATTR_EVENTS: {
            pth_event_t *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            if (a->a_tid == NULL)
                return FALSE;
            dst = va_arg(ap, pth_event_t *);
            *dst = a->a_tid->events;
            break;
        }
        case PTH_ATTR_BOUND: {
            int *dst;
            if (cmd == PTH_ATTR_SET)
                return FALSE;
            dst = va_arg(ap, int *);
            *dst = (a->a_tid != NULL ? TRUE : FALSE);
            break;
        }
        case PTH_ATTR_SCOPE: {
            int val, *src, *dst;
            if (cmd == PTH_ATTR_SET) {
                src = &val; val = va_arg(ap, int);
                dst = &a->a_scope;
            }
            else {
                src = &a->a_scope;
                dst = va_arg(ap, int *);
            }
            *dst = *src;
	    break;
	}
        default:
            return FALSE;
    }
    return TRUE;
}

