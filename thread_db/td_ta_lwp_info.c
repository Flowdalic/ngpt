/* Get info on the runtime lwps, somewhat implementation dependent
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

static void			*p_pthread_native_list = NULL;
static void			*p_pthread_number_of_natives = NULL;
static struct pth_descr_st	native_list[MAX_NATIVE_LIST];
static int			num_natives = 0;

td_err_e
get_targ_lwp_info(const td_thragent_t *ta, struct targ_lwp_info *lip)
{
    struct ps_prochandle *ps = ta->ph;
    psaddr_t addr;

    lip->num_lwps	= 0;
    lip->lwp_info_tab	= NULL;

    /* get the addresses of the native list and number of natives */
    if (p_pthread_native_list == NULL) {
	if (td_lookup (ps, PTHREAD_NATIVE_LIST, &addr) != PS_OK) {
	    lib_td_debug(__FUNCTION__ ": native list symbol missing");
	    return TD_ERR;
	}
	p_pthread_native_list = addr;
    }
    if (p_pthread_number_of_natives == NULL) {
	if (td_lookup (ps, PTHREAD_NUMBER_OF_NATIVES, &addr) != PS_OK) {
	    lib_td_debug(__FUNCTION__ ": number of natives symbol missing");
	    return TD_ERR;
	}
	p_pthread_number_of_natives = addr;
    }

    /* Read number of native descriptors in the table  */
    if (ps_pdread (ps, p_pthread_number_of_natives,
			   &num_natives, sizeof(int)) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading number of natives");
	return TD_BADTA;	/* XXX Other error value?  */
    }

    /* If number of natives is > 0 read in the array */
    if (num_natives > 0) {
	/* read the pthread_native_list array */
	if (ps_pdread (ps, p_pthread_native_list, &native_list[0],
			   sizeof(native_list[0]) * num_natives) != PS_OK) {
	    lib_td_debug(__FUNCTION__ ": failure reading native table");
	    return TD_BADTA;	/* XXX Other error value?  */
	}
    }

    /* update target_lwp_info for return */
    lip->num_lwps 	    = num_natives;
    lip->lwp_info_tab  	    = &native_list[0];
    lip->lwp_info_base_addr = p_pthread_native_list;

    return TD_OK;
}

td_err_e
get_targ_lwp_descr(const td_thragent_t *ta,
		   struct targ_lwp_info *lip,
		   psaddr_t descr_addr,
		   struct pth_descr_st **descr)
{
    struct targ_lwp_info lwp_info;
    psaddr_t lwp_info_base_addr;
    unsigned long off, slot;

    /* If we don't have a filled in lwp_info struct we need to build it */
    if (lip == NULL) {
	/* get the lwp info from the thread runtime */
	if (get_targ_lwp_info(ta, &lwp_info) != PS_OK) {
	    lib_td_debug(__FUNCTION__ ": failure reading lwp_info data");
	    return TD_BADTA;	/* XXX Other error value?  */
	}
	lip = &lwp_info;
    }
    lwp_info_base_addr = lip->lwp_info_base_addr;

    /* sanity check the descriptor address against lwp_info_tab range */
    off  = (unsigned long)descr_addr - (unsigned long)lwp_info_base_addr;
    slot = off / sizeof(struct pth_descr_st); 
    if (descr_addr < lwp_info_base_addr		  ||
	(off % sizeof(struct pth_descr_st)) != 0  ||
	slot > lip->num_lwps)
    {
	lib_td_debug(__FUNCTION__ ": descr_addr out of range");
	return TD_ERR;
    }

    /* return the address of the stored descr item */
    *descr = &lip->lwp_info_tab[slot];

    return TD_OK;
}

td_err_e
get_targ_lwp_from_thr(const td_thrhandle_t *th,
		      struct _pthread_descr_struct *thr_descr_p,
		      struct targ_lwp_info *lwp_info_p,
		      lwpid_t *ret_lwpid_p)
{
  struct targ_lwp_info lwp_info;
  td_thragent_t *ta = th->th_ta_p;
  lwpid_t lwpid;
  struct _pthread_descr_struct pds;

  /* Get the thread descriptor, if addr not passed in.	  */
  if (thr_descr_p == NULL) {
    /* Get the thread descriptor structure */
    if (ps_pdread (ta->ph, th->th_unique, &pds,
		           th->th_ta_p->sizeof_descr) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure reading thread_descriptor");
	return TD_ERR;	/* XXX Other error value?  */
    }
    thr_descr_p = &pds;
  }

  /* Get the target lwp info, if addr not passed in.	  */
  if (lwp_info_p == NULL) {
    /* get the lwp info from the thread runtime */
    if (get_targ_lwp_info(ta, &lwp_info) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure getting lwp_info data");
	return TD_BADTA;	/* XXX Other error value?  */
    }
    lwp_info_p = &lwp_info;
  }

  /* If we are a bound thread then we have the native descr */
  lwpid = 0;
  if (thr_descr_p->boundnative != NULL) {
    struct pth_descr_st *descr;
    if (get_targ_lwp_descr(ta, lwp_info_p,
			       thr_descr_p->boundnative, &descr) != PS_OK) {
      lib_td_debug(__FUNCTION__ ": failure getting boundnative descr");
      return TD_BADTA;	/* XXX Other error value?  */
    }
    if (descr->is_used) {
	lwpid = descr->tid;
	lib_td_debug(__FUNCTION__ ": got boundnative descr->tid for lwpid");
    }
  }
  else
  if (thr_descr_p->state == PTH_STATE_WAITING &&
		   thr_descr_p->waited_native != NULL) {
    struct pth_descr_st *descr;
    if (get_targ_lwp_descr(ta, lwp_info_p,
			       thr_descr_p->waited_native, &descr) != PS_OK) {
      lib_td_debug(__FUNCTION__ ": failure getting waited_native descr");
      return TD_BADTA;	/* XXX Other error value?  */
    }
    if (descr->is_used) {
	lwpid = descr->tid;
	lib_td_debug(__FUNCTION__ ": got waited_native descr->tid for lwpid");
    }
  }
  else
  {
    int cnt;
    /* scan the native array looking for a native current match threadid */
    for (cnt = 0; cnt < lwp_info_p->num_lwps; ++cnt) {
	if (lwp_info_p->lwp_info_tab[cnt].is_used == 0)
	    continue;
	if (lwp_info_p->lwp_info_tab[cnt].current == th->th_unique) {
	    lwpid = lwp_info_p->lwp_info_tab[cnt].tid;
	    lib_td_debug(__FUNCTION__ ": got native descr->tid for lwpid");
	    break;
	}
    }
  }

  /* If we didn't find a bound or current that matched, use the
     lastrannative value in the thread descriptor		 */
  if (lwpid == 0) {
    /* I'm a little dubious if this is right for PTH_STATE_NEW, 
       PTH_STATE_READY, PTH_STATE_DEAD, or PTH_STATE_EXIT. We 
       might should return with ti_lid == 0 for these. This is 
       likely ok for many cases if only data refs are done.	 */
    lib_td_debug(__FUNCTION__ ": using lastrannative as lwpid");
    if ((lwpid = thr_descr_p->lastrannative) == 0) {
	lib_td_debug(__FUNCTION__ ": last resort - using pid as lwpid");
	lwpid = ps_getpid (th->th_ta_p->ph); /* last resort */
    }
  }

  /* pass back the lwpid value and return */
  *ret_lwpid_p = lwpid;

  return TD_OK;
}
