/* Get thread information.
   Copyright (C) 1999, 2000 Free Software Foundation, Inc.
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

#include <stddef.h>

#include <pth_p.h>
#include "schedule.h"
#include "thread_dbP.h"

td_err_e
td_thr_get_info (const td_thrhandle_t *th, td_thrinfo_t *infop)
{
  struct _pthread_descr_struct pds;

  lib_td_debug(__FUNCTION__);

  /* Get the thread descriptor.  */
  if (ps_pdread (th->th_ta_p->ph, th->th_unique, &pds,
			           th->th_ta_p->sizeof_descr) != PS_OK) {
    lib_td_debug(__FUNCTION__ ": failure reading thread_descriptor");
    return TD_ERR;	/* XXX Other error value?  */
  }

  /* Fill in information.  Clear first to provide reproducable
     results for the fields we do not fill in.  */
  memset (infop, '\0', sizeof (td_thrinfo_t));

  /* We have to handle the manager thread and scheduler special since
     the thread descriptor in older versions is not fully initialized.  */
  if (pds.td_nr == 1 || pds.state == PTH_STATE_SCHEDULER)
    {
      infop->ti_type = TD_THR_SYSTEM;
      infop->ti_state = TD_THR_ACTIVE;
    }
  else
    {
      infop->ti_tls = (char *) pds.data_value;
      infop->ti_pri = __thread_prio_get (&pds);
      infop->ti_type = TD_THR_USER;
      
      if (! td_thread_terminated(&pds))
	/* XXX For now there is no way to get more information.  */
	infop->ti_state = TD_THR_ACTIVE;
      else if (! td_thread_detached(&pds))
	infop->ti_state = TD_THR_ZOMBIE;
      else
	infop->ti_state = TD_THR_UNKNOWN;
    }

  /* Initialization which is the same in both cases.  */
  infop->ti_tid = (pthread_t)pds.td_tid;

  /* Find the target lwp that is currently associated with thread */
  if (get_targ_lwp_from_thr(th, &pds, NULL, &infop->ti_lid) != TD_OK) {
    lib_td_debug(__FUNCTION__ ": failure getting lwpid from thr");
    return TD_ERR;	/* XXX Other error value?  */
  }

  infop->ti_ta_p = th->th_ta_p;
  infop->ti_startfunc = pds.start_func;
  memcpy (&infop->ti_events, &pds.p_eventbuf.eventmask,
	  sizeof (td_thr_events_t));
  infop->ti_traceme = pds.p_report_events != 0;

  return TD_OK;
}
