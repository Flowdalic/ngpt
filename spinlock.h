
  /* -*-c-*- NGPT: Linked lists
  ** 
  ** $Id: spinlock.h,v 1.7 2002/11/20 17:14:48 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** THESE ARE BUSY LOCKS FOR LOCKING BETWEEN NATIVE THREADS!!!
  **
  ** Implemented following as a great detailed reference the
  ** linux/include/list.h file. Thus the likeliness of the code [not
  ** to say identicalness]. 
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **
  **      [- THINGS DONE BY AUTHOR]
  */

#ifndef __ngpt_lock_h__
#define __ngpt_lock_h__

#include "bitops.h"
#include "debug.h"
#include "pth.h"
#include "futex.h"
#include <errno.h>

  /* Operational switches */

#define SPINLOCK_SUPPORT_LOCKER   /* Support lockers/unlockers */
#define SPINLOCK_CHECK_LOCKER 0   /* 1/0 Check/don't lockers */
#define SPINLOCK_NON_FUTEX 1      /* 1/0 futex based spinlock */

  /* An spinlock */

typedef struct {
#if SPINLOCK_NON_FUTEX
    int lock;
#else
    struct futex_st futex;

#ifdef SPINLOCK_SUPPORT_LOCKER
    volatile const void *locker;
#endif
#endif
} spinlock_t;

#define spinlock_NOLOCKER ((void *)-1)

  /* Initialize an spin lock */

static __inline__
void spinlock_init (spinlock_t *spinlock)
{
#if SPINLOCK_NON_FUTEX
    spinlock->lock = 0;
#else
    futex_init (&spinlock->futex);
#ifdef SPINLOCK_SUPPORT_LOCKER
    spinlock->locker = spinlock_NOLOCKER;
#endif
#endif
}

#ifndef SPINLOCK_SUPPORT_LOCKER
#if SPINLOCK_NON_FUTEX
#define spinlock_t_INIT_UNLOCKED { lock: 0 }
#else
#define spinlock_t_INIT_UNLOCKED { futex: futex_st_INIT_UNLOCKED }
#endif
#else
#if SPINLOCK_NON_FUTEX
#define spinlock_t_INIT_UNLOCKED { lock: 0 }
#else
#define spinlock_t_INIT_UNLOCKED { futex: futex_st_INIT_UNLOCKED, locker: spinlock_NOLOCKER }
#endif
#endif


  /* Check if an spin lock is already locked */

static __inline__
void spinlock_check_not_owned (spinlock_t *spinlock, const void *locker)
{
#ifndef SPINLOCK_SUPPORT_LOCKER
    return;
#else
#ifndef SPINLOCK_NON_FUTEX
    if (SPINLOCK_CHECK_LOCKER && spinlock->locker == locker) {
        __debugmsg ("%s (%p, %p): BUG: locker %p wants to recursively "
                    "lock spinlock %p\n",
                    __FUNCTION__, spinlock, locker, locker, spinlock);
        stack_dump();
    }
#endif
#endif
}

    /* Define to greater than zero if you want the spinlocks to spin a
    ** wee bit before locking [might improve contention]. */

#define SPINLOCK_MAX_LOOPS 100

#define MAX_SPIN_COUNT 500
#define SPIN_SLEEP_DURATION 5000001

  /* Lock an spinlock
  **
  **
  ** param timeout Timeout for the spinlock. NULL for infinite waiting.
  **
  ** return <0 errno code on timeout/error, 0 if ok.
  */

static __inline__
int spin_lock (spinlock_t *spinlock, const void *locker, struct timespec *timeout)
{

#if SPINLOCK_NON_FUTEX
    int cnt = 0;
    struct timespec tm;
	 
    while (test_and_set(&(spinlock->lock))) {
 	if (cnt < MAX_SPIN_COUNT) {
	    cnt++;
	} else {
	    tm.tv_sec = 0;
	    tm.tv_nsec = SPIN_SLEEP_DURATION;
	    syscall(__NR_nanosleep, &tm, NULL);
	    cnt = 0;
	}
    }
    return 0;
#else
    int result;
    //#warning FIXME: preemption disable
    spinlock_check_not_owned (spinlock, locker);

    if (SPINLOCK_MAX_LOOPS > 0) {
        int count = SPINLOCK_MAX_LOOPS;
        while ((result = futex_trydown (&spinlock->futex))) {
            if (count-- == 0)
                goto lock;
        }
    }
    else {
      lock:
        result = futex_down_timeout (&spinlock->futex, timeout);
        if (result < 0)
            __debugmsg ("spinlock %p futex error %d\n", spinlock, result);
    }
#ifdef SPINLOCK_SUPPORT_LOCKER
    spinlock->locker = locker;
#endif
    return result;

#endif
                        
}


  /* Try to lock an spinlock
  **
  ** return 0 if ok, !0 if locked
  */

static __inline__
int spin_trylock (spinlock_t *spinlock, const void *locker)
{
    int retval = 0;

#if SPINLOCK_NON_FUTEX
    while (test_and_set(&(spinlock->lock)))
	return 1;
#else
    retval = futex_trydown (&spinlock->futex);

#ifdef SPINLOCK_SUPPORT_LOCKER
    if (retval == 0) {
        spinlock->locker = locker;
    }
#endif
#endif
    return retval;
}


  /* Unlock an spinlock */

static __inline__
void spin_unlock (spinlock_t *spinlock, const void *locker)
{
#if SPINLOCK_NON_FUTEX
    spinlock->lock = 0;
#else
    futex_up (&spinlock->futex, 1);
    //#warning FIXME: preemption enable
#ifdef SPINLOCK_SUPPORT_LOCKER
    if (SPINLOCK_CHECK_LOCKER
        && spinlock->locker != locker) {
        __debugmsg ("%s (%p, %p): BUG: spinlock (futex: %p",
                    __FUNCTION__, spinlock, locker, &spinlock->futex);
        __futex_dump (&spinlock->futex);
        __debugmsg_int (NULL, ") owned by %p unlocked by different locker!",
                        spinlock->locker);
        stack_dump();
        __debugmsg_int (NULL, "\n");
    }
    spinlock->locker = spinlock_NOLOCKER;
#endif

#endif
}


  /* Who is the locker of the spinlock? */

static __inline__
const void * spinlock_locker (spinlock_t *spinlock)
{
#ifndef SPINLOCK_NON_FUTEX
#ifdef SPINLOCK_SUPPORT_LOCKER
    return (const void *) spinlock->locker;
#else
    return spinlock_NOLOCKER;
#endif
#else
    return spinlock_NOLOCKER;
#endif
}


  /* Who is the locker of the spinlock? */

static __inline__
void spinlock_dump (const spinlock_t *spinlock)
{
#ifndef SPINLOCK_NON_FUTEX
    __debugmsg_int (NULL, "{ futex: ");
    __futex_dump (&spinlock->futex);
#ifdef SPINLOCK_SUPPORT_LOCKER
    __debugmsg_int (NULL, ", locker: %p }", spinlock->locker);
#else
    __debugmsg_int (NULL, "}");
#endif    
#endif
}


#endif /* __ngpt_lock_h__ */


