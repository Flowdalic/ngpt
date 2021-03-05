/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
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
**  pthreadtypes.h: POSIX Thread ("Pthread") API for NGPT
*/
                             /* ``Only those who attempt the absurd
                                  can achieve the impossible.''
                                               -- Unknown          */
#if !defined _BITS_TYPES_H && !defined _PTHREAD_H
# error "Never include <bits/pthreadtypes.h> directly; use <sys/types.h> instead."
#endif

#ifndef _BITS_PTHREADTYPES_H
#define _BITS_PTHREADTYPES_H	1

#define __need_schedparam
#undef sched_param
#include <bits/sched.h>


struct _pthread_fastlock {
    long int __status;
    int __spinlock;
};

#ifndef _PTHREAD_DESCR_DEFINED
/* Thread descriptors */
typedef struct pthread_st *_pthread_descr;
# define _PTHREAD_DESCR_DEFINED
#endif


#ifdef __USE_XOPEN2K
/* POSIX spinlock data type.  */
typedef volatile int pthread_spinlock_t;

/* POSIX barrier. */
typedef struct {
  struct _pthread_fastlock __ba_lock; /* Lock to guarantee mutual exclusion */
  int __ba_required;                  /* Threads needed for completion */
  int __ba_present;                   /* Threads waiting */
  _pthread_descr __ba_waiting;        /* Queue of waiting threads */
} pthread_barrier_t;

/* barrier attribute */
typedef struct {
  int __pshared;
} pthread_barrierattr_t;

#endif

/*
 * Unprotect namespace, so we can define our own variants now
 */
#undef pthread_t
#undef pthread_attr_t
#undef pthread_key_t
#undef pthread_once_t
#undef pthread_mutex_t
#undef pthread_mutexattr_t
#undef pthread_cond_t
#undef pthread_condattr_t
#undef pthread_rwlock_t
#undef pthread_rwlockattr_t
 
/*
 * Forward structure definitions.
 * These are mostly opaque to the application.
 */
struct pthread_st;
struct pthread_attr_st;
struct pthread_cond_st {
    void *  res0;
    int	    res1;
    int	    res2;
};
struct pthread_mutex_st {
    void *  res0;
    int	    res1;
    int	    res2;
    int	    res3;
    int	    res4;
    int	    res5;
};
struct pthread_mutexattr_st;
struct pthread_rwlock_st {
    int            res0;
    unsigned int   res1;
    unsigned long  res2;
    void           *res3;
    void	   *res4;
    void           *res5;
    int            res6;
    int            res7;
};
struct pthread_rwlockattr_st {
    int	    res0;
    int	    res1;
};

/*
 * Primitive system data type definitions required by P1003.1c
 */
typedef struct  pthread_st              *pthread_t;
typedef struct  pthread_attr_st         *pthread_attr_t;
typedef int                              pthread_key_t;
typedef int                              pthread_once_t;
typedef struct  pthread_mutexattr_st	*pthread_mutexattr_t;
typedef struct  pthread_mutex_st         pthread_mutex_t;
typedef int                              pthread_condattr_t;
typedef struct  pthread_cond_st          pthread_cond_t;
typedef struct  pthread_rwlockattr_st    pthread_rwlockattr_t;
typedef struct  pthread_rwlock_st        pthread_rwlock_t;

#endif	/* bits/pthreadtypes.h */
