/* Special definitions for ix86 machine using segment register based
   thread descriptor.
   Copyright (C) 1998, 2000, 2001, 2002 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@cygnus.com>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef _USELDT_H_
#define _USELDT_H_

#ifndef __ASSEMBLER__
#include <stddef.h>	/* For offsetof.  */
#include <stdlib.h>	/* For abort().  */

#ifndef INLINE_SYSCALL
#define INLINE_SYSCALL(name, nr, args...) __syscall_##name (args)
#endif

#if defined (__i386__)
#include "sysdeps/i386/useldt.h"
#elif defined (__ia64__)
#include "sysdeps/ia64/useldt.h"
#elif defined (__powerpc__)
#include "sysdeps/powerpc/useldt.h"
#elif defined (__s390__)
#include "sysdeps/s390/useldt.h"
#else
#error useldt.h: Unknown platform, cannot optimize
#endif

#endif				/* __ASSEMBLER__ */

#endif				/* __ngpt_useldt_h__ */
