/* Utility functions for libthread_db functions.
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

void
td_utils_gregset_to_prgregset(gregset_t gregset, prgregset_t prgregset)
{
#if defined(__i386__)
    prgregset[0]  = gregset[8];		/* EBX */
    prgregset[1]  = gregset[10];	/* ECX */
    prgregset[2]  = gregset[9];		/* EDX */
    prgregset[3]  = gregset[5];		/* ESI */
    prgregset[4]  = gregset[4];		/* EDI */
    prgregset[5]  = gregset[6];		/* EBP */
    prgregset[6]  = gregset[11];	/* EAX */
    prgregset[7]  = gregset[3];		/* DS  */
    prgregset[8]  = gregset[2];		/* ES  */
    prgregset[9]  = gregset[1];		/* FS  */
    prgregset[10] = gregset[0];		/* GS  */
    prgregset[11] = gregset[11];	/* ORIG_EAX  */
    prgregset[12] = gregset[14];	/* EIP */
    prgregset[13] = gregset[15];	/* CS  */
    prgregset[14] = gregset[16];	/* EFL */
    prgregset[15] = gregset[17];	/* UESP */
    prgregset[16] = gregset[18];	/* SS  */
#elif defined(__ia64__) || defined(__powerpc__) || defined(__s390__)
    memcpy(&prgregset[0], gregset, sizeof(prgregset_t));
#else
#error "Unsupported Architecture"
#endif
}
void
td_utils_prgregset_to_gregset(prgregset_t prgregset, gregset_t gregset)
{
#if defined(__i386__)
    gregset[0]  = prgregset[10];	/* GS  */
    gregset[1]  = prgregset[9];		/* FS  */
    gregset[2]  = prgregset[8];		/* ES  */
    gregset[3]  = prgregset[7];		/* DS  */
    gregset[4]  = prgregset[4];		/* EDI */
    gregset[5]  = prgregset[3];		/* ESI */
    gregset[6]  = prgregset[5];		/* EBP */
    gregset[7]  = prgregset[15];	/* ESP */
    gregset[8]  = prgregset[0];		/* EBX */
    gregset[9]  = prgregset[2];		/* EDX */
    gregset[10] = prgregset[1];		/* ECX */
    gregset[11] = prgregset[6];		/* EAX */
    gregset[12] = 0;			/* TRAPNO */
    gregset[13] = 0;			/* ERR */
    gregset[14] = prgregset[12];	/* EIP */
    gregset[15] = prgregset[13];	/* CS  */
    gregset[16] = prgregset[14];	/* EFL */
    gregset[17] = prgregset[15];	/* UESP */
    gregset[18] = prgregset[16];	/* SS  */
#elif defined(__ia64__) || defined(__powerpc__) || defined(__s390__)
    memcpy(gregset, prgregset, sizeof(gregset_t));
#else
#error "Unsupported Architecture"
#endif
}

