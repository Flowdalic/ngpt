/* Which thread is running on an lwp?
   Copyright (C) 1999 Free Software Foundation, Inc.
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
td_ta_map_lwp2thr (const td_thragent_t *ta, lwpid_t lwpid, td_thrhandle_t *th)
{
    thread_t	curr_thread;
    size_t cnt;
    int foundit;
    struct ps_prochandle *ps = ta->ph;
    struct pthread_handle_struct phc;
    struct targ_lwp_info lwp_info;

    lib_td_debug(__FUNCTION__);

    /* Test whether the TA parameter is ok.  */
    if (! ta_ok (ta)) {
	lib_td_debug(__FUNCTION__ ": abort TD_BADTA");
	return TD_BADTA;
    }

    /* get the lwp info from the thread runtime */
    if (get_targ_lwp_info(ta, &lwp_info) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading lwp_info data");
	return TD_BADTA;	/* XXX Other error value?  */
    }

    /* If number of natives is zero assume in init, use main thread */
    if (lwp_info.num_lwps == 0) {
	lib_td_debug(__FUNCTION__ ": num_lwps == 0");
	goto do_main_thread;
    }

    /* scan the native array looking for a native tid match w/lwpid */
    curr_thread = (thread_t)NULL; foundit = 0;
    for (cnt = 0; cnt < lwp_info.num_lwps; ++cnt) {
	if (lwp_info.lwp_info_tab[cnt].tid == lwpid) {
	     curr_thread = (pthread_t)lwp_info.lwp_info_tab[cnt].current;
	     foundit++;
	     if (lwp_info.lwp_info_tab[cnt].is_used)
		break;
	}
    }

    /* If current is NULL but lwp found, assume init and use main. 
       this covers us for events during ngpt's pth_init; Otherwise
       NULL indicates that we couldn't find a lwp, so abort.	   */
    if (curr_thread == (thread_t)NULL) {
	if (foundit && lwp_info.num_lwps == 1) {
	    lib_td_debug(__FUNCTION__ ": lwp_info w/current == NULL");
	    goto do_main_thread;
	}
	lib_td_debug(__FUNCTION__ ": lwpid not found in lwp_info list");
	return TD_NOLWP;
    }

    /* now let library convert curr_thread to thread handle */
    return td_ta_map_id2thr(ta, curr_thread, th);

do_main_thread:
    /* Simply return the main thread, as we are likely initializing
       and haven't gotten a native up yet or native->current is NULL */
    lib_td_debug(__FUNCTION__ ": do main thread case");
    if (ps_pdread(ps, ta->handles + 0, &phc, sizeof (phc)) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading thread handle[0]");
	return TD_BADTA;	/* XXX Other error value?  */
    }
    /* Create the `td_thrhandle_t' object.  */
    th->th_ta_p = (td_thragent_t *) ta;
    th->th_unique = phc.h_descr;
    return TD_OK;
}
