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
**  pth_debug.c: Pth debugging support
*/
                             /* ``MY HACK: This universe.
                                  Just one little problem:
                                  core keeps dumping.'' 
                                              -- Unknown  */
#include "pth_p.h"
#include "spinlock.h"
#include "allocation.h"
#include "pqueue.h"

#if cpp
#include "debug.h"

#undef PTH_DEBUG
#ifndef PTH_DEBUG

#define pth_debug1(a1)                     /* NOP */
#define pth_debug2(a1, a2)                 /* NOP */
#define pth_debug3(a1, a2, a3)             /* NOP */
#define pth_debug4(a1, a2, a3, a4)         /* NOP */
#define pth_debug5(a1, a2, a3, a4, a5)     /* NOP */
#define pth_debug6(a1, a2, a3, a4, a5, a6) /* NOP */

#else

#define pth_debug1(a1)                     debugmsg (a1 "\n")
#define pth_debug2(a1, a2)                 debugmsg (a1 "\n", a2)
#define pth_debug3(a1, a2, a3)             debugmsg (a1 "\n", a2, a3)
#define pth_debug4(a1, a2, a3, a4)         debugmsg (a1 "\n", a2, a3, a4)
#define pth_debug5(a1, a2, a3, a4, a5)     debugmsg (a1 "\n", a2, a3, a4, a5)
#define pth_debug6(a1, a2, a3, a4, a5, a6) debugmsg (a1 "\n", a2, a3, a4, a5, a6)

#endif /* PTH_DEBUG */

#undef TD_DEBUG
#ifndef TD_DEBUG

#define td_debug(a1)                      /* NOP */
#define td_debug1(a1)                     /* NOP */
#define td_debug2(a1, a2)                 /* NOP */
#define td_debug3(a1, a2, a3)             /* NOP */
#define td_debug4(a1, a2, a3, a4)         /* NOP */
#define td_debug5(a1, a2, a3, a4, a5)     /* NOP */
#define td_debug6(a1, a2, a3, a4, a5, a6) /* NOP */

#else

#define td_debug(a1)                      debugmsg (a1 "\n")
#define td_debug1(a1)                     debugmsg (a1 "\n")
#define td_debug2(a1, a2)                 debugmsg (a1 "\n", a2)
#define td_debug3(a1, a2, a3)             debugmsg (a1 "\n", a2, a3)
#define td_debug4(a1, a2, a3, a4)         debugmsg (a1 "\n", a2, a3, a4)
#define td_debug5(a1, a2, a3, a4, a5)     debugmsg (a1 "\n", a2, a3, a4, a5)
#define td_debug6(a1, a2, a3, a4, a5, a6) debugmsg (a1 "\n", a2, a3, a4, a5, a6)

#endif /* TD_DEBUG */

#endif /* cpp */

/* dump out a page to stderr summarizing the internal state of Pth */
intern void pth_dumpstate (void)
{
    int slot = 0;

    debugmsg ("+----------------------------------------------------------------------\n");
    debugmsg ("| Next Generation POSIX Threading\n");
    debugmsg ("| NGPT Version: %s\n", PTH_VERSION_STR);
    debugmsg ("| Load Average: %.2f\n", pth_loadval);
    debugmsg ("| Current Number of natives: %d\n", pth_number_of_natives);
    debugmsg ("| Max Number of natives: %d\n", pth_max_native_threads);
    debugmsg ("| Number of user threads per native: %d\n", pth_threads_per_native);
    debugmsg ("| Thread ID: %d\n", k_gettid());
    pth_dumpnatives();
    debugmsg ("| Thread Queue NEW:\n");
    pqueue_dump (&pth_NQ);
    debugmsg ("| Thread Queue READY:\n");
    pqueue_dump (&pth_RQ);
    debugmsg ("| Thread Queue RUNNING:\n");
    while (pth_native_list[slot].is_used) {
	pth_descr_t descr = &pth_native_list[slot];
	if (descr->current != NULL)
	    debugmsg ("|   %d. thread 0x%lx (\"%s\") running on native tid %d, "
                      "last ran native tid=%d\n", 
                      slot+1, (unsigned long)descr->current, descr->current->name,
                      descr->tid, descr->current->lastrannative);
	else
	    debugmsg ("|   %d. No current thread for native tid %d", slot+1, descr->tid);
	slot++;
    }
    slot = 0;
    while (pth_native_list[slot].is_used) {
	pth_descr_t descr = &pth_native_list[slot++];
	debugmsg ("| WAITING threads on Native: %d, tid: %d\n", slot, descr->tid);
	pqueue_dump (&descr->wait_queue);
    }
    debugmsg ("| Thread Queue SUSPENDED:\n");
    pqueue_dump (&pth_SQ);
    debugmsg ("| Thread Queue DEAD:\n");
    pqueue_dump (&pth_DQ);
    debugmsg ("| Memory allocator status\n");
    cal_dump (&gcal);
    debugmsg ("+----------------------------------------------------------------------\n");
    return;
}
