
  /* -*-c-*- NGPT: Helpers for debugging output
  ** 
  ** $Id: debug.c,v 1.4 2002/10/09 14:51:08 billa Exp $
  **
  ** Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
  ** Portions Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
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
  ** See debug.h for documentation.
  */

#include "pth_p.h"
#include "debug.h"
#include "spinlock.h"

  /* Locked/protected debug output - da lock */

static spinlock_t debug_lock = spinlock_t_INIT_UNLOCKED;

  /* If we dump, where to? normally, stderr, or is it stdout? whatever */

int debug_fd = STDERR_FILENO;

  /* ... stdargs cannot be inlined ... */

void __debugmsg_int(const char *location, const char *fmt, ...)
{
#ifdef NGPT_DEBUG
    va_list args;
    va_start(args, fmt);
    __vdebugmsg_int(location, fmt, args);
    va_end(args);
#endif				/* #ifdef NGPT_DEBUG */
}

  /* Same old, same old */

void debugmsg_int(const char *location, const char *fmt, ...)
{
#ifdef NGPT_DEBUG
    va_list args;
    va_start(args, fmt);
    vdebugmsg_int(location, fmt, args);
    va_end(args);
#endif				/* #ifdef NGPT_DEBUG */
}

  /* The core of the locking debugging thing */

void vdebugmsg_int(const char *location, const char *fmt, va_list args)
{
    spin_lock(&debug_lock, (void *) k_gettid(), NULL);
    __vdebugmsg_int(location, fmt, args);
    spin_unlock(&debug_lock, (void *) k_gettid());
}

  /* Initialize the debugging system */

void debug_initialize(void)
{
    char *debug_file;

    debug_file = getenv("NGPT_DEBUG_PATH");
    if (debug_file != NULL)
	debug_fd = open(debug_file, O_WRONLY | O_CREAT | O_APPEND);
    if (debug_fd == -1)
	debug_fd = STDERR_FILENO;
}


  /* Shutdown the debugging system
  **
  ** Doubt we'll ever use it ... but ... */

void debug_shutdown(void)
{
    if ((debug_fd != STDERR_FILENO) && (debug_fd > 0))
	close(debug_fd);
}
