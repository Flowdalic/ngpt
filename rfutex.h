
/* -*-c-*- NGPT: Robust futex operations [global]
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
** Here lies the robust futex interface [along with futex.h and
** sys_futex.c]. This is layered out in order to be used by different
** parts of the NGPT implementation, so read on for the structure:
**
*/
                             /* ``Pardon me for not standing.''
                                 -- Groucho Marx's epitaph */


#ifndef __ngpt_rtufex_h__
#define __ngpt_rtufex_h__

#include "futex.h"


    /* The robust futex structure
    **
    ** This struct keeps information for implemeting robust futexes
    ** across the board. The main futex operations are still done on
    ** the futex member [ie: futex_acquire (&rfutex->futex)].
    */

struct rfutex_st {
    struct futex_st futex;
    unsigned pshared:1;
    unsigned type:8;
    void *parent;
};

#define rfutex_st_INIT(_type)                   \
{                                               \
    futex: futex_st_INIT_UNLOCKED,              \
    pshared: 0,                                 \
    type: (_type),                              \
    parent: NULL                                \
}


    /* Initialize a rfutex
    **
    ** Need to do the futex_init() at the end, as it will call
    ** __futex_commit().
    **
    ** param rfutex Pointer to the futex structure
    **
    ** param parent Owner of the futex
    **
    ** param pshared !0 if shared
    */

static __inline__
void rfutex_init (struct rfutex_st *rfutex, void *parent, int pshared)

{
    rfutex->pshared = pshared;
    rfutex->parent = parent;
    futex_init (&rfutex->futex);
}


    /* Destroy a rfutex
    **
    ** Need to do the futex_init() at the end, as it will call
    ** __futex_commit().
    **
    ** param rfutex Pointer to the futex structure
    **
    ** param parent Owner (FIXME?) of the futex
    **
    ** param pshared !0 if shared
    */

static __inline__
void rfutex_destroy (struct rfutex_st *rfutex)
{
    rfutex->pshared = 0;
    rfutex->parent = NULL;
    futex_destroy (&rfutex->futex);
}

#endif
