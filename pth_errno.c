/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Portions Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
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
**  pth_errno.c: Pth errno support
*/
                             /* Steinbach's Guideline for Systems Programming:
                                ``Never test for an error condition
                                  you don't know how to handle.''            */
#include "pth_p.h"

#if cpp

/* enclose errno in a block */
#define errno_shield(___x) \
    { \
        int __saved_errno = errno; \
        ___x \
        errno = __saved_errno; \
    }

/* return plus setting an errno value */
#if defined(PTH_DEBUG)
#define return_errno(return_val,errno_val) \
        do { errno = (errno_val); \
             pth_debug4("return 0x%lx with errno %d(\"%s\")", \
                        (unsigned long)(return_val), (errno), strerror((errno))); \
             return (return_val); } while (0)
#else
#define return_errno(return_val,errno_val) \
        do { errno = (errno_val); return (return_val); } while (0)
#endif
/*begin ibm*/
#define return_error(return_val) \
        do { return (return_val); } while (0)
/*end ibm*/


#endif /* cpp */

intern int pth_errno_storage = 0;
/* intern int pth_errno_flag    = 0; */

