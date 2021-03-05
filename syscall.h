
/* -*-c-*- NGPT: Generic system calls
**
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Based on the GLIBC syscall stubs
**  (C) 2002 Intel Corporation by Iñaky Pérez-González
**  <inaky.perez-gonzalez@intel.com>
**
**  This file is part of NGPT, a non-preemptive thread scheduling
**  library which can be found at http://www.ibm.com/developer.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA.
**
** This is an optimization for calling system calls directly; we don't
** want to use the GLIBC version because it sets errno when there is
** an error, and we don't want GLIBC to set errno [some times] because
** it requires a mutex for calling errno_location().
**
*/
                             /* ``Pardon me for not standing.''
                                 -- Groucho Marx's epitaph */


#ifndef __ngpt_syscall_h__
#define __ngpt_syscall_h__

#if defined (__i386__)
#include "sysdeps/i386/syscall.h"
#elif defined (__ia64__)
#include "sysdeps/ia64/syscall.h"
#elif defined (__powerpc__)
#include "sysdeps/powerpc/syscall.h"
#elif defined (__s390__)
#include "sysdeps/s390/syscall.h"
#else
#error syscall.h: Unknown platform, cannot optimize
#endif

#endif /* #ifndef __ngpt_syscall_h__ */

