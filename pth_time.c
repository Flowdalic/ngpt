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
**  pth_time.c: Pth time calculations
*/
                             /* ``Real programmers confuse
                                  Christmas and Halloween
                                  because DEC 25 = OCT 31.''
                                             -- Unknown     */

/* 
 * Implementation notes:
 * - time is kept internally as a pth_time_t, which now is a 'long long'
 *   typed object. This makes time calculations much faster than the old
 *   implementation where it was based on 'struct timeval' time objects.
 * - On the edges we have time objects coming in as 'struct timeval', 
 *   and so we have to do conversions to and from the internal type.
 * - When we have HP_TIMING_AVAIL, the counter returned is the HP_TIMING
 *   tsc register value. Otherwise it's the number of microseconds.
 * - Most all operations become simple math calculations using this, and
 *   so are implemented as macros that go into pth_p.h.
 * - For HP_TIMING, assumes:
 *	- All processors same clock speed
 *	- Speed doesn't change during the lifetime of process
 *	- All processors' timers must remain close to in sync
 *	- All TOD based time values need to use pth_TOD_time for 
 *	  converting secs & usecs values to internal pth_time_t
 *	  objects. For offset/delta times PTH_TIME will suffice.
 */

#include "pth_p.h"

#if cpp
#define PTH_TIME_NOW		(pth_time_t *)(0)
#define PTH_TIME_ZERO		&pth_time_zero
#if HP_TIMING_AVAIL
#define PTH_TIME(sec,usec)	 ((pth_time_t)((sec)*__tsc_counts_sec + \
					       (usec)*__tsc_counts_usec))
#else
#define PTH_TIME(sec, usec)	 ((pth_time_t)((sec)*1000000ull + (usec)))
#endif
#endif /* cpp */

/* a global variable holding a zero time */
intern pth_time_t pth_time_zero =  0LL;

/* sleep for a specified amount of microseconds */
intern void pth_time_usleep(unsigned long usec)
{
#ifdef HAVE_USLEEP
    usleep((unsigned int )usec);
#else
    struct timeval timeout;
    timeout.tv_sec  = usec / 1000000;
    timeout.tv_usec = usec - (1000000 * timeout.tv_sec);
    while (pth_sc(select)(1, NULL, NULL, NULL, &timeout) < 0 && errno == EINTR) ;
#endif
    return;
}

#if cpp
#if defined(HAVE_GETTIMEOFDAY_ARGS1)
#define __gettimeofday(t) gettimeofday(t)
#else
#define __gettimeofday(t) gettimeofday(t, NULL)
#endif
#endif

#if HP_TIMING_AVAIL
hp_timing_t		__tsc_counts_sec;	/* TSC counter ticks/sec */
hp_timing_t		__tsc_counts_usec;	/* TSC counter ticks/usec*/
static hp_timing_t	__tsc_baseline;		/* TSC baseline value    */
static struct timeval	__tsc_baseline_timeval;	/* TSC baseline TOD value*/


#include "get_clockfreq.c"		/* function to get TSC frequency */
#endif

/*
 * pth_time_init:
 *    - Do time initialization functions
 *    - Called from pth_initialize()
 */

intern void pth_time_init(void)
{
#if HP_TIMING_AVAIL
    /* use /proc to get processor speed for tsc ticks/sec */
    __tsc_counts_sec = __get_clockfreq();
    /* Same for usec's. Do once, use many times */
    __tsc_counts_usec = __tsc_counts_sec/1000000ull;
    /* Get the 'struct timeval' TOD for SOD time value */
    __gettimeofday(&__tsc_baseline_timeval);
    /* Get the TSC counter for SOD time value */
    HP_TIMING_NOW(__tsc_baseline);
#endif
    pth_loadtickgap = PTH_TIME(1,0);
}

#if cpp
#if HP_TIMING_AVAIL
extern hp_timing_t	__tsc_counts_sec;	/* TSC counter ticks/sec */
extern hp_timing_t	__tsc_counts_usec;	/* TSC counter ticks/usec*/
#endif
#endif

/* Convert a pth_time_t data object to a 'struct timeval' */
intern struct timeval 
pth_time_to_timeval(pth_time_t *t1)
{
    pth_time_t tval = *t1;
    struct timeval retval;
#if HP_TIMING_AVAIL
    /* compute timeval tv_sec & tv_usec values */
    retval.tv_sec  = tval / __tsc_counts_sec;
    retval.tv_usec = (tval % __tsc_counts_sec) / __tsc_counts_usec;
    if (retval.tv_usec > 1000000) {
	retval.tv_sec  += 1;
	retval.tv_usec -= 1000000;
    }
#else
    retval.tv_sec  = tval / 1000000ull;
    retval.tv_usec = tval % 1000000ull;
#endif
    return retval;
}

/* Convert a pth_time_t data object to a 'struct timespec' */
intern struct timespec 
pth_time_to_timespec(pth_time_t *t)
{
    struct timeval  tv = pth_time_to_timeval(t);
    struct timespec ts;

#ifdef TIMEVAL_TO_TIMESPEC
    TIMEVAL_TO_TIMESPEC(&tv, &ts);
#else
    ts.tv_sec  = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
#endif
    return ts;
}

/* Convert a TOD 'struct timeval' to a pth_time_t data object */
intern pth_time_t
pth_TOD_time(long sec, long usec)
{
    pth_time_t		ttim;
#if HP_TIMING_AVAIL
    pth_time_t		tadd;
    struct timeval	tval;
    /* Compute TOD - baseline SOD tod time offset */
    tval.tv_sec  = sec  - __tsc_baseline_timeval.tv_sec;
    tval.tv_usec = usec - __tsc_baseline_timeval.tv_usec;
    if (tval.tv_usec < 0) {
	tval.tv_sec--;
	tval.tv_usec += 1000000;
    }
    /* Add baseline ticks and computed offset - new time value */
    ttim = __tsc_baseline;
    tadd = PTH_TIME(tval.tv_sec, tval.tv_usec);
    pth_time_add(&ttim, &tadd);
#else
    /* non HP_TIMING case is TOD based already, just convert */
    ttim = PTH_TIME(sec, usec);
#endif
    return ttim;
}

/* calculate: t1 = t2 */
#if cpp
#if HP_TIMING_AVAIL
#define pth_time_set_now(t1)	HP_TIMING_NOW(*(t1))
#define pth_time_set(t1,t2) \
do { \
    if ((t2) == PTH_TIME_NOW) { \
	HP_TIMING_NOW(*(t1)) \
    } else \
	*(t1) = *(t2); \
} while (0)
#else
#define pth_time_set_now(t1) \
do { \
    struct timeval tt; \
    __gettimeofday(&tt); \
    *(t1) = PTH_TIME(tt.tv_sec, tt.tv_usec); \
} while (0)
#define pth_time_set(t1,t2) \
do { \
    if ((t2) == PTH_TIME_NOW) { \
	struct timeval tt; \
	__gettimeofday(&tt); \
	*(t1) = PTH_TIME(tt.tv_sec, tt.tv_usec); \
    } else \
	*(t1) = *(t2); \
} while (0)
#endif
#endif

/* calculate: t1 <=> t2 */
#if cpp
#define pth_time_cmp(t1,t2)	((pth_time_t)(*(t1) - *(t2)))
#endif

/* calculate: t1 = t1 + t2 */
#if cpp
#define pth_time_add(t1,t2)	((pth_time_t)(*(t1) += *(t2)))
#endif

/* calculate: t1 = t1 - t2 */
#if cpp
#define pth_time_sub(t1,t2)	((pth_time_t)(*(t1) -= *(t2)))
#endif

/* calculate: t1 = t1 / n */
#if cpp
#define pth_time_div(t1,n)	((pth_time_t)(*(t1) /= (n)))
#endif

/* calculate: t1 = t1 * n */
#if cpp
#define pth_time_mul(t1,n)	((pth_time_t)(*(t1) *= (n)))
#endif

/* convert a time structure into a double value */
#if cpp
#if HP_TIMING_AVAIL
#define pth_time_t2d(t)		((double)(*(t)/tsc_counts_sec))
#else
#define pth_time_t2d(t)		((double)(*(t)/1000000ull))
#endif
#endif

/* convert a time structure into a integer value */
#if cpp
#if HP_TIMING_AVAIL
#define pth_time_t2i(t)		((int)(*(t)/tsc_counts_sec))
#else
#define pth_time_t2i(t)		((int)(*(t)/1000000ull))
#endif
#endif

/* check whether time is positive */
#if cpp
#define pth_time_pos(t)		((int)(*(t) > 0))
#endif

/* check whether time values are equal */
#if cpp
#define pth_time_equal(t1,t2)	 ((t1) == (t2))
#endif

/* time value constructor */
pth_time_t pth_time(long sec, long usec)
{
    return PTH_TIME(sec, usec);
}

/* timeout value constructor */
#if cpp
static inline pth_time_t PTH_TIMEOUT(long sec, long usec)
{
    pth_time_t tv, tvadd;

    pth_time_set_now(&tv);
    tvadd = PTH_TIME(sec, usec);
    pth_time_add(&tv, &tvadd);
    return tv;
}
#endif

pth_time_t pth_timeout(long sec, long usec)
{
    return PTH_TIMEOUT(sec, usec);
}

