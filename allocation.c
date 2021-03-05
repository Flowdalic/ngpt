
  /* -*-c-*- NGPT: Implementation of allocation helpers
  ** 
  ** $Id: allocation.c,v 1.8 2002/11/14 15:23:50 billa Exp $
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
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
  **
  ** This is a caching allocator layered in three (guess what?) layers:
  **
  ** - memory chunks smaller than 'cal_threshold_size' [chunks]
  **
  ** - memory chunks bigger than 'cal_threshold_size';
  **   the rest.
  **
  ** The entry point is the cal_get() and cal_put() functions. Each of
  ** those is fed a 'struct cal_st *' [previously initialized withc
  ** cal_create()] that keeps the state of the allocator.
  **
  ** The cal_get()/cal_put() functions will check the request size and
  ** redirect the query to the chunk cache/allocator [struct
  ** chunk_cache_st] or straight to mmap.
  **
  ** The chunk allocator gets mchunks from the mmmap allocator and
  ** splits them in equal power-of-two-sized chunks. Easy, uh?
  **
  ** TODO:
  **
  **   - garbage collector (cal_gc) - actually, a compactor.
  **
  **   - link mchunk_headers in their list, so they can be disposed
  **     from the garbage collector 
  **
  **   - shutdown: destroy an allocator, freeing up all the memory it
  **     allocated.
  **
  ** None of these functions MODIFY errno [they save it for you]
  */

#include "pth_p.h"
#include "allocation.h"




  /* Debug utilities
  ** ---------------
  **
  ** Dump data structures, etc ...
  */

  /* Dump a list node */

static __inline__
void lnode_dump(struct lnode_st *node)
{
    if (CAL_DEBUG_LNODE == 1) {
        if (node)
            __debugmsg("debug: lnode 0x%p { prev 0x%p, next 0x%p }\n",
                       node, node->prev, node->next);
        else
            __debugmsg("debug: lnode 0x%p\n", node);
    }
}


  /* Dump the contents of a list
  **
  ** param l list to verify
  **
  ** param n maximum number of loops [in case its broken]
  **
  ** return 0 if consistent, 1 if not consistent.
  */

static __inline__ int list_dump(struct lnode_st *l, size_t n)
{
    if (CAL_DEBUG_LIST == 1)
        return 0;
    else {				/* CAL_DEBUG_LIST == 1 */
        struct lnode_st *p = NULL, *itr = l;
        size_t c = 0;

        __debugmsg("debug: dumping list %p\n", l);
        for (c = 0, itr = l; c < n; c++) {
            lnode_dump(itr);
            p = itr;
            itr = itr->next;
            if (itr == l)
                break;
        }
        __debugmsg("debug: dumped list %p\n", l);
        
        return 0;
    } /* CAL_DEBUG_LIST == 1 */
}


  /* Verify the consistency of a list
  **
  ** param l list to verify
  **
  ** param n maximum number of loops
  **
  ** return 0 if consistent, 1 if not consistent.
  */


static __inline__ int list_is_not_consistent(struct lnode_st *l, size_t n)
{
    if (CAL_CHECK_CONSISTENCY == 0)
        return 0;
    else {
        struct lnode_st *p = NULL, *itr = l;
        size_t c = 0;
        int retval = 1;

        while (1) {
            if (itr == 0) {
                __debugmsg("error: list %p, itr %p, prev %p: itr NULL\n", l,
                           itr, p);
                goto eexit;
            }
            if (itr->next == 0) {
                __debugmsg("error: list %p, itr %p, prev %p: itr->next NULL\n",
                           l, itr, p);
                goto eexit;
            }
            if (itr->prev == 0) {
                __debugmsg("error: list %p, itr %p, prev %p: itr->prev NULL\n",
                           l, itr, p);
                goto eexit;
            }
            if (c++ > n) {
                __debugmsg
                    ("error: list %p, itr %p, prev %p: max loops reached (%u)\n",
                     l, itr, p, c);
                goto eexit;
            }
            if (itr->next == l)
                break;
            if (itr == p) {
                __debugmsg
                    ("error: list %p, itr %p, prev %p: did any body remove"
                     " the head? count %u\n", l, itr, p, c);
                goto eexit;
            }
            p = itr;
            itr = itr->next;
        }
        return 0;
      eexit:
        return retval;
    }				/* CAL_CHECK_CONSISTENCY == 0 */
}












  /* Miscellaneous utilities
  ** ----------------------- */


  /* Get the exponent of a size that we round to the largest-or-equal
  ** power of two.
  */

static __inline__ unsigned get_pow2_order(unsigned s)
{
    unsigned exp = find_first_ms_bit(s);
    if (likely((1 << exp) < s))
	exp++;
    return exp;
}

  /* Round a size to be a multiple of the given size. */

static __inline__ size_t round_size(size_t round_to, size_t size)
{
    return ((size + round_to - 1) / round_to) * round_to;
}


  /* Convert the order of a memory allocation size to a list number.
  **
  ** The cache keeps list of sizes, however, they need a conversion
  ** from a size to the list index.
  */

static __inline__ int get_index_by_order(int order)
{
    return order - CAL_CHUNK_ORDER_MIN;
}


  /* Number of pages allocated to store more than one item of a
  ** certain size.
  **
  ** Basically, this is the ratio of ... if we have to allocate items
  ** of size X, how many should we allocate of them for further use
  ** and so that the usage of the space is maximized while keeping
  ** alignment?
  **
  ** In the current scheme, that seems to work pretty well, we
  ** allocate order/2 pages - thus, for allocating 1KiB blocks, we
  ** allocate 5 pages; that is 20 1KiB areas.
  */

static __inline__ size_t get_total_pages_by_order(size_t order)
{
    return order / 2;
}


  /* Get order by size
  **
  ** Given a size, calculate the order AND correct it to the minimum
  ** we allocate (CAL_CHUNK_ORDER_MIN).
  */

static __inline__ int get_order_by_size(size_t size)
{
    size_t order = get_pow2_order(size);

    if (unlikely(order < CAL_CHUNK_ORDER_MIN))
	order = CAL_CHUNK_ORDER_MIN;
    return order;
}
































  /* mchunk_header - splitting an mchunk in chunks
  ** --------------------------------------------- */

  /* An mmaped chunk that contains chunks; this is the header
  **
  ** Yes, we can be terribly innefective if we allocate sizes >>> than
  ** sizeof (struct mchunk_header_st) ... life sucks; even if I used a
  ** bitmap for the allocation table. I don' t want to keep the
  ** housekeeping information for the page in a separate structure,
  ** because that way we cannot get to it; this way, we just need to
  ** do (chunk & PAGE_MASK) and that is the address of the mchunk
  ** header.
  **
  ** Ok, the way to go is when we allocate big stuff (the >>>> case),
  ** then we should allocate more than one page for that
  ** thing. However, that complicates the identification of the mchunk
  ** header. Where is it? chunk & ~PAGE_MASK? chunk & PAGE_MASK - 1?
  ** -2? OK, we can use a magic number, actually, two magic
  ** numbers. They are actually a pointer to the chunk_cache_st that created
  ** this mchunk; the second is that pointer, negated. We also store
  ** the order of the chunks in this mchunk. As cal_put() has the size
  ** available, it can check the order of the size; if it matches, we
  ** have another level of security. Still there is a collision
  ** chance, but it should be minimal. FIXME.
  **
  ** FIXME: compress pages, used, order in bitfields
  */

struct mchunk_header_st {
    struct chunk_cache_st *chunk_cache;
    struct lnode_st node;
    unsigned pages;
    unsigned used;
    unsigned order;		/* order of the chunks in this mchunk */
    //#warning FIXME: time_t timestamp;
    unsigned long not_chunk_cache;	/* ~chunk_cache */
    void *first_chunk;
};

  /* Keep GCC 3.2 happy */

extern struct mchunk_header_st *get_mchunk_header_by_chunk(void *, size_t);
extern struct mchunk_header_st *mchunk_header_get_chunks(struct cal_st *,
							 size_t);
extern int chunk_cache_check_consistency(struct cal_st *, void *, size_t);

  /* Given an address for a chunk and it's size's order, locate the
  ** header of the mchunk.
  **
  ** See 'struct mchunk_header_st'.
  **
  ** We should not hit bad memory for the following reason: we should
  ** only be fed 'healthy chunks', and healthy chunks belong to some
  ** set of pages, and one set of pages has a header at the beginning.
  **
  ** A collision is possible, though unlikely
  */

struct mchunk_header_st *get_mchunk_header_by_chunk(void *p, size_t order)
{
    struct mchunk_header_st *mchunk_header;
    struct chunk_cache_st *chunk_cache;
    size_t pages = get_total_pages_by_order(order);
    unsigned long np = (unsigned long) p;

    for (np &= PAGE_MASK; pages > 0; np -= PAGE_SIZE, pages--) {
	/* Ok, if the chunk_cache equals the negated value of
	** not_cal and the order is the same, we have a header. */
	mchunk_header = (struct mchunk_header_st *) np;
	if (likely(mchunk_header->chunk_cache != NULL)) {
	    chunk_cache = (struct chunk_cache_st *)
		~mchunk_header->not_chunk_cache;
	    if (unlikely(mchunk_header->chunk_cache == chunk_cache
			 && mchunk_header->order == order))
		return mchunk_header;
	}
    }
    return NULL;
}


  /* Return the first [aligned] chunk from an mchunk [header] page
  ** set.
  */


static int mh_order = 0;
static int mh_pow2_size = 0;	/* 1 << mh_order */

static __inline__ struct lnode_st *mchunk_header_get_1st_chunk(struct mchunk_header_st
							       *mchunk_header)
{
    return mchunk_header->first_chunk;
}


  /* Get an mchunk and splice it into chunks
  **
  ** The ugliest part is calculating how many pages we should get; the
  ** smaller the chunk (the order, sizeof (chunk) == 1<<order), the
  ** less the pages, the bigest, the more. Why? because we need to
  ** optimize; we take out the page header from each one, so if we
  ** allocate for chunks of order 11, getting a single page is
  ** meaningless; we better get a few pages so that we have so and so
  ** many order 11 chunks without wasting a whole page for each
  ** because of the stupid header.
  **
  ** Use get_total_pages_by_order() to centralize the order-to-pages
  ** ratio.
  */

struct mchunk_header_st *mchunk_header_get_chunks(struct cal_st *cal,
						  size_t order)
{
    size_t total_pages;
    struct mchunk_header_st *mchunk_header;
    struct lnode_st *chunk, *pchunk, *next, *prev, *last;
    int step = 1 << order;

    /* How many pages? */

    total_pages = get_total_pages_by_order(order);

    mchunk_header = mmap_allocate(PAGE_SIZE * total_pages);
    if (mchunk_header == NULL)	/* Ouch! */
	return NULL;

    /* Initialize the header parts that we know about
    **
    ** For 'first_chunk': If the size of the chunks is smaller than
    ** the size of the 'struct mchunk_header_st', so we start where
    ** the header ends (aligning to the next power of two - and that
    ** is going to be aligned anyway to the chunk size).
    **
    ** The size of the chunks is bigger, so we start where the next
    ** chunk is meant to start from the beginning of the page [aka: we
    ** loose the first chunk].
    */

    mchunk_header->order = order;
    mchunk_header->pages = total_pages;
    mchunk_header->first_chunk = mchunk_header->order < mh_order ?
	((char *) mchunk_header) + (mh_pow2_size)
	: ((char *) mchunk_header) + (1 << order);
    last = (struct lnode_st *)
	((char *) mchunk_header + total_pages * PAGE_SIZE);
    mchunk_header->used =
	((char *) last -
	 (char *) mchunk_header->first_chunk) / (1 << order);

    /* Initialize the chunks starting at the next chunk boundary after
    ** the header
    **
    ** NOTE: this could be optimized in the list code.
    */

    for (pchunk = chunk = mchunk_header_get_1st_chunk(mchunk_header),
	 prev = (struct lnode_st *) ((char *) last - step);
	 chunk < last;) {
	next = (struct lnode_st *) ((char *) chunk + step);
	chunk->next = next;
	chunk->prev = prev;
	prev = chunk;
	chunk = next;
    }
    prev->next = pchunk;	/* last's next */
    list_is_not_consistent(pchunk, 2000);
    return mchunk_header;
}



















  /* Chunk Cache
  ** ----------- */


  /* Initialize */

static __inline__ void chunk_list_init(struct chunk_list_st *chunk_list)
{
    spinlock_init (&chunk_list->lock);
    lnode_init(&chunk_list->head);
    lnode_init(&chunk_list->mchunk_head);
}


  /* Release */

static __inline__ void chunk_list_release(struct chunk_list_st *chunk_list)
{
    while (!list_empty(&chunk_list->head)) {
	list_del(chunk_list->head.next);
	chunk_list->total--;
    }
}


  /* Initialize */

static __inline__ void chunk_cache_init(struct chunk_cache_st *chunk_cache)
{
    int cnt;
    for (cnt = 0;
	 cnt < sizeof(chunk_cache->head) / sizeof(struct chunk_list_st);
	 cnt++)
	chunk_list_init(&chunk_cache->head[cnt]);
}


  /* Free
  **
  ** FIXME: this needs finishing
  */

static __inline__
    void chunk_cache_release(struct chunk_cache_st *chunk_cache)
{
    int cnt;
    for (cnt = 0;
	 cnt < sizeof(chunk_cache->head) / sizeof(struct chunk_list_st);
	 cnt++)
	chunk_list_release(&chunk_cache->head[cnt]);
      // FIXME: release from mchunk_head list in the chunk_list_st
}


  /* Check consistency of a chunk
  **
  ** If it is on an mchunk, if the mchunk matches the allocator, etc, etc.
  **
  ** param cal Chunk cache this chunk should be in
  **
  ** param ptr Pointer to the chunk to test for consistency
  **
  ** param order Order of the chunk.
  **
  ** return 0 if ok, !0 on inconsistency
  */

int chunk_cache_check_consistency(struct cal_st *cal,
				  void *ptr, size_t order)
{
    if (CAL_CHECK_CONSISTENCY == 0)
        return 0;
    else {				/* #if CAL_CHECK_CONSISTENCY == 0 */
        int retval = 1;
        struct lnode_st *chunk = ptr;
        struct mchunk_header_st *mchunk_header;

            /* Do we have an mchunk? */

        mchunk_header = get_mchunk_header_by_chunk(chunk, order);
        if (mchunk_header == NULL) {
            __debugmsg("error: chunk_check_consistency (%p, %p, %u): "
                       "no mchunk\n", cal, ptr, order);
            goto eexit;
        }

            /* Is the mchunk in this cal? */

        if (mchunk_header->chunk_cache != &cal->chunk_cache) {
            __debugmsg("error: chunk_check_consistency (%p, %p, %u): "
                       "mchunk_header %p does not belong to chunk cache\n",
                       cal, ptr, order, mchunk_header);
            goto eexit;
        }

        retval = 0;
      eexit:
        return retval;
    }				/* #if CAL_CHECK_CONSISTENCY == 0 */
}


  /* Check if a chunk has been already 'cal_put()'
  **
  ** param cal Chunk cache this chunk should be in
  **
  ** param ptr Pointer to the chunk to test for consistency
  **
  ** param order Order of the chunk.
  **
  ** return 0 if ok, !0 on inconsistency
  */

static __inline__
    int chunk_cache_check_is_put(struct cal_st *cal,
				 void *ptr, size_t order)
{
    if (CAL_CHECK_CONSISTENCY == 0)
        return 0;
    else {				/* #if CAL_CHECK_CONSISTENCY == 0 */
        int retval = 1;
        size_t idx = get_index_by_order(order);
        struct lnode_st *chunk = ptr, *chunk_itr;
        struct mchunk_header_st *mchunk_header;
        struct chunk_list_st *chunk_list;
        void *locker;
        
        retval = chunk_cache_check_consistency(cal, ptr, order);
        if (retval != 0)
            goto eexit_nolock;

            /* Now check the chunk list for order 'order' in the cal, search if
            ** the chunk is already in the list [has been 'cal_put()'
            ** before]. */

        mchunk_header = get_mchunk_header_by_chunk(ptr, order);
        chunk_list = &cal->chunk_cache.head[idx];
        locker = pth_get_native_descr();
        spin_lock(&chunk_list->lock, locker, NULL);
        if (list_is_not_consistent(&chunk_list->head, 100000)) {
            __debugmsg("error: chunk_check_is_put (%p, %p, %u): "
                       "screwed up list in mchunk_header %p, list %p\n",
                       cal, ptr, order, mchunk_header, &chunk_list->head);
            retval = 1;
            goto eexit;
        }
        for (chunk_itr = chunk_list->head.next;
             !list_node_is_tail(chunk_itr, &chunk_list->head);
             chunk_itr = chunk_itr->next) {
            if (chunk_itr == NULL) {
                __debugmsg("error: chunk_check_is_put (%p, %p, %u): "
                           "chunk_itr is NULL mchunk_header %p, list %p\n",
                           cal, ptr, order, mchunk_header, &chunk_list->head);
                retval = 1;
                goto eexit;
            }
            if (chunk_itr == chunk) {
                __debugmsg("error: chunk_check_is_put (%p, %p, %u): "
                           "chunk was already released to mchunk_header %p, list %p\n",
                           cal, ptr, order, mchunk_header, &chunk_list->head);
                retval = 1;
                goto eexit;
            }
        }

        retval = 0;
      eexit:
        spin_unlock(&chunk_list->lock, locker);
      eexit_nolock:
        return retval;
    }				/* #if CAL_CHECK_CONSISTENCY == 0 */
}


  /* Provide a chunk of memory of the given size.
  **
  ** Actually the chunk is rounded up in size to the next power of
  ** two, as we split mchunks in chunks of powers of two to provide
  ** space. 
  */

void *chunk_cache_get(struct cal_st *cal, size_t size)
{
    void *ptr = NULL;
    size_t order;
    size_t idx;
    struct chunk_cache_st *chunk_cache;
    struct chunk_list_st *chunk_list;
    struct lnode_st *chunk;
    void *locker;
    
    __fdebugmsg(CAL_DEBUG_CHUNK_CACHE_GET,
		"debug: %s (%p, %lu)\n", __FUNCTION__, cal,
		(unsigned long) size);

    /* Paranoia check */
    if (CAL_CHECK_INITIALIZATION && mh_order == 0) {
        __debugmsg("BUG: %s (%p, %lu): allocator not yet initialized\n",
		    __FUNCTION__, cal, (unsigned long) size);
	goto exit;
    }
    
    /* First look for it in the cache; if nothing, allocate a set of
    ** pages, put an mchunk header, partition it and hook it to the
    ** list. */

    order = get_order_by_size(size);
    if (CAL_CHECK_CONSISTENCY && order >= CAL_CHUNK_ORDER_MAX) {
	__fdebugmsg(CAL_CHECK_CONSISTENCY, "debug: %s (%p, %lu): "
		    "requesting size too big for chunk\n",
		    __FUNCTION__, cal, (unsigned long) size);
	goto exit;
    }
    idx = get_index_by_order(order);

    chunk_cache = &cal->chunk_cache;
    chunk_list = &chunk_cache->head[idx];
    locker = pth_get_native_descr();
    spin_lock(&chunk_list->lock, locker, NULL);
    if (likely(!list_empty(&chunk_list->head))) {
#if CAL_STATISTICS == 1
	chunk_list->hits++;
#endif
    } else {
	/* List is empty, get another mchunk partitioned into chunks
	** (they are already linked into a list), fill out the header
	** stuff we need, and append the new list to the head of the
	** chunk_list. */

	struct mchunk_header_st *mchunk_header;

	if (CAL_CHECK_CONSISTENCY && chunk_list->total != chunk_list->used)
	    __fdebugmsg(CAL_CHECK_CONSISTENCY,
			"error: %s(%p, %lu): order %lu idx %lu"
			" chunk_list %p: list empty, total != used\n",
			__FUNCTION__, cal, (unsigned long) size,
			(unsigned long) order, (unsigned long) idx,
			chunk_list);

	mchunk_header = mchunk_header_get_chunks(cal, order);
	if (mchunk_header == NULL) {	/* ops, we are OOM */
	    spin_unlock(&chunk_list->lock, locker);
	    goto exit;
	}
	chunk_list->total += mchunk_header->used;
	mchunk_header->chunk_cache = chunk_cache;
	mchunk_header->not_chunk_cache = ~(unsigned long) chunk_cache;
	mchunk_header->used = 0;
	chunk = mchunk_header_get_1st_chunk(mchunk_header);
	list_is_not_consistent(chunk, 2000);
	list_insert_list(chunk, &chunk_list->head);
        list_insert_tail(&mchunk_header->node, &chunk_list->mchunk_head);
	__fdebugmsg(CAL_DEBUG_CHUNK_CACHE_GET,
		    "debug: %s (%p, %lu): created mchunk header "
		    "%p, first %p order %d\n",
		    __FUNCTION__, cal, (unsigned long) size, mchunk_header,
		    chunk, order);
#if CAL_STATISTICS == 1
	chunk_list->misses++;
#endif

	if (CAL_CHECK_CONSISTENCY) {
	    unsigned long c = (unsigned long) chunk_list->head.next;
	    unsigned long b = (unsigned long) chunk;
	    unsigned long e = (unsigned long) mchunk_header
		+ mchunk_header->pages * PAGE_SIZE;
	    if ((c < b) || (c >= e))
		__fdebugmsg(CAL_DEBUG_CHUNK_CACHE_GET,
			    "error: %s (%p, %lu): soon-to-be-given "
			    "chunk 0x%lx is not in mchunk_header %p, order %d\n",
			    __FUNCTION__, cal, (unsigned long) size, c,
			    mchunk_header, order);
	}
    }

    /* Ok, there are chunks (already there or recently allocated) in
    ** the list; pull out the first one */

    chunk = chunk_list->head.next;
    if (list_is_not_consistent(&chunk_list->head, 2000))
	list_dump(&chunk_list->head, 50);
    list_del(chunk);
    chunk_list->used++;
    spin_unlock(&chunk_list->lock, locker);
    ptr = chunk;
    memset(ptr, 0, 1 << order);

  exit:
    __fdebugmsg(CAL_DEBUG_CHUNK_CACHE_GET, "debug: %s (%p, %lu) = %p\n",
		__FUNCTION__, cal, (unsigned long) size, ptr);
    return ptr;
}


  /* Release usage of a chunk of memory
  **
  ** This puts it back into the chunk cache.
  */

void chunk_cache_put(struct cal_st *cal, void *ptr, size_t size)
{
    struct lnode_st *chunk = ptr;
    struct mchunk_header_st *mchunk_header;
    struct chunk_list_st *chunk_list;
    size_t order;
    size_t idx;
    void *locker;
    
    __fdebugmsg(CAL_DEBUG_CHUNK_CACHE_PUT, "debug: %s (%p, %p, %lu)\n",
		__FUNCTION__, cal, ptr, (unsigned long) size);

    /* Get the mchunk_header (set of pages) this ptr belongs to */

    order = get_order_by_size(size);
    if (CAL_CHECK_CONSISTENCY && order >= CAL_CHUNK_ORDER_MAX) {
	__fdebugmsg(CAL_CHECK_CONSISTENCY,
		    "debug: %s (%p, %p, %lu): requesting size too big for chunk\n",
		    __FUNCTION__, cal, ptr, (unsigned long) size);
	return;
    }
    idx = get_index_by_order(order);

    if (chunk_cache_check_is_put(cal, ptr, order))
	return;

    mchunk_header = get_mchunk_header_by_chunk(ptr, order);
    if (mchunk_header == NULL) {
	__fdebugmsg(CAL_CHECK_CONSISTENCY, "debug: %s (%p, %p, %lu): "
		    "Unable to get the mchunk header\n",
		    __FUNCTION__, cal, ptr, (unsigned long) size);
	/* Well, this means we'll leak this guy! */
	return;
    }

    /* Now we know the mchunk_header [and get_mchunk_header_by_chunk()
    ** has done sanity checks to make sure it is all right]. Thus, we
    ** know the cal and our order [chunk size], so we can get to our
    ** chunk_list. Thus, we can insert the chunk into the chunk list,
    ** update stats and finish.
    **
    ** NOTE: modify this such that if the mchunk is kind of empty
    ** (used < mchunk_capacity), it is appended to the tail;
    ** otherwise to the beginning (this way, the mchunks that are
    ** almost empty are freed before, and the fuller ones keep being
    ** reused. 
    */

    chunk_list = &mchunk_header->chunk_cache->head[idx];
    lnode_init(chunk);
    locker = pth_get_native_descr();
    spin_lock(&chunk_list->lock, locker, NULL);
    //#warning FIXME: timestamp (&mchunk->timestamp)
    mchunk_header->used--;
    chunk_list->used--;
    list_insert(chunk, &chunk_list->head);
    spin_unlock(&chunk_list->lock, locker);
    return;
}


  /* Verify if a pointer is part of an mchunk managed by the allocator
  **
  ** BEWARE: this just says if it is 'safe' to dereference the pointer.
  **
  ** THIS CAN ONLY VERIFY CHUNKS SMALLER IN ORDER THAN
  ** CAL_CHUNK_ORDER_MAX, as chunks bigger than that are passed
  ** directly to mmap_allocate().
  **
  ** We keep different lists, each one corresponding to a chunk size;
  ** the mchunks split their space in chunks and put the resulting
  ** chunks in the list for that corresponding size.
  **
  ** So, mchunks are always partitioned in chunks of the same size,
  ** ergo mchunks of the same "order" can be grouped together.
  **
  ** What we do then, is using the size, locate the list of mchunks
  ** that are providing memory for that size. Then just verify if the
  ** pointer is contained in one of those mchunks [from the beginning
  ** of the mchunk data zone (first_chunk) to the end
  ** (mchunk_header+mchunk size). 
  **
  ** It'd be much easier if there were a way to make sure that a
  ** pointer can be dereferenced with no problem ... but I cannot find
  ** it [of course, trapping an exception could be it, but it is kind
  ** of too complex, and probably would kill the program] ...
  **
  ** param cal Allocator to check against.
  **
  ** param p Pointer to verify
  **
  ** param s Size of the chunk where the pointer is/should be
  **
  ** returns 0 if the pointer is part of a chunk
  **       managed by the allocator and thus, it is safe to
  **       dereference it. !0 otherwise [actually, !0 means
  **       inconclusive -no way to know-, so, it is not that safe].
  */

int cal_verify_pointer (const struct cal_st *cal, const void *p, size_t s)
{
    int result = !0;
    
    __fdebugmsg(0 || CAL_DEBUG_CAL_GET || CAL_DEBUG_CAL_PUT,
                "debug: %s (%p, %lu)\n",
                __FUNCTION__, p, (unsigned long) s);
    
    if (s < CAL_CHUNK_THRESHOLD_SIZE)
    {
      size_t order, idx;
      const struct lnode_st *itr, *head;
      struct mchunk_header_st *mchunk_header;
      void *end;
      
      order = get_order_by_size(s);
      idx = get_index_by_order(order);
      head = &cal->chunk_cache.head[idx].mchunk_head;
      itr = list_next (head); /* We don't want the head, but the first :) */
      while (itr != head)
      {
          mchunk_header = list_entry (itr, struct mchunk_header_st, node);
          end = (char *) mchunk_header + PAGE_SIZE * mchunk_header->pages - s;
          if ((p >= mchunk_header->first_chunk) || (p < end)) {
              result = 0;
              break;
          }
          itr = list_next (itr);
      }
    }
    return result;
}






  /* Management of the allocation system and allocators
  ** -------------------------------------------------- */


  /* Initialize the allocator stuff
  **
  ** Having to initialize 'mh_order' in here is the lamest thing I
  ** have done ever, but I cannot find a way to have available, at
  ** compile time, the order of a know, constant number [without
  ** having to hardcode it for every arch, of course]. */

int cal_initialize(void)
{
    __fdebugmsg(CAL_DEBUG_INITIALIZATION, "debug: %s()\n", __FUNCTION__);
    mh_order = get_pow2_order(sizeof(struct mchunk_header_st));
    mh_pow2_size = 1 << mh_order;
    return 0;
}


  /* Create an allocator */

void cal_init(struct cal_st *cal)
{
    __fdebugmsg(CAL_DEBUG_INITIALIZATION,
		"debug: %s (%p)\n", __FUNCTION__, cal);
    memset(cal, 0, sizeof(*cal));
    chunk_cache_init(&cal->chunk_cache);
    return;
}

  /* Initialize the spinlock for the fork() */
void cal_fork_init(struct cal_st *cal)
{
    int cnt;
    __fdebugmsg(CAL_DEBUG_INITIALIZATION,
		"debug: %s (%p)\n", __FUNCTION__, cal);
    for (cnt = 0;
	 cnt <
	 sizeof(cal->chunk_cache.head) / sizeof(struct chunk_list_st);
	 cnt++)
        spinlock_init (&cal->chunk_cache.head[cnt].lock);
    return;
}

  /* Dump the status of the allocator :( */

void cal_dump(const struct cal_st *cal)
{
    if (CAL_DUMP == 0)
        return;
    else {				/* CAL_DUMP == 0 */
        int cnt;
        debugmsg("chunk cache %p:\n", cal);
        for (cnt = 0; cnt < PAGE_SHIFT - CAL_CHUNK_ORDER_MIN; cnt++) {
            const struct chunk_list_st *list;
            list = &cal->chunk_cache.head[cnt];
#if CAL_STATISTICS == 1
            debugmsg("  chunk list %d:\n"
                     "    lock:    %d\n"
                     "    hits:    %u\n"
                     "    misses   %u\n"
                     "    total:   %u\n"
                     "    used:    %u\n",
                     cnt, list->lock.futex.count,
                     (unsigned) list->hits,
                     (unsigned) list->misses,
                     (unsigned) list->total, (unsigned) list->used);
#else
            debugmsg("  chunk list %d:\n"
                     "    total:   %u\n"
                     "    used:    %u\n",
                     cnt, (unsigned) list->total, (unsigned) list->used);
#endif
        }
        return;
    }				/* CAL_DUMP == 0 */
}


  /* Destroy the allocator - free all mmaps - make sure they are not used.
  **
  ** maps that are not returned to us at the time of calling this will
  ** leak. We can only return the cached maps. The cached chunks might
  ** only be released if there is no used cache in the mchunk.
  **
  ** FIXME: This is incomplete right now. Link the mchunks in the
  ** chunk cache in a list so is easy to reclaim them. 
  */

void cal_release(struct cal_st *cal)
{
    __fdebugmsg(CAL_DEBUG_INITIALIZATION,
		"debug: %s (%p)\n", __FUNCTION__, cal);
    cal_dump(cal);
    chunk_cache_release(&cal->chunk_cache);
}


  /* Shutdown */

void cal_shutdown(void)
{
    __fdebugmsg(CAL_DEBUG_INITIALIZATION, "debug: %s()\n", __FUNCTION__);
}
