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
**  ngptc.c: Create shared memory file for shared mutexes.
*/
                             /* ``It is hard to fly with
                                  the eagles when you work
                                  with the turkeys.''
                                          -- Unknown  */

#include "pth_p.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <getopt.h>
#include <errno.h>

#define MAX_SHARED_OBJECTS	64000

#define OPT_ADDR	'a'
#define OPT_SIZE	's'
#define OPT_VERBOSE	'v'
#define OPT_REPORT	'r'
#define OPT_HELP	'h'

static struct option options[] =
{
    {"verbose",	no_argument,       NULL, OPT_VERBOSE},
    {"addr",	required_argument, NULL, OPT_ADDR},
    {"help",	no_argument,       NULL, OPT_HELP},
    {"rpt",	no_argument,       NULL, OPT_REPORT},
    {"size",	required_argument, NULL, OPT_SIZE},
    {NULL  ,	0,                 NULL, 0 }
};

int main(int argc, char *argv[])
{
    int		  fd = 0;
    int		  opt;
    int		  verbose = 0;
    int		  report = 0;
    unsigned long addr;
    size_t	  num;
    size_t	  shm_sz;
    size_t	  pagesize;
    char	  *buff;

    /* Set defaults for size and addr, in case options not seen */
    num  = PTH_DEFAULT_MAX_SHARED_OBJECTS;
    addr = PTH_DEFAULT_SHARED_ADDRESS;

    /* Scan for the options */
    while ((opt = getopt_long(argc, argv, "a:hrs:v", options, NULL)) != -1)
	switch (opt)
	{
	    case OPT_HELP:
		/* TODO: Add more help later */
	    case ':':
	    case '?':
		fprintf(stdout, "usage: %s [--size n][--addr a][--rpt][--verbose][--help]\n", argv[0]);
		exit (0);
	    case OPT_REPORT:
		report++;
		break;
	    case OPT_SIZE:
		errno = 0;
		num = (size_t)strtoul(optarg, 0, 0);
		if (errno != 0) {
		    perror ("Invalid size specified");
		    exit (1);
		}
		if (num > MAX_SHARED_OBJECTS) {
		    perror ("Invalid size out of range");
		    exit (1);
		}
		break;
	    case OPT_ADDR:
		errno = 0;
		addr = (size_t)strtoul(optarg, 0, 0);
		if (errno != 0) {
		    perror ("Invalid address specified");
		    exit (1);
		}
		pagesize = getpagesize();
		if ((addr % pagesize) != 0) {
		    perror ("Invalid address not pagesize aligned");
		    exit (1);
		}
		break;
	    case OPT_VERBOSE:
		verbose++;
		break;
	}

    /* If report option do it now */
    if (report) {
	struct pth_shared_area_st file_shared_area;
	
	/* Open the shared area file */
	if ((fd = shared_area_file_open(O_RDONLY, 0)) == -1) {
	    perror("Open shared area file");
	    exit(1);
	}

	/* read it in so that we can get init params */
	if (read(fd, &file_shared_area, sizeof(file_shared_area)) == -1) {
	    perror("Can't read shared area file");
	    exit(1);
        }

	/* Check the version and bail if it's not what we expect */
	if (file_shared_area.verid != PTH_INTERNAL_VERSION) {
	    perror("shared area file version is not recognized");
	    exit(1);
	}

	printf("Shared area file: %s NGPT internal version: 0x%x\n", 
				   get_pth_shared_pathname(),
				   file_shared_area.verid);
	shm_sz = COMPUTE_PTH_SHARED_SIZE(file_shared_area.num_objs);
	printf("Shared area address %p Size %u Shared segment size %u(0x%x)\n",
				   file_shared_area.addr,
				   file_shared_area.num_objs,
				   shm_sz, shm_sz);
	exit(0);
    }

    /* Don't proceed if not root */
    if (geteuid() != 0) {
	fprintf(stderr, "%s - super-user privilige required\n", argv[0]);
	exit(-1);
    }

    /* Code below builds a new shared area */
    shared_area_file_unlink();

    shm_sz = COMPUTE_PTH_SHARED_SIZE(num);

    /* Do the open of the shared filename */
    fd = shared_area_file_open(O_CREAT | O_RDWR, (S_IRWXU | S_IRWXG | S_IRWXO));
    if (fd == -1) {
	perror("Open shared area file");
	exit(1);
    }

    /* zero out the buffer to the proper size */
    buff = (char *)calloc(1, shm_sz);
    if (buff == NULL) {
	perror("Allocating initialized shared area");
	exit(1);
    }

    /* write the number of lock objects field */
    ((struct pth_shared_area_st *)buff)->verid    = PTH_INTERNAL_VERSION;
    ((struct pth_shared_area_st *)buff)->addr     = addr;
    ((struct pth_shared_area_st *)buff)->num_objs = num;

    /* write out the pth_shared_area initialized template */
    if (write(fd, buff, shm_sz) == -1) {
	perror ("Writing shared area file");
	exit (1);
    }

    /* fix file permissions */
    if (fchmod(fd, (mode_t)0777) == -1) {
	perror ("Chmod on shared area file");
	exit (1);
    }

    close(fd);

    fprintf(stderr,"Complete.");
    if (verbose)
	fprintf(stderr," Addr %p Size %u", addr, num);
    fprintf(stderr, "\n");

    exit(0);
}
