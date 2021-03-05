
  /* -*-c-*- NGPT: Bit operations for s390
  ** 
  ** $Id: bitops.h,v 1.2 2002/11/15 15:54:09 billa Exp $
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

#ifndef __ngpt_bitops_s390_h__
#define __ngpt_bitops_s390_h__

static __inline__ int test_and_set(volatile unsigned int *atomic)
{
    int ret;

    __asm__ __volatile__("	la  1,1		\n"
			 "	la  2,%2	\n"
			 "	slr 0,0		\n"
			 "	cs  0,1,0(2)	\n"
			 "	lr  %1,0	\n"
			 :"=m" (*atomic), "=r" (ret)
			 :"m" (*atomic)
			 :"0","1","2");
    return ret;
}


  /* Finds first MOST SIGNIFICANT bit set */

static __inline__ int find_first_ms_bit(unsigned long w)
{
    int r = 32;

    if (w == 0)
	return 0;
  __asm__("    tmh  %1, 0xffff  \n"
          "    jz   0f          \n"
          "    sll  %1, 16      \n"
          "    ahi  %0, -16     \n"
          "0:  tmh  %1, 0xff00  \n"
          "    jz   1f          \n"
          "    sll  %1, 8       \n"
          "    ahi  %0, -8      \n"
          "1:  tmh  %1, 0xf000  \n"
          "    jz   2f          \n"
          "    sll  %1, 4       \n"
          "    ahi  %0, -4      \n"
          "2:  tmh  %1, 0xc000  \n"
          "    jz   3f          \n"
          "    sll  %1, 2       \n"
          "    ahi  %0, -2      \n"
          "3:  tmh  %1, 0x8000  \n"
          "    jz   4f          \n"
          "    ahi  %0, -1      \n"
          "4:                     "
          :"+d"(r), "+d" (w)
          :
          : "cc");
    return r;
}

#endif				/* __ngpt_bitops_s390_h__ */
