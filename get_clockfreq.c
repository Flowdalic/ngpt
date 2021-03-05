  /* -*-c-*- NGPT: Get cpu clock frequency switch for arch-dependent source
  ** 
  ** Portions (C) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
  ** (C) 2001 International Business Machines Corporation
  **   Bill Abt <babt@us.ibm.com>
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

#ifndef __ngpt_get_clockfreq_c__
#define __ngpt_get_clockfreq_c__

#include "hp-timing.h"

hp_timing_t __get_clockfreq (void);

#if defined (__i386__)
#include "sysdeps/i386/get_clockfreq.c"
#elif defined (__ia64__)
#include "sysdeps/ia64/get_clockfreq.c"
#elif defined (__powerpc__)
#include "sysdeps/powerpc/get_clockfreq.c"
#elif defined (__s390__)
#include  "sysdeps/s390/get_clockfreqs.c"
#else
#error get_clockfreq.c: Unknown platform, cannot optimize
#endif

#endif				/* __ngpt_get_clockfreq_c__ */
