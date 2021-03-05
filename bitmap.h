
  /* -*-c-*- NGPT: Compile-time-sized bitmaps
  ** 
  ** $Id: bitmap.h,v 1.1 2002/10/09 15:08:58 billa Exp $
  **
  ** Distributed under the terms of the LGPL v2.1. See file
  ** $top_srcdir/COPYING.
  **
  ** Code for management of bits set in bitmaps. The basic operations
  ** needed are clear/set bits, find first set bit and little more
  ** (the minimal set I need for the pqeues).
  **
  ** (C) 2002 Intel Corporation 
  **   Iñaky Pérez-González <inaky.perez-gonzalez@intel.com>
  **
  **      [- THINGS DONE BY AUTHOR]
  */

#ifndef __ngpt_bitmap_h__
#define __ngpt_bitmap_h__

#include <string.h>   /* memset() */
#include "bitops.h"   /* bit operations */

  /* Number of bits - we optimize for our case - we are interested in
  ** 140 levels; check out bitmap_find_first_set() if you bump it up. */

#define BITMAP_BITS 160


  /* How many 'unsigned long's are needed for BITMAP_BITS?
  **
  ** First we round BITMAP_BITS to a multiple of 8 (byte size) and
  ** then, round the number of bytes to 'sizeof (unsigned long)'. */

#define BITMAP_SLOTS (\
  ((((BITMAP_BITS)+1+7) / 8) + sizeof (unsigned long) - 1) \
  / sizeof (unsigned long))


  /* Bits-per-slot (assuming 8 bits/byte :) */

#define BITMAP_BASE_BITSIZE (8 * sizeof (unsigned long))


  /* The bitmap structure
  **
  ** bit 0 for the bitmap is at slot[0], bit 0; bit 31 is slot[0], bit
  ** 31, bit 32 is slot[1] bit 0 ... bit 65 is slot[2], bit 1.
  */

struct bitmap_st
{
  unsigned long slot[BITMAP_SLOTS];
};


  /* Set all of the bits to zero */

static __inline__
void bitmap_zero (struct bitmap_st *bitmap)
{
  memset (bitmap, 0, sizeof (struct bitmap_st));
}


  /* Init non-zerod data */

static __inline__
void bitmap_init (struct bitmap_st *bitmap)
{
}


  /* Set all to one */

static __inline__
void bitmap_set (struct bitmap_st *bitmap)
{
  memset (&bitmap, -1, sizeof (struct bitmap_st));
}


  /* Reset all to zero */

static __inline__
void bitmap_reset (struct bitmap_st *bitmap)
{
  bitmap_zero (bitmap);
}


  /* Set given bit */

static __inline__
void bitmap_set_bit (struct bitmap_st *bitmap, size_t bit)
{
  bit_set (bit, bitmap->slot);
}


  /* Reset given bit */

static __inline__
void bitmap_reset_bit (struct bitmap_st *bitmap, size_t bit)
{
  bit_reset (bit, bitmap->slot);
}


  /* Find first bit most significant bit set, return its index (start
  ** from 'bitmap_bits' down to zero).
  **
  ** I am sure there is a more portable and elegant way of doing this,
  ** but I feel lazy today.
  **
  ** return Index of the first bit set with lower index; undefined if
  **        none set [oju! we start on the top of the array, that's
  **        it, BITMAP_SLOT-1 and work our way down to slot[0]].
  */

static __inline__
int bitmap_ffms_bit_set (struct bitmap_st *bitmap)
{
#define bitmap_scan(round)                              \
  if (unlikely (bitmap->slot[round]))                   \
    return find_first_ms_bit (bitmap->slot[round])      \
      + round * BITMAP_BASE_BITSIZE;
  bitmap_scan(4);
  bitmap_scan(3);
  bitmap_scan(2);
  bitmap_scan(1);
  return find_first_ms_bit (bitmap->slot[0]);
}

#endif /* __ngpt_bitmap_h__ */

