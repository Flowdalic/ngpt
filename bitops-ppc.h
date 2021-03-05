
  /* -*-c-*- NGPT: Bit operations for ppc
  ** 
  ** $Id: bitops-ppc.h,v 1.2 2002/09/10 15:29:36 billa Exp $
  **
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** Some parts based on portions (C) The Linux Kernel Hackers
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
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
  */

#ifndef __ngpt_bitops_ppc_h__
#define __ngpt_bitops_ppc_h__

static __inline__ int test_and_set(volatile unsigned int *atomic)
{
    int ret;

    __asm__ __volatile__("1:     lwarx   %0,0,%1   \n"
			 "       cmpwi   0,%0,0    \n"
			 "       bne-    2f        \n"
			 "       stwcx.  %2,0,%1   \n"
			 "       bne-    1b        \n"
			 "       isync             \n" "2:":"=&r"(ret)
			 :"r"(atomic), "r"(1)
			 :"cr0", "memory");
    return ret;
}

  /* Finds first MOST SIGNIFICANT bit set */

static __inline__ int find_first_ms_bit(unsigned long w)
{
    int r;

  __asm__("cntlzw %0,%1":"=r"(r)
  :	    "r"(w));
    return 32 - r;
}

#endif				/* __ngpt_bitops_ppc_h__ */
