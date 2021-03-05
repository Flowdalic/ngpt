/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
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
**  sigthread.h: POSIX Thread ("Pthread") API for NGPT
*/
                             /* ``Only those who attempt the absurd
                                  can achieve the impossible.''
                                               -- Unknown          */
#if !defined _BITS_TYPES_H && !defined _PTHREAD_H
# error "Never include <bits/sigthread.h> directly; use <pthread.h> instead."
#endif

#ifndef _BITS_SIGTHREAD_H
#define _BITS_SIGTHREAD_H	1

extern int       pthread_sigmask __P((int, const sigset_t *, sigset_t *));
extern int       pthread_kill __P((pthread_t, int));

#endif	/* bits/sigthread.h */
