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
**  pth_ext.c: Pth extensions
*/

#include "pth_p.h"

/*
 * Sfio Extension:
 *
 * We provide an Sfio discipline which can be pushed on an Sfio_t* stream
 * to use the Pth thread-aware I/O routines (pth_read/pth_write).
 */

#if PTH_EXT_SFIO

static ssize_t pth_sfio_read(Sfio_t *f, Void_t *buf, size_t n, Sfdisc_t *disc)
{
    ssize_t rv;

    rv = pth_read(sffileno(f), buf, n);
    return rv;
}

static ssize_t pth_sfio_write(Sfio_t *f, const Void_t *buf, size_t n, Sfdisc_t *disc)
{
    ssize_t rv;

    rv = pth_write(sffileno(f), buf, n);
    return rv;
}

static Sfoff_t pth_sfio_seek(Sfio_t *f, Sfoff_t addr, int type, Sfdisc_t *disc)
{
    return sfsk(f, addr, type, disc);
}

static int pth_sfio_except(Sfio_t *f, int type, Void_t* data, Sfdisc_t *disc)
{
    int rv;

    switch (type) {
        case SF_LOCKED:
        case SF_READ:
        case SF_WRITE:
        case SF_SEEK:
        case SF_NEW:
        case SF_CLOSE:
        case SF_FINAL:
        case SF_DPUSH:
        case SF_DPOP:
        case SF_DBUFFER:
        case SF_DPOLL:
        case SF_READY:
        case SF_SYNC:
        case SF_PURGE:
        default:
            rv = 0; /* perform default action */
    }
    return rv;
}

#endif /* PTH_EXT_SFIO */

Sfdisc_t *pth_sfiodisc(void)
{
#if PTH_EXT_SFIO
    Sfdisc_t *disc;

    if ((disc = (Sfdisc_t *)pth_malloc(sizeof(Sfdisc_t))) == NULL)
        return NULL;
    disc->readf   = pth_sfio_read;
    disc->writef  = pth_sfio_write;
    disc->seekf   = pth_sfio_seek;
    disc->exceptf = pth_sfio_except;
    return disc;
#else
    return NULL;
#endif /* PTH_EXT_SFIO */
}

