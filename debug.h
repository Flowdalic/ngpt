
  /* -*-c-*- NGPT: Helpers for debugging output
  ** 
  ** $Id: debug.h,v 1.5 2002/11/15 13:35:38 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
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
  **
  **
  ** *debugmsg() are used to produce debugging output; they only 
  ** produce output when NGPT_DEBUG is #defined. *fdebugmsg()
  ** produce output when NGPT_DEBUG is #defined and flag is different
  ** to zero.
  **
  ** Note a good compiler will optimize out all the code in
  ** *fdebugmsg() if the flag is zero [and a constant], because it is
  ** an inline. Also, then NGPT_DEBUG is #undefined, all the code
  ** should be optimized out. NGPT_DEBUG acts as the MAIN SWITCH.
  **
  ** The idea behind this is that you can #define different flags to
  ** be 0 or 1, and thus deactivate/activate debugging output
  ** selectively for different areas or a combination of them, without
  ** activating all or nothing.
  **
  ** The declarations suck, so here is a fast summary:
  **
  ** [__][v][f]debugmsg[_int] ([flag,] [location,] fmt, args)
  **
  ** [__]   If present, unprotected [no lock involved]
  **
  ** [v]    If present, va_list of args, not the manual one (...)
  **
  ** [f]    If present, add [flag,] argument; if 0, it will be
  **        discarded, if 1, output will be produced. Compilers
  **        should optimize this out when flag is a constant 0. 
  **
  ** [_int] If present, add [location,] manually. Location commonly is
  **        FILE:LINE, automatically added when "_int" is not there.
  */

#ifndef __ngpt_debug_h__
#define __ngpt_debug_h__

#define NGPT_DEBUG        /* Enable or disable output? */
#define NGPT_DUMP_STACK 1 /* [switch 0/1] disable/enable stack dump */

#include <stddef.h>
#include <stdarg.h>
#ifdef NGPT_DEBUG
#include <unistd.h>
#endif
#if NGPT_DUMP_STACK == 1
#include <execinfo.h>
#endif
#include "syscall.h"
#include <sys/syscall.h>  /* System call codes */

#define __S(a) #a
#define _S(a) __S(a)

  /* Initialize debugging */

extern void debug_initialize(void);
extern void debug_shutdown(void);

  /* Unprotected - unlocked - debug output
  **
  ** If you want no location, append _int and you got it :)
  */

#define __debugmsg(fmt...) \
  do { __debugmsg_int (__FILE__ ":" _S(__LINE__), fmt); } while (0)
#define __vdebugmsg(fmt, args) \
  do { __vdebugmsg_int (__FILE__ ":" _S(__LINE__), fmt, args); } while (0)
#define __fdebugmsg(flag, fmt...) \
  do { if (flag) __debugmsg_int (__FILE__ ":" _S(__LINE__), fmt); } while (0)
#define __fvdebugmsg(flag, fmt, args) \
  do { if (flag) __vdebugmsg_int (__FILE__ ":" _S(__LINE__), fmt, args); } while (0)

  /* Protected - locked - debug output */

extern void vdebugmsg_int(const char *location, const char *fmt,
			  va_list args)
    __attribute__ ((format(printf, 2, 0)));

#define debugmsg(fmt...) \
  do { debugmsg_int (__FILE__ ":" _S(__LINE__), fmt); } while (0)
#define vdebugmsg(fmt, args) \
  do { vdebugmsg_int (__FILE__ ":" _S(__LINE__), fmt, args); } while (0)
#define fdebugmsg(flag, fmt...) \
  do { if (flag) debugmsg_int (__FILE__ ":" _S(__LINE__), fmt); } while (0)
#define fvdebugmsg(flag, fmt, args) \
  do { if (flag) vdebugmsg_int (__FILE__ ":" _S(__LINE__), fmt, args); } while (0)

  /* Unprotected w/o location debug output */

extern int __pth_vsnprintf(char *, size_t, const char *, va_list)
    __attribute__ ((format(printf, 3, 0)));
extern int __pth_snprintf(char *, size_t, const char *, ...)
    __attribute__ ((format(printf, 3, 4)));

static __inline__
    void __vdebugmsg_int(const char *location, const char *fmt,
			 va_list args)
    __attribute__ ((format(printf, 2, 0)));

extern int debug_fd;
extern int __libc_write(int, const void *, size_t);

static __inline__
void __vdebugmsg_int(const char *location, const char *fmt, va_list args)
{
#ifdef NGPT_DEBUG
    int count;
    char b[2048];

    if (location != NULL)
      count = __pth_snprintf(b, sizeof(b), "%s: %d:%d ", location,
                             (int) getpid(), (int) k_gettid());
    else
      count = 0;
    count += __pth_vsnprintf(b + count, sizeof(b) - count, fmt, args);
    if (count > 0) {
	ngpt_SYSCALL (write, 3, debug_fd, b, count);
    }
#endif				/* #ifdef NGPT_DEBUG */
}

extern void __debugmsg_int(const char *location, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));

extern void debugmsg_int(const char *location, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));


    /* Generate an hex stack dump.
    **
    ** Will only work if NGPT_DUMP_STACK is != 0
    */

#define STACK_DUMP_FRAME_MAX 64
  
static __inline__
void stack_dump (void)
{
#if NGPT_DUMP_STACK == 0
    return;
#else
    int frames, cnt;
    void *frame[STACK_DUMP_FRAME_MAX];
    frames = backtrace (frame, STACK_DUMP_FRAME_MAX);
    __debugmsg_int (NULL, "Stack dump [%d frames]: ", frames);
    for (cnt = 0; cnt < frames; cnt++)
        __debugmsg_int (NULL, "[%d] %p, ", cnt, frame[cnt]);
#endif /* NGPT_DUMP_STACK == 0 */
}

#endif				/* #ifndef __ngpt_debug_h__ */
