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
 *  DESCRIPTION : create a tree of threads
 *  HISTORY:
 *    04/09/2001 Paul Larson (plars@us.ibm.com)
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
#include <errno.h>
#include <sys/types.h>
#include "test_str.h"

int	depth = 3;
int	breadth = 4;
int	timeout = 30;			/* minutes */
int	cdepth;				/* current depth */
int	debug = 0;

c_info		*child_info;		/* pointer to info array */
int		node_count;		/* number of nodes created so far */
pthread_mutex_t	node_mutex;		/* mutex for the node_count */
pthread_cond_t	node_condvar;		/* condition variable for node_count */
pthread_attr_t	attr;			/* attributes for created threads */

int num_nodes(int, int);
int synchronize_children(c_info *);
int doit(c_info *);

/*
 * parse_args
 *
 * Parse command line arguments.  Any errors cause the program to exit
 * at this point.
 */
static void	parse_args( int argc, char *argv[] )
{
	int		opt, errflag = 0;
	int		bflag = 0, dflag = 0, tflag = 0;

	while ( (opt = getopt( argc, argv, "b:d:t:Dh?" )) != EOF ) {
	    switch ( opt ) {
		case 'b':
		    if ( bflag )
			errflag++;
		    else {
			bflag++;
			breadth = atoi( optarg );
			if ( breadth <= 0 )
			    errflag++;
		    }
		    break;
		case 'd':
		    if ( dflag )
			errflag++;
		    else {
			dflag++;
			depth = atoi( optarg );
			if ( depth <= 0 )
			    errflag++;
		    }
		    break;
		case 't':
		    if ( tflag )
			errflag++;
		    else {
			tflag++;
			timeout = atoi( optarg );
			if ( timeout <= 0 )
			    errflag++;
		    }
		    break;
		case 'D':
		    debug = 1;
		    break;
		case 'h':
		default:
		    errflag++;
		    break;
	    }
	}

	if ( errflag ) {
		fprintf( stderr, "usage: %s [-b <num>] [-d <num>] [-t <num>] [-D]", argv[0] );
		fprintf( stderr, " where:\n" );
		fprintf( stderr, "\t-b <num>\tbreadth of child nodes\n" );
		fprintf( stderr, "\t-d <num>\tdepth of child nodes\n" );
		fprintf( stderr, "\t-t <num>\ttimeout for child communication (in minutes)\n" );
		fprintf( stderr, "\t-D\t\tdebug mode on\n" );
		exit( 1 );
	}

}

/*
 * num_nodes
 *
 * Caculate the number of child nodes for a given breadth and depth tree.
 */
int	num_nodes( int b, int d )
{
	int	n, sum = 1, partial_exp = 1;

	/*
	 * The total number of children = sum ( b ** n )
	 *                                n=0->d
	 * Since b ** 0 == 1, and it's hard to compute that kind of value
	 * in this simplistic loop, we start sum at 1 (above) to compensate
	 * and do the computations from 1->d below.
	 */
	for ( n = 1; n <= d; n++ ) {
		partial_exp *= b;
		sum += partial_exp;
	}

	return( sum );
}

/*
 * synchronize_children
 *
 * Register the child with the parent and then wait for all of the children
 * at the same level to register also.  Return when everything is synched up.
 */
int	synchronize_children( c_info *parent ) {
	int		my_index, rc;
	c_info		*info_p;
	struct timespec	timer;

	if ( debug ) {
	    printf( "trying to lock node_mutex\n" );
	    fflush( stdout );
	}

	/*
	 * Lock the node_count mutex to we can safely increment it.  We
	 * will unlock it when we broadcast that all of our siblings have
	 * been created or when we block waiting for that broadcast.
	 */
	pthread_mutex_lock( &node_mutex );
	my_index = node_count++;

	printf( "thread %d started\n", my_index );
	fflush( stdout );

	/*
	 * Get a pointer into the array of thread structures which will
	 * be "me".
	 */
	info_p = &child_info[my_index];
	info_p->index = my_index;

	if ( debug ) {
	    printf( "thread %d info_p=%x\n", my_index, (unsigned int)info_p );
	    fflush( stdout );
	}

	/*
	 * Register with parent bumping the parent's child_count variable.
	 * Make sure we have exclusive access to that variable before we
	 * do the increment.
	 */
	if ( debug ) {
	    printf( "thread %d locking child_mutex %x\n", my_index,
	      (unsigned int)&parent->child_mutex );
	    fflush( stdout );
	}
	pthread_mutex_lock( &parent->child_mutex );
	if ( debug ) {
	    printf( "thread %d bumping child_count (currently %d)\n",
	      my_index, parent->child_count );
	    fflush( stdout );
	}
	parent->child_ptrs[parent->child_count++] = info_p;
	if ( debug ) {
	    printf( "thread %d unlocking child_mutex %x\n", my_index,
	      (unsigned int)&parent->child_mutex );
	    fflush( stdout );
	}
	pthread_mutex_unlock( &parent->child_mutex );

	if ( debug ) {
	    printf( "thread %d node_count = %d\n", my_index, node_count );
	    printf( "expecting %d nodes\n", num_nodes(breadth, cdepth) );
	    fflush( stdout );
	}

	/*
	 * Have all the nodes at our level in the tree been created yet?
	 * If so, then send out a broadcast to wake everyone else up (to let
	 * them know they can now create their children (if they are not
	 * leaf nodes)).  Otherwise, go to sleep waiting for the broadcast.
	 */
	if ( node_count == num_nodes(breadth, cdepth) ) {

	    /*
	     * Increase the current depth variable, as the tree is now
	     * fully one level taller.
	     */
	    if ( debug ) {
		printf( "thread %d doing cdepth++ (%d)\n", my_index, cdepth );
		fflush( stdout );
	    }
	    cdepth++;

	    if ( debug ) {
		printf( "thread %d sending child_mutex broadcast\n", my_index );
		fflush( stdout );
	    }
	    /*
	     * Since all of our siblings have been created, this level of
	     * the tree is now allowed to continue its work, so wake up the
	     * rest of the siblings.
	     */
	    pthread_cond_broadcast( &node_condvar );

	} else {

	    /*
	     * In this case, not all of our siblings at this level of the
	     * tree have been created, so go to sleep and wait for the
	     * broadcast on node_condvar.
	     */
	    if ( debug ) {
		printf( "thread %d waiting for siblings to register\n",
		  my_index );
		fflush( stdout );
	    }
	    time( &timer.tv_sec );
	    timer.tv_sec += (unsigned long)timeout * 60;
            timer.tv_nsec = (unsigned long)0;
	    if ((rc = pthread_cond_timedwait(&node_condvar, &node_mutex,
	      &timer))) {
		fprintf( stderr, "pthread_cond_timedwait (sync) %d: %s\n",
		    my_index, strerror(rc) );
		exit( 2 );
	    }

	    if ( debug ) {
		printf( "thread %d is now unblocked\n", my_index );
		fflush( stdout );
	    }

	}

	/*
	 * Unlock the node_mutex lock, as this thread is finished
	 * initializing.
	 */
	if ( debug ) {
	    printf( "thread %d unlocking node_mutex\n", my_index );
	    fflush( stdout );
	}
	pthread_mutex_unlock( &node_mutex );
	if ( debug ) {
	    printf( "thread %d unlocked node_mutex\n", my_index );
	    fflush( stdout );
	}

	if ( debug ) {
	    printf( "synchronize_children returning %d\n", my_index );
	    fflush( stdout );
	}

	return( my_index );

}

/*
 * doit
 *
 * Do it.
 */
int	doit( c_info *parent ) {
	int		rc, child, my_index;
	void		*status;
	c_info 		*info_p;
	struct timespec	timer;

	if ( debug ) {
	    printf( "parent=%#010x\n", (unsigned int)parent );
	    fflush( stdout );
	}

	if ( parent != NULL ) {
	    /*
	     * Synchronize with our siblings so that all the children at
	     * a given level have been created before we allow those children
	     * to spawn new ones (or do anything else for that matter).
	     */
	    if ( debug ) {
		printf( "non-root child calling synchronize_children\n" );
		fflush( stdout );
	    }
	    my_index = synchronize_children( parent );
	    if ( debug ) {
		printf( "non-root child has been assigned index %d\n",
		  my_index );
		fflush( stdout );
	    }
	} else {
	    /*
	     * The first thread has no one with which to synchronize, so
	     * set some initial values for things.
	     */
	    if ( debug ) {
		printf( "root child\n" );
		fflush( stdout );
	    }
	    cdepth = 1;
	    my_index = 0;
	    node_count = 1;
	}

	/*
	 * Figure out our place in the pthread array.
	 */
	info_p = &child_info[my_index];

	if ( debug ) {
	    printf( "thread %d getting to heart of doit.\n", my_index );
	    printf( "info_p=%x, cdepth=%d, depth=%d\n", (unsigned int)info_p, cdepth, depth );
	    fflush( stdout );
	}

	if ( cdepth <= depth ) { 

	    /*
	     * Since the tree is not yet complete (it is not yet tall enough),
	     * we need to create another level of children.
	     */

	    printf( "thread %d creating kids, cdepth=%d\n", my_index, cdepth );
	    fflush( stdout );

	    /*
	     * Create breadth children.
	     */
	    for ( child = 0; child < breadth; child++ ) {
		if ( debug ) {
		    printf( "thread %d making child %d, ptr=%x\n", my_index,
		      child, (unsigned int)&(info_p->threads[child]) );
		    fflush( stdout );
		}
		if ((rc = pthread_create(&(info_p->threads[child]), &attr,
		  (void *)doit, (void *)info_p))) {
		    fprintf( stderr, "pthread_create (doit): %s\n",
		      strerror(rc) );
		    exit( 3 );
		} else {
		    if ( debug ) {
			printf( "pthread_create made thread %x\n",
			  (unsigned int)&(info_p->threads[child]) );
			fflush( stdout );
		    }
		}
	    }

	    if ( debug ) {
		printf( "thread %d waits on kids, cdepth=%d\n", my_index,
		    cdepth );
		fflush( stdout );
	    }

	    /*
	     * Wait for our children to finish before we exit ourselves.
	     */
	    for ( child = 0; child < breadth; child++ ) {
		if ( debug ) {
		    printf( "attempting join on thread %x\n",
		      (unsigned int)&(info_p->threads[child]) );
		    fflush( stdout );
		}
		if ((rc = pthread_join((info_p->threads[child]), &status))) {
		    if ( debug ) {
			fprintf( stderr,
			  "join failed on thread %d, status addr=%x: %s\n",
			  my_index, (unsigned int)status, strerror(rc) );
			fflush( stderr );
		    }
		    exit( 4 );
		} else {
		    if ( debug ) {
			printf( "thread %d joined child %d ok\n", my_index,
			  child );
			fflush( stdout );
		    }
		}
	    }

	} else {

	    /*
	     * This is the leaf node case.  These children don't create
	     * any kids; they just talk amongst themselves.
	     */
	    printf( "thread %d is a leaf node, depth=%d\n", my_index, cdepth );
	    fflush( stdout );

	    /*
	     * Talk to siblings (children of the same parent node).
	     */
	    if ( breadth > 1 ) {

		for ( child = 0; child < breadth; child++ ) {
		    /*
		     * Don't talk to yourself.
		     */
		    if ( parent->child_ptrs[child] != info_p ) {
			if ( debug ) {
			    printf( "thread %d locking talk_mutex\n",
			      my_index );
			    fflush( stdout );
			}
			pthread_mutex_lock(
			  &(parent->child_ptrs[child]->talk_mutex) );
			if ( ++parent->child_ptrs[child]->talk_count
			  == (breadth - 1) ) {
			    if ( debug ) {
				printf( "thread %d talk siblings\n", my_index );
				fflush( stdout );
			    }
			    if ((rc = pthread_cond_broadcast(
			      &parent->child_ptrs[child]->talk_condvar))) {
				fprintf( stderr, "pthread_cond_broadcast: %s\n",
				  strerror(rc) );
				exit( 5 );
			    }
			}
			if ( debug ) {
			    printf( "thread %d unlocking talk_mutex\n",
			      my_index );
			    fflush( stdout );
			}
			pthread_mutex_unlock(
			  &(parent->child_ptrs[child]->talk_mutex) );
		    }
		}

		/*
		 * Wait until the breadth - 1 siblings have contacted us.
		 */
		if ( debug ) {
		    printf( "thread %d waiting for talk siblings\n",
		      my_index );
		    fflush( stdout );
		}

		pthread_mutex_lock( &info_p->talk_mutex );
		if ( info_p->talk_count < (breadth - 1) ) {
		    time( &timer.tv_sec );
		    timer.tv_sec += (unsigned long)timeout * 60;
		    timer.tv_nsec = (unsigned long)0;
		    if ((rc = pthread_cond_timedwait(&info_p->talk_condvar,
		      &info_p->talk_mutex, &timer))) {
			fprintf( stderr,
			  "pthread_cond_timedwait (leaf) %d: %s\n",
			  my_index, strerror(rc) );
			exit( 6 );
		    }
		}
		pthread_mutex_unlock( &info_p->talk_mutex );

	    }

	}

	/*
	 * Our work is done.  We're outta here.
	 */
	printf( "thread %d exiting, depth=%d, status=%d, addr=%x\n", my_index,
	  cdepth, info_p->status, (unsigned int)info_p);
	fflush( stdout );

	pthread_exit( 0 );

	/*NOTREACHED*/
	return 0;
		
}

/*
 * main
 */
int	main( int argc, char *argv[] ) {
	int		rc, ind, total;
	pthread_t	root_thread;

	parse_args( argc, argv );

	/*
	 * Initialize node mutex.
	 */
	if ((rc = pthread_mutex_init(&node_mutex, NULL))) {
		fprintf( stderr, "pthread_mutex_init(node_mutex): %s\n",
		    strerror(rc) );
		exit( 7 );
	}

	/*
	 * Initialize node condition variable.
	 */
	if ((rc = pthread_cond_init(&node_condvar, NULL))) {
		fprintf( stderr, "pthread_cond_init(node_condvar): %s\n",
		    strerror(rc) );
		exit( 8 );
	}

	/*
	 * Allocate pthread info structure array.
	 */
	total = num_nodes( breadth, depth );
	printf( "Allocating %d nodes.\n", total );
	fflush( stdout );
	if ( (child_info = (c_info *)malloc( total * sizeof(c_info) ))
	    == NULL ) {
		perror( "malloc child_info" );
		exit( 10 );
	}
	bzero( child_info, total * sizeof(c_info) );

	if ( debug ) {
		printf( "Initializing array for %d children\n", total );
		fflush( stdout );
	}

	/*
	 * Allocate array of pthreads descriptors and initialize variables.
	 */
	for ( ind = 0; ind < total; ind++ ) {

		if ( (child_info[ind].threads =
		    (pthread_t *)malloc( breadth * sizeof(pthread_t) ))
		    == NULL ) {
			perror( "malloc threads" );
			exit( 11 );
		}
		bzero( child_info[ind].threads, breadth * sizeof(pthread_t) );
		
		if ( (child_info[ind].child_ptrs =
		    (c_info **)malloc( breadth * sizeof(c_info *) )) == NULL ) {
			perror( "malloc child_ptrs" );
			exit( 12 );
		}
		bzero( child_info[ind].child_ptrs,
		    breadth * sizeof(c_info *) );
		
		if ((rc = pthread_mutex_init(&child_info[ind].child_mutex,
		    NULL))) {
			fprintf( stderr, "pthread_mutex_init child_mutex: %s\n",
			    strerror(rc) );
			exit( 13 );
		}

		if ((rc = pthread_mutex_init(&child_info[ind].talk_mutex,
		    NULL))) {
			fprintf( stderr, "pthread_mutex_init talk_mutex: %s\n",
			    strerror(rc) );
			exit( 14 );
		}
		
		if ((rc = pthread_cond_init(&child_info[ind].child_condvar,
		    NULL))) {
			fprintf( stderr,
			    "pthread_cond_init child_condvar: %s\n",
			    strerror(rc) );
			exit( 15 );
		}

		if ((rc = pthread_cond_init(&child_info[ind].talk_condvar,
		    NULL))) {
			fprintf( stderr, "pthread_cond_init talk_condvar: %s\n",
			    strerror(rc) );
			exit( 16 );
		}

		if ( debug ) {
			printf( "Successfully initialized child %d.\n", ind );
			fflush( stdout );
		}

	}

	printf( "Creating root thread attributes via pthread_attr_init.\n" );
	fflush( stdout );

	if ((rc = pthread_attr_init(&attr))) {
		fprintf( stderr, "pthread_attr_init: %s\n", strerror(rc) );
		exit( 17 );
	}

	/*
	 * Make sure that all the thread children we create have the
	 * PTHREAD_CREATE_JOINABLE attribute.  If they don't, then the
	 * pthread_join call will sometimes fail and cause mass confusion.
	 */
	if ((rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
	   ) {
		fprintf( stderr, "pthread_attr_setdetachstate: %s\n",
		    strerror(rc) );
		exit( 18 );
	}

	printf( "Creating root thread via pthread_create.\n" );
	fflush( stdout );

	if ((rc = pthread_create(&root_thread, &attr, (void *)doit, NULL))) {
		fprintf( stderr, "pthread_create: %s\n", strerror(rc) );
		exit( 19 );
	}

	if ( debug ) {
		printf( "Doing pthread_join.\n" );
		fflush( stdout );
	}

	/*
	 * Wait for the root child to exit.
	 */
	if (( rc = pthread_join(root_thread, NULL) )) {
		fprintf( stderr, "pthread_join: %s\n", strerror(rc) );
		exit( 20 );
	}

	if ( debug ) {
		printf( "About to pthread_exit.\n" );
		fflush( stdout );
	}

	exit( 0 );
}
