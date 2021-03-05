/* Get a thread's floating point register set.
   Copyright (C) 1999, 2000, 2001 Free Software Foundation, Inc.
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
td_thr_getfpregs (const td_thrhandle_t *th, prfpregset_t *fpregs)
{
  struct _pthread_descr_struct pds;
  struct targ_lwp_info lwp_info;
  pid_t lwpid;

  lib_td_debug(__FUNCTION__);

  /* We have to get the state and the PID for this thread.  */
  if (ps_pdread (th->th_ta_p->ph, th->th_unique, &pds,
		 sizeof (struct _pthread_descr_struct)) != PS_OK) {
    lib_td_debug(__FUNCTION__ ": failure getting thread descr");
    return TD_ERR;
  }

  /* If the thread already terminated we return all zeroes.  */
  if (td_thread_terminated(&pds)) {
    lib_td_debug(__FUNCTION__ ": terminated thread, return zeros");
    memset (fpregs, '\0', sizeof (prfpregset_t));
    return TD_OK;
  }

  /* get the lwp info from the thread runtime */
  if (get_targ_lwp_info(th->th_ta_p, &lwp_info) != PS_OK) {
    lib_td_debug(__FUNCTION__ ": failure getting lwp_info data");
    return TD_BADTA;	/* XXX Other error value?  */
  }

  /* If we are a bound thread then we have the native descr */
  lwpid = 0;
  if (pds.boundnative != NULL) {
    struct pth_descr_st *descr;
    if (get_targ_lwp_descr(th->th_ta_p, &lwp_info,
				        pds.boundnative, &descr) != PS_OK) {
      lib_td_debug(__FUNCTION__ ": failure getting lwp_info data");
      return TD_BADTA;	/* XXX Other error value?  */
    }
    if (descr->is_used)
	lwpid  = descr->tid;
  }
  else
  {
    int cnt;
    /* Special case startup where there is one clone still being built.
       For this case assume a thread match and use the lwp's tid.        */
    if (lwp_info.num_lwps == 1 && lwp_info.lwp_info_tab[0].current == NULL)
	lwpid = lwp_info.lwp_info_tab[0].tid;
    else {
	/* scan the native array looking for a native current match */
	for (cnt = 0; cnt < lwp_info.num_lwps; ++cnt) {
	    if (lwp_info.lwp_info_tab[cnt].is_used == 0)
		continue;
	    if (lwp_info.lwp_info_tab[cnt].current == th->th_unique) {
		lwpid = lwp_info.lwp_info_tab[cnt].tid;
		break;
	    }
	}
    }
  }

  /* A lwpid at this point that is non-zero implies bound or running 	*/
  if (lwpid != 0) {
      /* Read the clone regs directly as we are assuming that it is 
	 stopped and will resume running this thread again.		*/
      if (ps_lgetfpregs (th->th_ta_p->ph, lwpid, fpregs) != PS_OK) {
	lib_td_debug(__FUNCTION__ ": failure getting fpregs");
	return TD_ERR;
      }
      return TD_OK;
  }

  /* Other cases get harder as we have to consider the saved state in
     the thread, in the tcb mctx structure.				*/
#if PTH_MCTX_MTH(mcsc)
  /* Use the ucontext_t state saved in the tcb mctx member. This should 
     have the complete register set, work will be to get it copied where
     gdb expects the registers to be in fpregs.                         */
  /* it appears that the NGPT ucontext_t for mctx *matches* the register
     ordering for prfpregset_t, what we have to pass back as fpregs. So, a
     simple copy is in order.						*/
#if defined(__i386__)
  if (pds.mctx.uc.uc_mcontext.fpregs == NULL) {
    lib_td_debug(__FUNCTION__ ": uninitialized thread, return zeros");
    memset (fpregs, '\0', sizeof (prfpregset_t));
    return TD_OK;
  }
  /* Compute the register address relative to the pds structure */
  {
     unsigned long fpregs_addr	   = (unsigned long)pds.mctx.uc.uc_mcontext.fpregs;
     unsigned long base_pds_addr   = (unsigned long)th->th_unique;
     unsigned long fpregs_off	   = fpregs_addr - base_pds_addr;
     char *        pds_fpregs_addr = (char *)&pds + fpregs_off;
     memcpy((void *)fpregs, (void *)pds_fpregs_addr, sizeof(prfpregset_t));
  }
#elif defined (__ia64__)
  memcpy(fpregs, pds.mctx.uc.uc_mcontext.sc_fr, sizeof(prfpregset_t));
#elif defined (__powerpc__)
  memcpy(fpregs, pds.mctx.uc.uc_mcontext.fpregs, sizeof(prfpregset_t));
#elif defined (__s390__)
  memcpy(fpregs, pds.mctx.uc.uc_mcontext.fpregs, sizeof(prfpregset_t));
#else
#error td_thr_getgregs: Unknown arch, cant implement ucontext getgregs
#endif

#else
#error "unknown mctx method"
#endif

  return TD_OK;
}
