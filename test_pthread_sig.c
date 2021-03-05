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
**  test_pthread_sig.c: Pth test program (pthread API)
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


#define MAX_NUM_THREADS  10

void sigcatcher(int);
void *cause_sig_sync(void *);


void sigcatcher(int sig)
{
    fprintf(stderr, "The BUGCATCHER caught signal %d\n", sig);
    pthread_exit(0);
}

void *cause_sig_sync(void *p)
{
    int i, id = 1;
    sigset_t sigs_to_catch;

    /* Identify our thread */
    fprintf(stderr, "cause_sig_sync: running in thread 0x%x\n", (int)pthread_self());

#if 1
    /* set this thread's signal mask to block out all other signals */
    sigemptyset(&sigs_to_catch);
    sigaddset(&sigs_to_catch, SIGSEGV);
    sigaddset(&sigs_to_catch, SIGBUS);
    pthread_sigmask(SIG_UNBLOCK, &sigs_to_catch, NULL);
#endif

    /* Loop simulating useful processing in this thread */
    for (i = 1; i == i; i++) {
	if (i % 100 == 0) {
	    fprintf(stderr, "cause_sig_sync: printing count: %4d\n", i);
	    /* id = *(int *)p; Guaranteed bad address */
	    *(int *) p = id;	/* Guaranteed bad address */
	}
    }

    return (NULL);
}

extern int main(void)
{
    int i;
    pthread_t threads[MAX_NUM_THREADS];
    int num_threads = 0;
    sigset_t sigs_to_block;
    struct sigaction action;


    /* Identify our thread */
    fprintf(stderr, "main: running in thread 0x%x\n", (int)pthread_self());

    /* 
     * Set this thread's signal mask to block out all other signals
     * Other thread's will inherit the mask
     */

    sigfillset(&sigs_to_block);
    pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);
    /* Set signal handler for catching SIGSEGV and SIGBUS */
#if 1
    action.sa_handler = sigcatcher;
    action.sa_flags = 0;
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
#endif

    /* spawn the threads */

    /* Make sure we can catch synchronous signals as exceptions */
    pthread_create(&threads[num_threads++], NULL, cause_sig_sync, NULL);


    fprintf(stderr, "main: %d threads created\n", num_threads);

    /* wait until all threads have finished */
    for (i = 0; i < num_threads; i++) {
	pthread_join(threads[i], NULL);
	fprintf(stderr, "main: joined to thread %d \n", i);
    }

    fprintf(stderr, "main: all %d threads have finished. \n", num_threads);

    return 0;
}
