/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
**  Based on work by Matthew Kirkwood <matthew@hairy.beasts.org>.
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
**  sys_futex.c:    Fast userspace semaphores/mutexes
*/
                             /* ``Only those who attempt the absurd
                                  can achieve the impossible.''
                                               -- Unknown          */


#include "pth_p.h"
#include <stdint.h>
#include <sys/stat.h>

#define __NR_sys_futex __NR_futex

static void sys_futex_sighandler(int sig, siginfo_t *info, void *unused) __attribute__ ((unused));
static void sys_futex_sighandler(int sig, siginfo_t *info, void *unused)
{
    int i;
    /* 
     * This code turns out to be the easiest... 
     * All we have to do is write to first native's sigpipe and have it
     * go thru and check for events...
     */
    char c = (int)1;

    pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));

    for (i = 1; pth_native_list[i].is_used; i++) {
	if (pth_native_list[i].is_bounded && pth_native_list[i].is_bound == FALSE)
	    pth_sc(write)(pth_native_list[i].sigpipe[1], &c, sizeof(char));
    }

    /*
     * Close the fd that spawned this signal...
     */
    close(info->si_fd);
}

