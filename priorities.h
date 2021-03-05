
  /* -*-c-*- NGPT: Priority massaging
  ** 
  ** $Id: priorities.h,v 1.1 2002/10/09 15:11:15 billa Exp $
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **
  ** This file is part of NGPT, a non-preemptive thread scheduling
  ** library which can be found at http://www.ibm.com/developer.
  **
  ** This library is free software; you can redistribute it and/or
  ** modify it under the terms of the GNU Lesser General Public
  ** License as published by the Free Software Foundation; either
  ** version 2.1 of the License, or (at your option) any later version.
  **
  ** This library is distributed in the hope that it will be useful,
  ** but WITHOUT ANY WARRANTY; without even the implied warranty of
  ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  ** Lesser General Public License for more details.
  **
  ** You should have received a copy of the GNU Lesser General Public
  ** License along with this library; if not, write to the Free Software
  ** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  ** USA, or contact Bill Abt <babt@us.ibm.com>
  **
  **
  ** Priorities come in different flavors, and different parts of the
  ** code prefer some others ...
  **
  ** - I - Internal priorities: are the ones seen and used by the library
  **   to do the day-to-day calculations.
  **
  ** - X - External priorities: are the ones seen by the user, that
  **   conform to the POSIX standard. They also include the execution
  **   policy [wow!].
  **
  ** - E - Effective priorities: are the ones used by the scheduler;
  **   they are basically the internal priority on the rocks [modified
  **   according to some parameters here and there].
  **
  ** The idea is that priorities have to be mapped, from the space
  ** that the user sees when programming applications
  ** [policy+priority] to a flat space so it is much easier to sort in
  ** priority order [for example, for who is the first acquiring a
  ** mutex, etc].
  **
  ** The pthread/mutex/etc attribute structure stores the priorities
  ** in the INTERNAL format; when set, it is validated against the
  ** current policy [POSIX is kind of blurry regarding this]. The
  ** thread structures keep their priorities in INTERNAL
  ** format. Synchronization primitives do the same.
  **
  ** The internal mapping gives a higher priority to FIFO and RR
  ** polcies than the OTHER policy. This way, no black magic has to
  ** be done and there is no need to check the policy when sorting
  ** (call it embedding the policy in the priority). FIFO and RR are
  ** given the same treatment (aka: a FIFO thread with priority 3 has
  ** the same priority as an RR thread with priority 3 - this is what
  ** it seems to me that POSIX 2001-1003.1, page 46, paragraph 1900
  ** means: RR is like FIFO, but with a timeslice).
  **
  ** The range of priorities for normal threads is chosen to have 41
  ** levels, from 0 [lowest] to 41 [highest], so it is as the "normal"
  ** unix processes (from -20 to 20, 41 levels). However, it is quite
  ** difficult to make it the same because niceness -20 is the maximum
  ** prio and 20 the minium prio ... whereas FIFO and RR tasks are 99
  ** maximum and 1 minimum (in Linux).
  **
  ** Thus, for each policy (let's call them 0 - OTHER, 1 - RR and 2 -
  ** FIFO), the external to internal mappings will be (m lowest, M
  ** highest, e external-user space, i internal-mapped):
  **
  ** mi0 = mx0            Mi0 = Mx0          xi0 = xx0
  ** mi1 = Mi0 + mx1      Mi1 = Mi0 + Mx1    xi1 = Mi0 + xx1
  ** mi2 = Mi0 + mx2      Mi2 = Mi0 + Mx2    xi2 = Mi0 + xx2
  ** 
  ** And viceversa, internal-to-external:
  **
  ** mx0 = mi0            Mx0 = Mi0          xx0 = xi0
  ** mx1 = mi1 - Mi0      Mx1 = Mi1 - Mi0    xx1 = xi1 - Mi0
  ** mx2 = mi2 - Mi0      Mx2 = Mi2 - Mi0    xx2 = xi2 - Mi0
  */

#ifndef __ngpt_priorities_h__
#define __ngpt_priorities_h__

#include <sched.h>
#include <errno.h>


  /* Priority ranges for each scheduling policy
  **
  ** In linux, SCHED_OTHER, SCHED_FIFO and SCHED_RR are 0, 1 and 2,
  ** thus a linear array for the limits suits it.
  **
  ** WARNING: Any other arrangement would need manual adjustment
  **
  ** FIXME: optimize this array for shared libraries [See Ulrich's
  ** presentation]
  **
  ** FIXME: these values (except the ones for OTHER) should be
  ** obtained at configure time.
  */

#define SCHED_INTERNAL 3 /* Convenience */
#define XPRIO_DEFAULT 20
#define IPRIO_MAX 140
#define IPRIO_MIN 0

struct priority_bounds_s {
    int min, max;
};

static const
struct priority_bounds_s priority_bounds[] = {
    { 0,  41 }, /* External SCHED_OTHER */
    { 1,  99 }, /* External SCHED_FIFO */
    { 1,  99 }, /* External SCHED_RR */
    { IPRIO_MIN, IPRIO_MAX }, /* Internal */
};


  /* Return the minimum external priority bound for each policy */

static __inline__
int xprio_get_min (int sched_policy)
{
    switch (sched_policy) {
      case SCHED_OTHER:
      case SCHED_FIFO:
      case SCHED_RR:
        return priority_bounds[sched_policy].min;
      default:
        errno = EINVAL;
        return -1;
    }
}


   /* Return the maximum external priority bound for each policy */

static __inline__
int xprio_get_max (int sched_policy)
{
    switch (sched_policy) {
      case SCHED_OTHER:
      case SCHED_FIFO:
      case SCHED_RR:
        return priority_bounds[sched_policy].max;
      default:
        errno = EINVAL;
        return -1;
    }
}


  /* Convert an internal priority+policy to an external priority */

static __inline__
int iprio_to_xprio (int sched_policy, int internal_prio)
{
    switch (sched_policy) {
      case SCHED_OTHER:
        return internal_prio;
      case SCHED_FIFO:
      case SCHED_RR:
        return internal_prio - priority_bounds[SCHED_OTHER].max;
      default:
        errno = EINVAL;
        return -1;
    }
}


  /* Convert an external priority+policy to an internal priority */

static __inline__
int xprio_to_iprio (int sched_policy, int external_prio)
{
    switch (sched_policy) {
      case SCHED_OTHER:
        return external_prio;
      case SCHED_FIFO:
      case SCHED_RR:
        return external_prio + priority_bounds[SCHED_OTHER].max;
      default:
        errno = EINVAL;
        return -1;
    }
}


  /* Validate a priority versus the range for its policy
  **
  ** NOTE: you can use SCHED_INTERNAL for validating internal (i) or
  ** effective (e) priorities.
  */

static __inline__
int prio_validate (int sched_policy, int external_prio)
{
    if ((external_prio < xprio_get_min (sched_policy)
         || external_prio > xprio_get_max (sched_policy))) {
        errno = EINVAL;
        return -1;
    }
    else
        return 0;
}


  /* Get the default priority for a policy */

static __inline__
int xprio_get_default (int sched_policy)
{
    switch (sched_policy) {
      case SCHED_OTHER:
      case SCHED_FIFO:
      case SCHED_RR:
        return (priority_bounds[sched_policy].max
                - priority_bounds[sched_policy].min) / 2;
      default:
        errno = EINVAL;
        return -1;
    }
}
                              
static __inline__
int iprio_get_default (int sched_policy)
{
    return xprio_to_iprio (sched_policy, xprio_get_default (sched_policy));
}

#endif				/* #ifndef __ngpt_priorities_h__ */

