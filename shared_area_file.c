/*
**  NGPT - Next Generation POSIX Threading
**  Portions Copyright (C) 2001 Free Software Foundation, Inc.
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
**  shm_open.c: Open a POSIX shared memory object.  Generic POSIX file version.
*/
                             /* ``The nice thing about standards is that
                                  there are so many to choose from.  And if
                                  you really don't like all the standards you
                                  just have to wait another year until the one
                                  arises you are looking for'' 
                                                 -- Tannenbaum, 'Introduction
                                                    to Computer Networks' */

#include "pth_p.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif

#ifndef _PATH_VARRUN
#define _PATH_VARRUN "/dev/"
#endif
#define SHMDIR	(_PATH_VARRUN "shm/")
#define PTH_SHARED_FILENAME	    "/ngpt"

static char pth_shared_pathname[PATH_MAX] = {0};

intern char *get_pth_shared_pathname()
{
    if (pth_shared_pathname[0] == 0)
	sprintf(pth_shared_pathname, "%s%s.%x", SHMDIR, PTH_SHARED_FILENAME,
					(PTH_INTERNAL_VERSION >> 8) & 0xff);
    return pth_shared_pathname;
}

/* Open shared memory object.  */
int shared_area_file_open (int oflag, mode_t mode)
{
    char *fname = get_pth_shared_pathname();
    int fd, flags;

    if ((fd = open (fname, oflag, mode)) == -1)
	return (-1);

    /* Shared area file is open, now set the FD_CLOEXEC bit.  */
    if ((flags = fcntl (fd, F_GETFD, 0)) == -1) {
	/* Something went wrong.  We cannot return the descriptor.  */
	int save_errno = errno;
	close (fd);
	errno = save_errno;
	return -1;
    }

    flags |= FD_CLOEXEC;
    flags = fcntl (fd, F_SETFD, flags);
    return fd;
}

int shared_area_file_unlink(void)
{
    return unlink(get_pth_shared_pathname());
}
