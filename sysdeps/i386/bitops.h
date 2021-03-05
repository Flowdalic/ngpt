
  /* -*-c-*- NGPT: Bit operations for ia32
  ** 
  ** $Id: bitops.h,v 1.1 2002/11/14 15:25:40 billa Exp $
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

#ifndef __ngpt_bitops_ia32_h__
#define __ngpt_bitops_ia32_h__


  /* Set to one if non-zero, return prev value */

static __inline__ int test_and_set(volatile unsigned int *w)
{
    int ret;

    __asm__ __volatile__("lock; xchgl %0, %1":"=r"(ret), "=m"(*w)
			 :"0"(1), "m"(*w)
			 :"memory");
    return ret;
}


  /* Finds first MOST SIGNIFICANT bit set */

static __inline__ int find_first_ms_bit(unsigned long w)
{
    __asm__ __volatile__("bsrl %1, %0":"=r"(w)
			 :"rm"(w));
    return w;
}


  /* Set a bit in an array of unsigned long [atomic] */

static __inline__ void bit_set(int bit, volatile unsigned long *w)
{
    __asm__ __volatile__("lock; btsl %1, %0":"=m"(*w)
			 :"Ir"(bit));
}


  /* Clear a bit in an array of unsigned long [atomic] */

static __inline__ void bit_reset(int bit, volatile unsigned long *w)
{
    __asm__ __volatile__("lock; btrl %1, %0":"=m"(*w)
			 :"Ir"(bit));
}


#endif				/* __ngpt_bitops_ia32_h__ */
