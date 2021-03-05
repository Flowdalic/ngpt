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
**   test_str03c.c: Create shared memory file for shared mutexes.
*/
                             /* ``It is hard to fly with
                                  the eagles when you work
                                  with the turkeys.''
                                          -- Unknown  */
#ifdef GLOBAL
#include <pthread.h>
#else
#define _PTHREAD_PRIVATE
#include "pthread.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <memory.h>
#include <sys/stat.h>

typedef struct shared_area shared_area_t;
struct shared_area {
    int		    initialized;
    int		    locked;
    pthread_mutex_t mutex;
};

int main(int argc, char *argv[])
{
    int fd = 0;
    int sz = sizeof(struct shared_area);
    char *buff;

    unlink("test_str03m");
    
    fd = open("test_str03m", O_CREAT | O_RDWR, (S_IRWXU | S_IRWXG | S_IRWXO));
    if (fd < 0) {
	perror("Open file");
	exit(1);
    }

    buff = (char *)calloc(1, sz);
    if (buff == NULL) {
	perror("calloc");
	exit(1);
    }

    write(fd, buff, sz);

    close(fd);

    fprintf(stderr,"Complete\n");

    exit(1);
}
