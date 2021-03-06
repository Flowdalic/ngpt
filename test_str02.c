/*
 *
 *   Copyright (c) International Business Machines  Corp., 2001
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 *  FILE        : pth_str01.c
 *  DESCRIPTION : Create n threads
 *  HISTORY:
 *    05/16/2001 Paul Larson (plars@us.ibm.com)
 *      -Ported
 *
 */

#ifdef GLOBAL
#include <pthread.h>
#else
#define _PTHREAD_PRIVATE
#include "pthread.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>


/* Defines
 *
 * DEFAULT_NUM_THREADS: Default number of threads to create,
 * user can specifiy with [-n] command line option.
 *
 * USAGE: usage statement
 */

#define DEFAULT_NUM_THREADS 		100
#define USAGE	"\nUsage: %s [-l | -n num_threads] [-d]\n\n" \
		"\t-l		     Test as many as threads as possible\n" \
		"\t-n num_threads    Number of threads to create\n" \
		"\t-d                Debug option\n\n"

/*
 * Function prototypes
 *
 * sys_error (): System error message function
 * error (): Error message function
 * parse_args (): Parses command line arguments
 */

static void sys_error (const char *, int);
static void error (const char *, int);
static void parse_args (int, char **);
void *thread (void *);

/*
 * Global Variables
 */

int num_threads = DEFAULT_NUM_THREADS;
int test_limit  = 0;
int debug       = 0;


/*---------------------------------------------------------------------+
|                               main ()                                |
| ==================================================================== |
|                                                                      |
| Function:  Main program  (see prolog for more details)               |
|                                                                      |
+---------------------------------------------------------------------*/
int main (int argc, char **argv)
{
	/*
	 * Parse command line arguments and print out program header
	 */
	parse_args (argc, argv);

        if(test_limit) {
	  printf ("\n    Creating as many threads as possible\n\n");
        } else {
	  printf ("\n    Creating %d threads\n\n", num_threads);
        }
	thread (0);	

	/* 
	 * Program completed successfully...
	 */
	printf ("\ndone...\n\n");
	fflush (stdout);
	exit (0);
}


/*---------------------------------------------------------------------+
|                               thread ()                              |
| ==================================================================== |
|                                                                      |
| Function:  Recursively creates threads while num < num_threads       |
|                                                                      |
+---------------------------------------------------------------------*/
void *thread (void *parm)
{
	int num = (int) parm;
	pthread_t	th;
	pthread_attr_t	attr;
	int 		rc;

	if (debug) {
		printf ("\tThread [%d]: new\n", num);
		fflush (stdout);
	}

	/*
	 * Create threads while num < num_threads...
	 */
	if (test_limit || (num < num_threads)) {

		if (pthread_attr_init (&attr))
			sys_error ("pthread_attr_init failed", __LINE__);
		if (pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE))
			sys_error ("pthread_attr_setdetachstate failed", __LINE__);
		if ((rc = pthread_create (&th, &attr, thread, (void *)(num + 1)))) {
			if (test_limit) {
			   printf ("Testing pthread limit, %d pthreads created.\n", num);
			   pthread_exit(0);
			}
			if (rc == EAGAIN) {
			    fprintf (stderr, "Thread [%d]: unable to create more threads!\n", num);
			    return NULL;
			}
			else 
			    sys_error ("pthread_create failed", __LINE__);
		}
		if (test_limit && ((num % 1000) == 0))
		    printf ("Testing pthread limit: %d pthreads created so far...\n", num);
		pthread_join (th, (void *) NULL);
	}
	if (debug) {
		printf ("\tThread [%d]: done\n", num);
		fflush (stdout);
	}

	pthread_exit(0);

	/*NOTREACHED*/
	return 0;
}


/*---------------------------------------------------------------------+
|                             parse_args ()                            |
| ==================================================================== |
|                                                                      |
| Function:  Parse the command line arguments & initialize global      |
|            variables.                                                |
|                                                                      |
+---------------------------------------------------------------------*/
static void parse_args (int argc, char **argv)
{
	int	i;
	int	errflag = 0;
	char	*program_name = *argv;

	while ((i = getopt(argc, argv, "dln:?")) != EOF) {
		switch (i) {
			case 'd':		/* debug option */
				debug++;
				break;
			case 'l':		/* test pthread limit */
				test_limit++;
				break;
			case 'n':		/* number of threads */
				num_threads = atoi (optarg);
				break;
			case '?':
				errflag++;
				break;
		}
	}

	/* If any errors exit program */
	if (errflag) {
		fprintf (stderr, USAGE, program_name);
		exit (2);
	}
}


/*---------------------------------------------------------------------+
|                             sys_error ()                             |
| ==================================================================== |
|                                                                      |
| Function:  Creates system error message and calls error ()           |
|                                                                      |
+---------------------------------------------------------------------*/
static void sys_error (const char *msg, int line)
{
	char syserr_msg [256];

	sprintf (syserr_msg, "%s: %s\n", msg, strerror (errno));
	error (syserr_msg, line);
}


/*---------------------------------------------------------------------+
|                               error ()                               |
| ==================================================================== |
|                                                                      |
| Function:  Prints out message and exits...                           |
|                                                                      |
+---------------------------------------------------------------------*/
static void error (const char *msg, int line)
{
	fprintf (stderr, "ERROR [line: %d] %s\n", line, msg);
	exit (-1);
}
