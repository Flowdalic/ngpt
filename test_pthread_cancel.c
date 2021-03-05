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
**  test_pthread_cancel.c: Pth test program (pthread API)
*/

#ifdef GLOBAL
#include <pthread.h>
#else
#define _PTHREAD_PRIVATE
#include "pthread.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>


static int counter;
void err_abort(int status, char *where);
void *thread_routine(void * arg);

/*
 * Loop until cancelled. The thread can be cancelled only
 * when it calls pthread_testcancel, which it does each 1000
 * iterations.
 */
void *thread_routine(void *arg)
{
    fprintf(stderr, "thread_routine: starting\n");
    for (counter = 0;; counter++)
	if ((counter % 1000) == 0) {
	    fprintf(stderr, "thread_routine: calling testcancel\n");
	    pthread_testcancel();
	}
}

void err_abort(int status, char *where)
{
    fprintf(stderr, "Aborting program in %s, status code = %d\n", where, status);
    abort();
}

int main(int argc, char *argv[])
{
    pthread_t thread_id;
    void *result;
    int status;

    fprintf(stderr, "main: creating thread\n");
    status = pthread_create(&thread_id, NULL, thread_routine, NULL);
    if (status != 0)
	err_abort(status, "main: Create thread");
    sleep(2);

    fprintf(stderr, "main: calling cancel\n");
    status = pthread_cancel(thread_id);
    if (status != 0)
	err_abort(status, "main: Cancel thread");

    fprintf(stderr, "main: calling join\n");
    status = pthread_join(thread_id, &result);
    if (status != 0)
	err_abort(status, "main: Join thread");
    if (result == PTHREAD_CANCELED)
	fprintf(stderr, "main: Thread cancelled at iteration %d\n",
		counter);
    else
	fprintf(stderr, "main: Thread was not cancelled\n");
    return 0;
}
