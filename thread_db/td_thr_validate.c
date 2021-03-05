/* Validate a thread handle.
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
td_thr_validate (const td_thrhandle_t *th)
{
  struct pthread_handle_struct *handles = th->th_ta_p->handles;
  int pthread_handles_num;
  int cnt;

  lib_td_debug(__FUNCTION__);

  if (ps_pdread (th->th_ta_p->ph, th->th_ta_p->pthread_handles_num,
		 &pthread_handles_num, sizeof (int)) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failed read of pthread_handles_num");
	return TD_ERR;	/* XXX Other error value?  */
  }

  /* Now get all active descriptors, one after the other.  */
  for (cnt = 0; cnt < pthread_handles_num; ++cnt, ++handles) {
      struct pthread_handle_struct phc;

      if (ps_pdread (th->th_ta_p->ph, handles, &phc,
		     sizeof (struct pthread_handle_struct)) != PS_OK) {
	 lib_td_debug(__FUNCTION__ ": failed read of handle");
	 return TD_ERR;	/* XXX Other error value?  */
      }

      if (phc.h_descr != NULL && phc.h_descr == th->th_unique) {
	  struct _pthread_descr_struct pds;

	  if (ps_pdread (th->th_ta_p->ph, phc.h_descr, &pds,
			 th->th_ta_p->sizeof_descr) != PS_OK) {
	    lib_td_debug(__FUNCTION__ ": failed read of handle's h_descr");
	    return TD_ERR;	/* XXX Other error value?  */
	  }
	  return td_thread_terminated(&pds) ? TD_NOTHR : TD_OK;
      }
  }

  return TD_ERR;
}
