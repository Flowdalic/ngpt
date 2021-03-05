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
**  sigrt.c: realtime signal handling.
*/
                             /* ``Free Software: generous programmers
                                  from around the world all join
                                  forces to help you shoot yourself
                                  in the foot for free.''
                                                 -- Unknown         */
#include "pth_p.h"

/* 
 * Paired down rtsig API support, should add stuff like POSIX signals
 * for NGPT interthread communication later.
 */
static int current_rtmin;
static int current_rtmax;

static int rtsigs_initialized;

static int kernel_has_rtsig(void)
{
#if defined(__SIGRTMIN) && __SIGRTMIN != -1
    return 1;
#else
    return 0;
#endif
}

#ifdef THREAD_DB
#define INTERNAL_SIGS_NUM	5
#else
#define INTERNAL_SIGS_NUM	2
#endif

/*
 * Initialize the realtime signals.
 */
static void do_rtsigs_init(void)
{
    if (kernel_has_rtsig()) {
	current_rtmin = __SIGRTMIN + INTERNAL_SIGS_NUM + 1;
	current_rtmax = __SIGRTMAX;
    }
    rtsigs_initialized = 1;
}

/* 
 * Return highest priority signal number of available real-time signals
 */
extern int __libc_current_sigrtmin (void) __THROW;
int __libc_current_sigrtmin (void)
{
#ifdef __SIGRTMIN
    if (!rtsigs_initialized)
	do_rtsigs_init();
    return current_rtmin;
#else
    return -1;
#endif
}

/* 
 * Return lowest priority signal number of available real-time signals
 */
extern int __libc_current_sigrtmax (void) __THROW;
int __libc_current_sigrtmax (void)
{
#ifdef __SIGRTMIN
    if (!rtsigs_initialized)
	do_rtsigs_init();
    return current_rtmax;
#else
    return -1;
#endif
}

/* 
 * Allocate real-time signal with highest/lowest available
 * priority.  Please note that we don't use a lock since we assume
 * this function to be called at program start.  
 */
extern int __libc_allocate_rtsig (int high) __THROW;
int __libc_allocate_rtsig (int high)
{
#ifdef __SIGRTMIN
    if (!rtsigs_initialized)
	do_rtsigs_init();
    if (current_rtmin != -1 && current_rtmin <= current_rtmax)
	return high ? current_rtmin++ : current_rtmax--;
#endif
    return -1;
}
