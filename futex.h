
/* -*-c-*- NGPT: Futex operations [global]
**
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Based on work by Matthew Kirkwood <matthew@hairy.beasts.org>.
**  (C) 2002 Intel Corporation by Iñaky Pérez-González
**  <inaky.perez-gonzalez@intel.com> [Reorganization and cleanup for
**  use for inter-native thread spinlocks].
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
**
** Here lies the futex interface [along with sys_futex.c]. This is
** layered out in order to be used by different parts of the NGPT
** implementation, so read on for the structure:
**
** layer 1: arch specific futex code: futex-*.h
** layer 2: low level futex support 
** layer 3: public futex functions 
** 
**
*/
                             /* ``Pardon me for not standing.''
                                 -- Groucho Marx's epitaph */


#ifndef __ngpt_futex_h__
#define __ngpt_futex_h__

#include <errno.h>         /* E* */
#include <limits.h>        /* INT_MAX and friends */
#include <sys/user.h>      /* PAGE_SIZE */
#include <sys/mman.h>      /* PROT_* */
#include <linux/unistd.h>  /* __NR_* */
#include <unistd.h>        /* close() */
#include <string.h>	   /* memset */
#include "debug.h"         /* Debug support for __futex_dump() */
#include "syscall.h"       /* NGPT syscall acceleration */

/* Note: Default futex syscall number comes from syscall.h */

  /* Second argument to futex syscall */

#define FUTEX_WAIT (0)
#define FUTEX_WAKE (1)
#define FUTEX_FD   (2)



  /* Layer 1: ARCH SPECIFIC FUTEX CODE */


#if defined (__i386__)
#include "sysdeps/i386/futex.h"
#elif defined (__ia64__)
#include "sysdeps/ia64/futex.h"
#elif defined (__powerpc__)
#include "sysdeps/powerpc/futex.h"
#elif defined (__s390__)
#include "sysdeps/s390/futex.h"
#else
#error futex.h: Unknown platform, cannot optimize
#endif

  /* Check out we have this - protection for semaphore memory */

#ifndef PROT_SEM
#define PROT_SEM 0x08
#endif

#define FUTEX_PASSED (-(1024 * 1024 * 1024))

#ifndef F_SETSIG
#define F_SETSIG 10
#endif

struct timespec;



    /* Layer 2: LOW LEVEL FUTEX SUPPORT */


/* Futex main system call
**
** returns < 0 ernro code on error.
**
** FIXME: check for trouble.
*/

static __inline__
int sys_futex (int *futex, int op, int val, struct timespec *rel)
{
    return ngpt_SYSCALL (futex, 4, futex, op, val, rel);
}


/* mprotect system call
**
** returns < 0 errno code on error.
**
** FIXME: check for trouble!!!!
*/

static __inline__
long sys_mprotect (void *start, size_t len, unsigned long prot)
{
    return ngpt_SYSCALL (mprotect, 3, start, len, prot);
}


/* The simple futex structure (word size _always_). */

struct futex_st {
    int count;
};		

#define futex_st_INIT_UNLOCKED { count: 1 }

/* Return the futex count
**
** param futex Pointer to the futex structure
**
** returns Futex's current count
*/

static __inline__
int futex_count (struct futex_st *futex)
{
    return futex->count;
}


/* Ask the kernel to acquire a futex, sleeping in the process
**
** param futex Pointer to the futex structure
**
** 
**
** returns <0 errno code on error, 0 on wakeup, 1 on pass, 2 on didn't
**         sleep
*/

static __inline__
int futex_down_slow (struct futex_st *futex, int val, struct timespec *rel)
{
    int result;
    result = sys_futex (&futex->count, FUTEX_WAIT, val, rel);
    if (result == 0) {
    	/* <= in case someone else decremented it */
    	if (futex->count <= FUTEX_PASSED) {
	    futex->count = -1;
	    return 1;
	}
	return 0;
    }
    /* EWOULDBLOCK just means value changed before we slept: loop */
    if (result == -EWOULDBLOCK)
    	return 2;
    return result;
}


/* Ask the kernel to release a futex, wake up waiter(s)
**
** param futex Pointer to the futex structure
**
** param howmany Number of waiters to wake up; ie: 1 for 1, INT_MAX
**               for all
**
** returns 0 if OK, <0 errno code on error
*/

static __inline__
int futex_up_slow (struct futex_st *futex, int howmany)
{
    futex->count = 1;
    __futex_commit();
    return sys_futex (&futex->count, FUTEX_WAKE, howmany, NULL);
}


/* Acquire a futex with optional timeout
**
** Decrement the count; if it goes from 1 to 0, then we have it [fast
** user land operation]. Else, we will sleep and the kernel will wake
** us up. Hoever, if we set a timeout, we will be woken up if we
** cannot get it before the timer expires.
**
** param futx Pointer to the futex structure.
**
** returns <0 errno code on error, 0 if we got it, FIXME what do we
**      get when we timed out? error?
*/

static __inline__
int futex_down_timeout (struct futex_st *futex, struct timespec *rel)
{
    int val, woken = 0;
    int result;
    
    /* Returns new value */
    while ((val = __futex_down (&futex->count)) != 0) {
    	result = futex_down_slow (futex, val, rel);
        if (result < 0)
            return result;
        else if (result == 1)
            return 0;
        else if (result == 0)
            woken = 1;
    }
    
    /* If we were woken, someone else might be sleeping too: set to -1 */
    if (woken) {
    	futex->count = -1;
    }
    return 0;
}


/* Acquire a futex
**
** Decrement the count; if it goes from 1 to 0, then we have it [fast
** user land operation]. Else, we will sleep and the kernel will wake
** us up.
**
** param futex Pointer to the futex structure.
**
** returns FIXME
*/

static __inline__
int futex_down (struct futex_st *futex)
{
    return futex_down_timeout (futex, NULL);
}


/* Conditionally acquire a futex
**
** If it is not acquired [locked] already, give it to us; else fail.
**
** param futex Pointer to the futex structure.
**
** returns 0 if we acquired the futex; <0 if it is locked and we could
**   not acquire it without locking.
*/

static __inline__
int futex_trydown (struct futex_st *futex)
{
    return __futex_down (&futex->count) == 0 ? 0 : -EWOULDBLOCK;
}


/* Release a futex
**
** Increments the futex count from 0 to 1 if nobody us waiting
** [fast user land operation]. If somebody is waiting, then set the
** count to 1 and tell the kernel to wake them up.
**
** param futex Pointer to the futex structure.
**
** returns FIXME.
*/

static __inline__
int futex_up (struct futex_st *futex, int howmany)
{
    if (!__futex_up (&futex->count))
    	return futex_up_slow (futex, howmany);
    return 0;
}


/* 
** FIXME
**
*/

static __inline__
int futex_up_fair (struct futex_st *futex)
{
    /* Someone waiting? */
    if (!__futex_up (&futex->count)) {
    	futex->count = FUTEX_PASSED;
	__futex_commit();
	/* If we wake one, they'll see it's a direct pass. */
	if (sys_futex (&futex->count, FUTEX_WAKE, 1, NULL) == 1)
		return 0;
	/* Otherwise do normal slow case */
	return futex_up_slow (futex, INT_MAX);
    }
    return 0;
}


/* Asychronous wait on a futex
**
** FIXME: not clear what this does
*/

static __inline__
int futex_await (struct futex_st *futex)
{
    return sys_futex (&futex->count, FUTEX_FD, 0, NULL);
}



    /* Layer 3: PUBLIC FUTEX FUNCTIONS */


/* Initialize a futex
**
** param futex Pointer to the futex structure
*/

static __inline__
void futex_init (struct futex_st *futex)
{
    futex->count = 1;
    __futex_commit();
}


/* Acquire the futex
**
** This is used for asynchronous waiting. Basically, so that a wait
** does not block a kernel thread; instead, whatever entity that needs
** to wait on the futex can poll the returned file descriptor once it
** changes to acquire the futex.
**
** param futex Pointer to the futex structure
**
** param tryonly if !0, fail if it cannot be acquired, block
**               otherwise.
**
** returns
**	0   Futex acquired
**	> 0 waiting for futex, return futex file descriptor
**	<0 errno code  Error occured
*/

static __inline__
int futex_acquire (struct futex_st *futex, int tryonly)
{
    int futex_fd = 0;

    if (futex_trydown (futex) != 0) {
	if (tryonly)
	    return 0;
	futex_fd = futex_await (futex);
	if (futex_fd < 0)
	    return futex_fd;
	if (futex_trydown (futex) == 0) {
	    close (futex_fd);
	    return 0;
	}
	/* ??? - will this happen?? */
	if (futex->count <= FUTEX_PASSED) {
	    futex->count = -1;
	    return 0;
	}
	return futex_fd;
    }
    return 0;
}


/* Release a futex
**
** param futex Pointer to the futex structure
*/

static __inline__
int futex_release (struct futex_st *futex)
{
    return futex_up (futex, INT_MAX);
}


/* Add a waiter for a futex
**
** Wait on a [possibly locked] futex, get a file descriptor to poll
** for activity.
**
** param futex Pointer to the futex structure
**
** returns file descriptor where to poll for activity; 0 if ok, < 0 on
**              error and errno set.
*/

static __inline__
int futex_add_waiter (struct futex_st *futex)
{
    return futex_await (futex);
}


/* Notify first waiter
**
** Wake up the first waiter on the futex
**
** param futex Pointer to the futex structure
*/

static __inline__
void futex_notify(struct futex_st *futex)
{
    futex_up_slow (futex, 1);
}


/* Notify all waiters of a futex to wake up
**
** param futex Pointer to the futex structure
*/

static __inline__
void futex_notify_all (struct futex_st *futex)
{
    futex_up_slow (futex, INT_MAX);
}


/* Destroy a futex
**
** param futex Pointer to the futex structure
*/

static __inline__
void futex_destroy (struct futex_st *futex)
{
    memset (futex, 0, sizeof (*futex));
}


/* Prepare a shared area for use by futexes
**
** YOU ONLY NEED TO USE THIS IF THE FUTEXES ARE GOING TO BE USED BY
** DIFFERENT PROCESSES AND THUS, THE FUTEX WILL BE MAPPED WITH
** DIFFERENT VMA ADDRESSES IN DIFFERENT ADDRESS SPACES.
**
** Thanks Rusty :)
**
** [so this means that clones of a process that share the same address
** space don't need to call this on a futex they are going to use for
** contention].
**
** param area Pointer to the area
**
** param size Size of the area
**
** returns 0 if ok, else ERRNO error code.
*/

static __inline__
int futex_region (void *area, size_t size)
{
    int retval;
    
    /* Align pointer to previous start of page */
    area = (void *)(((unsigned long)area / PAGE_SIZE) * PAGE_SIZE);

    /* Round size up to page size */
    size = ((size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    retval = sys_mprotect(area, size, PROT_READ|PROT_WRITE|PROT_SEM);
    if (retval < 0)
      return retval;
    return 0;
}


/* Dump the internal state of a futex
**
** param futex Pointer to the futex structure.
*/

static __inline__
void __futex_dump (const struct futex_st *futex)
{
    __debugmsg_int (NULL, "{ count: %d }", futex->count);
}

#endif
