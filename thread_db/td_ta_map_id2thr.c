/* Map thread ID to thread handle.
   Copyright (C) 1999, 2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1999.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include "thread_dbP.h"


td_err_e
td_ta_map_id2thr (const td_thragent_t *ta, pthread_t pt, td_thrhandle_t *th)
{
#if 0
    struct pthread_handle_struct phc;
#endif
    struct _pthread_descr_struct pds;
    pth_t pt2 = (pth_t)pt;

    lib_td_debug(__FUNCTION__);

    /* Test whether the TA parameter is ok.  */
    if (! ta_ok (ta)) {
	lib_td_debug(__FUNCTION__ ": lwp2thr - abort TD_BADTA");
	return TD_BADTA;
    }

    /* Get the descriptor to see whether this is not an old thread 
       handle. We cheat as the id is the address of the thread tcb. */
    if (ps_pdread (ta->ph, pt2, &pds, sizeof (pds)) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading thread tcb");
	return TD_ERR;	/* XXX Other error value?  */
    }

#if 0
    /* Some sanity checks on the thread tcb data */
    if (pt2 != pds.td_tid) {
	lib_td_debug(__FUNCTION__ ": thread tcb data not valid - tid");
	return TD_NOTHR;
    }

    if (pds.td_nr < 0 || pds.td_nr > MAX_TD_DEBUG_HANDLES) {
	lib_td_debug(__FUNCTION__ ": thread tcb data not valid - td_nr");
	return TD_NOTHR;
    }
#endif

#if 0
    /* We can compute the entry in the handle array we want, as the
       thread tcb has the handle index (pds.td_nr).                 */
    if (ps_pdread (ta->ph, ta->handles + pds.td_nr,
					 &phc, sizeof (phc)) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading thread handle");
	return TD_ERR;	/* XXX Other error value?  */
    }

    /* Test whether this entry is in use.  */
    if (phc.h_descr == NULL) {
	lib_td_debug(__FUNCTION__ ": handle not in use");
	return TD_BADTH;
    }

    if (pds.td_tid != pt2) {
	lib_td_debug(__FUNCTION__ ": handle not for this thread");
	return TD_BADTH;
    }
#endif

    if (td_thread_terminated(&pds)) {
	lib_td_debug(__FUNCTION__ ": bad thread status(terminated)");
	return TD_NOTHR;
    }

    /* Create the `td_thrhandle_t' object.  */
    th->th_ta_p = (td_thragent_t *) ta;
    th->th_unique = pt2;

    return TD_OK;
}
