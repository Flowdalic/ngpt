
  /* -*-c-*- NGPT: Implementation of allocation helpers
  ** 
  ** $Id: allocation.h,v 1.7 2002/10/09 14:49:49 billa Exp $
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **
  **      - Design and implementation
  **
  ** This file is part of NGPT, a non-preemptive thread scheduling
  ** library which can be found at http://www.ibm.com/developer.
  **
  ** This library is free software; you can redistribute it and/or
  ** modify it under the terms of the GNU Lesser General Public
  ** License as published by the Free Software Foundation; either
  ** version 2.1 of the License, or (at your option) any later version.
  **
  ** This library is distributed in the hope that it will be useful,
  ** but WITHOUT ANY WARRANTY; without even the implied warranty of
  ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  ** Lesser General Public License for more details.
  **
  ** You should have received a copy of the GNU Lesser General Public
  ** License along with this library; if not, write to the Free Software
  ** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  ** USA, or contact Bill Abt <babt@us.ibm.com>
  **
  ** For docs, see allocation.c
  */

#ifndef __ngpt_allocation_h__
#define __ngpt_allocation_h__

#include <pth_p.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <stddef.h>
#include <asm/page.h>
#include "list.h"
#include "spinlock.h"
#include "debug.h"

  /* Feature switches for the different areas */

#define CAL_CHECK_CONSISTENCY        0
#define CAL_CHECK_INITIALIZATION     1
#define CAL_STATISTICS               0
#define CAL_DUMP                     1

  /* Debug switches for the different areas [NGPT_DEBUG == main switch]
  **
  ** WARNING!
  **
  **   If you enable CAL_DEBUG_INITIALIZATION, ugly things can happen
  ** in the form of a SEGFAULT as soon as the program starts and the
  ** library is initalized. The problem is the debugmsg() statement
  ** for it (in debug.h) will try to save errno and thus, it will call
  ** glibc's _h_errno_location(), that in turn will try to acquire a
  ** mutex and thus, again enter the library and again, try to
  ** initialize the cache allocator, trying again to print the
  ** initialization debug messages - and the story repeats, so at the
  ** end you have a recursive, unbound call that causes the segfault.
  **
  ** So, if you need to enable CAL_DEBUG_INITIALIZATION, disable errno
  ** saving in debug.h [there is also a comment there explaining it].
  */

#define CAL_DEBUG_INITIALIZATION     0	/* See WARNING!!! */
#define CAL_DEBUG_CAL_GET            0
#define CAL_DEBUG_CAL_PUT            0
#define CAL_DEBUG_CHUNK_CACHE_GET    0
#define CAL_DEBUG_CHUNK_CACHE_PUT    0
#define CAL_DEBUG_MMAP_ALIGNMENT     0
#define CAL_DEBUG_MMAP_ALLOCATE      0
#define CAL_DEBUG_MMAP_DEALLOCATE    0
#define CAL_DEBUG_LIST               0
#define CAL_DEBUG_LNODE              0






  /* Allocation of memory via mmap
  ** -----------------------------
  **
  ** Memory is rounded to PAGE_SIZE.
  */


  /* Simple: allocate with mmap a size (size is ROUNDED up to page
  ** size by the kernel).
  **
  ** Return NULL if no memory available.
  */

static __inline__ void *mmap_allocate(size_t size)
{
    void *mchunk;
    int save_errno;

    __fdebugmsg(CAL_DEBUG_MMAP_ALLOCATE,
		"debug: %s (%lu)\n", __FUNCTION__, (unsigned long) size);

    save_errno = errno;
    mchunk = mmap(NULL, size, PROT_READ | PROT_WRITE,
		  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    errno = save_errno;
    if (mchunk == (void *) -1)
	mchunk = NULL;

    __fdebugmsg(CAL_DEBUG_MMAP_ALLOCATE,
		"debug: %s (%lu) = %p\n", __FUNCTION__,
		(unsigned long) size, mchunk);
    if (CAL_DEBUG_MMAP_ALIGNMENT
	&& ((unsigned long) mchunk & ~PAGE_MASK) != 0)
	__fdebugmsg(CAL_DEBUG_MMAP_ALIGNMENT,
		    "debug: %s (%lu) = %p NON PAGE ALIGNED\n",
		    __FUNCTION__, (unsigned long) size, mchunk);

    return mchunk;
}


  /* Even more simple: deallocate an mmap'ed area (size is rounded up
  ** to PAGE_SIZE by the kernel).
  */

static __inline__ void mmap_deallocate(void *ptr, size_t size)
{
    int save_errno = errno;
    __fdebugmsg(CAL_DEBUG_MMAP_DEALLOCATE,
		"debug: %s (%p, %lu)\n", __FUNCTION__, ptr,
		(unsigned long) size);
    munmap(ptr, size);
    errno = save_errno;
}









  /* Allocator front-end functions
  ** ----------------------------- */

  /* If a chunk is bigger than CAL_CHUNK_THRESHOLD_SIZE, it is
  ** allocated with mmap and we do not monitor it; else, we use actual
  ** chunks. */

#define CAL_CHUNK_ORDER_MAX (PAGE_SHIFT-1)
#define CAL_CHUNK_THRESHOLD_SIZE (1 << CAL_CHUNK_ORDER_MAX)


  /* The minimum size we allocate with the chunk allocator (1 <<
  ** CAL_CHUNK_ORDER_MIN). */

#define CAL_CHUNK_ORDER_MIN 4


  /* A list of chunks of the same size.
  **
  ** Getting small pieces of memory uses to be more frequent, so we
  ** use a lock-per-list.
  **
  ** Chunk lists are populated getting an mchunk_header() from
  ** mchunk_header_get_chunks(). That provides an space of memory with
  ** more chunks inside, already made a list. See chunk_cache_get().
  **
  ** mchunk_head is the head of all the mchunks for this size that are
  ** allocated. This is used to speed up cal_verify_pointer(). 
  **
  ** We keep the numbers just for statistics.
  */

struct chunk_list_st {
    spinlock_t lock;
    struct lnode_st head;
    struct lnode_st mchunk_head;
    size_t total;
    size_t used;
#if CAL_STATISTICS == 1
    size_t hits;
    size_t misses;
#endif				/* #if CAL_STATISTICS == 1 */
};


  /* A chunk cache/allocator :)
  **
  ** This is basically an array of 'struct chunk_list_st'; each
  ** correspond to chunks of a given size (1 << (index +
  ** CAL_CHUNK_ORDER_MIN)).
  */

#define CAL_CHUNK_HEADS (1 + CAL_CHUNK_ORDER_MAX - CAL_CHUNK_ORDER_MIN)

struct chunk_cache_st {
    struct chunk_list_st head[CAL_CHUNK_HEADS];
};


  /* An allocator with chunk cache and mchunk cache
  **
  ** The array stores list heads from 4 to PAGE_SHIFT order; we
  ** allocate a minimum of 16 bytes (2>>CHUNK_SHIFT_MIN). However, we
  ** need the ones for < cal_threshold_size; as cal_threshold_size > (1
  ** << PAGE_SHIFT-1), we need to add that +1 :)
  */

struct cal_st {
    struct chunk_cache_st chunk_cache;
};


  /* needed helpers */

extern void chunk_cache_put(struct cal_st *, void *, size_t);
extern void *chunk_cache_get(struct cal_st *, size_t);


  /* Provide an area of memory of the given size.
  **
  ** The area size will be either rounded up to a power of two or to
  ** the size of a page, depending.
  **
  ** Smaller sizes (smaller than cal_mchunk_threshold_size) can be
  ** cached.
  */

static __inline__ void *cal_get(struct cal_st *cal, size_t size)
{
    void *ptr = NULL;

    if (size < CAL_CHUNK_THRESHOLD_SIZE)
	ptr = chunk_cache_get(cal, size);
    else
	ptr = mmap_allocate(size);
    return ptr;
}


  /* Release usage of an area of memory
  **
  ** This puts it back into the chunk cache, updating the timestamps
  ** for each big chunk.
  */

static __inline__ void cal_put(struct cal_st *cal, void *ptr, size_t size)
{
    if (size < CAL_CHUNK_THRESHOLD_SIZE)
	chunk_cache_put(cal, ptr, size);
    else
	mmap_deallocate(ptr, size);
    return;
}


  /* The interface to NGPT */

extern struct cal_st gcal;

static __inline__ void *pth_malloc(size_t sz)
{
    return cal_get(&gcal, sz);
}

static __inline__ void pth_free_mem(void *memptr, size_t sz)
{
    cal_put(&gcal, memptr, sz);
}


extern int cal_initialize(void);
extern void cal_init(struct cal_st *);
extern void cal_fork_init(struct cal_st *);
extern int cal_verify_pointer (const struct cal_st *, const void *, size_t);
extern void cal_dump(const struct cal_st *);
extern void cal_release(struct cal_st *);
extern void cal_shutdown(void);

#endif				/* #ifndef __ngpt_allocation_h__ */
