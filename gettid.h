
/* -*-c-*- NGPT: Temporary gettid() support
**
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Based on work by Matthew Kirkwood <matthew@hairy.beasts.org>.
**  (C) 2002 Intel Corporation by Iñaky Pérez-González
**  <inaky.perez-gonzalez@intel.com> [Reorganization and cleanup for
**  use for inter-native thread spinlocks].
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
**
** This is a simple invocation of the kernel's gettid(). Currently
** most glibc's don't include it [does any?] so we need it ... split
** off big header files to avoid dependency nightmares.
**
** FIXME: avoid using glibc's syscall() in other archs???
**
*/
                             /* ``Jarl, no puedorl, no puedorl ...''
                                 -- Chiquito de la Calzada */

#ifndef __ngpt_gettid_h__
#define __ngpt_gettid_h__

#include <sys/types.h>  /* pid_t */

#ifndef __NR_gettid
#if defined(__powerpc__)
#define __NR_gettid	207
#elif defined(__s390__)
#define __NR_gettid	236
#elif defined(__ia64__)
#define __NR_gettid	1105
#else
#define __NR_gettid	224
#endif
#endif


static __inline__
pid_t k_gettid(void) {
#if defined(__i386__)
	int __result;
	__asm__ volatile (
          "int $0x80"
          : "=a" (__result)
          : "a" (__NR_gettid)
          );
	return __result;
#else
	return syscall(__NR_gettid);
#endif
}

#endif /* #ifdef __ngpt_gettid_h__ */
