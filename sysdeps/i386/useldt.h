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
**  useldt.h - architecture specific ldt table for i386
*/

#ifndef __ngpt_useldt_ia32_h__
#define __ngpt_useldt_ia32_h__

/* We don't want to include the kernel header.  So duplicate the
   information.  */

/* Structure passed on `modify_ldt' call.  */
struct modify_ldt_ldt_s
{
  unsigned int entry_number;
  unsigned long int base_addr;
  unsigned int limit;
  unsigned int seg_32bit:1;
  unsigned int contents:2;
  unsigned int read_exec_only:1;
  unsigned int limit_in_pages:1;
  unsigned int seg_not_present:1;
  unsigned int useable:1;
  unsigned int empty:25;
};

/* System call to set LDT entry.  */
extern int __modify_ldt (int, struct modify_ldt_ldt_s *, size_t);


/* Return the thread descriptor for the current thread.

   The contained asm must *not* be marked volatile since otherwise
   assignments like
	pthread_descr self = thread_self();
   do not get optimized away.  */
#define NATIVE_SELF \
({									      \
  register pth_descr_t __self;					      \
  __asm__ ("movl %%gs:%c1,%0" : "=r" (__self)				      \
	   : "i" (offsetof (struct pth_descr_st,			      \
			    self)));					      \
  __self;								      \
})


/* Initialize the thread-unique value.  Two possible ways to do it.  */

#define DO_MODIFY_LDT(descr, nr)					      \
({									      \
  struct modify_ldt_ldt_s ldt_entry =					      \
    { nr, (unsigned long int) descr, sizeof (struct pth_descr_st),	      \
      1, 0, 0, 0, 0, 1, 0 };						      \
  if (__modify_ldt (1, &ldt_entry, sizeof (ldt_entry)) != 0)		      \
    abort ();								      \
  asm ("movw %w0, %%gs" : : "q" (nr * 8 + 7));				      \
})

/* When using the new set_thread_area call, we don't need to change %gs
   because we inherited the value set up in the main thread by TLS setup.
   We need to extract that value and set up the same segment in this
   thread.  */
# if USE_TLS
#  define DO_SET_THREAD_AREA_REUSE(nr)	1
# else
/* Without TLS, we do the initialization of the main thread, where NR == 0.  */
#  define DO_SET_THREAD_AREA_REUSE(nr)	(!__builtin_constant_p (nr) || (nr))
# endif
# define DO_SET_THREAD_AREA(descr, nr)					      \
({									      \
  int __gs;								      \
  if (DO_SET_THREAD_AREA_REUSE (nr))					      \
    {									      \
      asm ("movw %%gs, %w0" : "=q" (__gs));				      \
      struct modify_ldt_ldt_s ldt_entry =				      \
	{ (__gs & 0xffff) >> 3,						      \
	  (unsigned long int) descr, sizeof (struct pth_descr_st),	      \
	  1, 0, 0, 0, 0, 1, 0 };					      \
      if (__builtin_expect (INLINE_SYSCALL (set_thread_area, 1, &ldt_entry),  \
			    0) == 0)					      \
	asm ("movw %w0, %%gs" :: "q" (__gs));				      \
      else								      \
	__gs = -1;							      \
    }									      \
  else									      \
    {									      \
      struct modify_ldt_ldt_s ldt_entry =				      \
	{ -1,								      \
	  (unsigned long int) descr, sizeof (struct pth_descr_st),	      \
	  1, 0, 0, 0, 0, 1, 0 };					      \
      if (__builtin_expect (INLINE_SYSCALL (set_thread_area, 1, &ldt_entry),  \
			    0) == 0)					      \
	{								      \
	  __gs = (ldt_entry.entry_number << 3) + 3;			      \
	  asm ("movw %w0, %%gs" : : "q" (__gs));			      \
	}								      \
      else								      \
	__gs = -1;							      \
    }									      \
  __gs;									      \
})

#if defined __ASSUME_SET_THREAD_AREA_SYSCALL_disabled
# define INIT_NATIVE_SELF(descr, nr)	DO_SET_THREAD_AREA (descr, nr)
#elif defined __NR_set_thread_area_disabled
# define INIT_NATIVE_SELF(descr, nr)					      \
({									      \
  if (__builtin_expect (__have_no_set_thread_area, 0)			      \
      || (DO_SET_THREAD_AREA (descr, DO_SET_THREAD_AREA_REUSE (nr)) == -1     \
	  && (__have_no_set_thread_area = 1)))				      \
    DO_MODIFY_LDT (descr, nr);						      \
})
/* Defined in pspinlock.c.  */
extern int __have_no_set_thread_area;
#else
# define INIT_NATIVE_SELF(descr, nr)	DO_MODIFY_LDT (descr, nr)
#endif

/* Free resources associated with thread descriptor.  */
#ifdef __ASSUME_SET_THREAD_AREA_SYSCALL_disabled
#define FREE_NATIVE(descr, nr) do { } while (0)
#elif defined __NR_set_thread_area_disabled
#define FREE_NATIVE(descr, nr) \
{									      \
  int __gs;								      \
  __asm__ __volatile__ ("movw %%gs, %w0" : "=q" (__gs));		      \
  if (__builtin_expect (__gs & 4, 0))					      \
    {									      \
      struct modify_ldt_ldt_s ldt_entry =				      \
	{ nr, 0, 0, 0, 0, 1, 0, 1, 0, 0 };				      \
      __modify_ldt (1, &ldt_entry, sizeof (ldt_entry));			      \
    }									      \
}
#else
#define FREE_NATIVE(descr, nr) \
{									      \
  struct modify_ldt_ldt_s ldt_entry =					      \
    { nr, 0, 0, 0, 0, 1, 0, 1, 0, 0 };					      \
  __modify_ldt (1, &ldt_entry, sizeof (ldt_entry));			      \
}
#endif

#if 0
/* Read member of the thread descriptor directly.  */
#define THREAD_GETMEM(descr, member) \
({									      \
  __typeof__ (descr->member) __value;					      \
  if (sizeof (__value) == 1)						      \
    __asm__ __volatile__ ("movb %%gs:%P2,%b0"				      \
			  : "=q" (__value)				      \
			  : "0" (0),					      \
			    "i" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else if (sizeof (__value) == 4)					      \
    __asm__ __volatile__ ("movl %%gs:%P1,%0"				      \
			  : "=r" (__value)				      \
			  : "i" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else									      \
    {									      \
      if (sizeof (__value) != 8)					      \
	/* There should not be any value with a size other than 1, 4 or 8.  */\
	abort ();							      \
									      \
      __asm__ __volatile__ ("movl %%gs:%P1,%%eax\n\t"			      \
			    "movl %%gs:%P2,%%edx"			      \
			    : "=A" (__value)				      \
			    : "i" (offsetof (struct _pthread_descr_struct,    \
					     member)),			      \
			      "i" (offsetof (struct _pthread_descr_struct,    \
					     member) + 4));		      \
    }									      \
  __value;								      \
})

/* Same as THREAD_GETMEM, but the member offset can be non-constant.  */
#define THREAD_GETMEM_NC(descr, member) \
({									      \
  __typeof__ (descr->member) __value;					      \
  if (sizeof (__value) == 1)						      \
    __asm__ __volatile__ ("movb %%gs:(%2),%b0"				      \
			  : "=q" (__value)				      \
			  : "0" (0),					      \
			    "r" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else if (sizeof (__value) == 4)					      \
    __asm__ __volatile__ ("movl %%gs:(%1),%0"				      \
			  : "=r" (__value)				      \
			  : "r" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else									      \
    {									      \
      if (sizeof (__value) != 8)					      \
	/* There should not be any value with a size other than 1, 4 or 8.  */\
	abort ();							      \
									      \
      __asm__ __volatile__ ("movl %%gs:(%1),%%eax\n\t"			      \
			    "movl %%gs:4(%1),%%edx"			      \
			    : "=&A" (__value)				      \
			    : "r" (offsetof (struct _pthread_descr_struct,    \
					     member)));			      \
    }									      \
  __value;								      \
})

/* Same as THREAD_SETMEM, but the member offset can be non-constant.  */
#define THREAD_SETMEM(descr, member, value) \
({									      \
  __typeof__ (descr->member) __value = (value);				      \
  if (sizeof (__value) == 1)						      \
    __asm__ __volatile__ ("movb %0,%%gs:%P1" :				      \
			  : "q" (__value),				      \
			    "i" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else if (sizeof (__value) == 4)					      \
    __asm__ __volatile__ ("movl %0,%%gs:%P1" :				      \
			  : "r" (__value),				      \
			    "i" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else									      \
    {									      \
      if (sizeof (__value) != 8)					      \
	/* There should not be any value with a size other than 1, 4 or 8.  */\
	abort ();							      \
									      \
      __asm__ __volatile__ ("movl %%eax,%%gs:%P1\n\n"			      \
			    "movl %%edx,%%gs:%P2" :			      \
			    : "A" (__value),				      \
			      "i" (offsetof (struct _pthread_descr_struct,    \
					     member)),			      \
			      "i" (offsetof (struct _pthread_descr_struct,    \
					     member) + 4));		      \
    }									      \
})

/* Set member of the thread descriptor directly.  */
#define THREAD_SETMEM_NC(descr, member, value) \
({									      \
  __typeof__ (descr->member) __value = (value);				      \
  if (sizeof (__value) == 1)						      \
    __asm__ __volatile__ ("movb %0,%%gs:(%1)" :				      \
			  : "q" (__value),				      \
			    "r" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else if (sizeof (__value) == 4)					      \
    __asm__ __volatile__ ("movl %0,%%gs:(%1)" :				      \
			  : "r" (__value),				      \
			    "r" (offsetof (struct _pthread_descr_struct,      \
					   member)));			      \
  else									      \
    {									      \
      if (sizeof (__value) != 8)					      \
	/* There should not be any value with a size other than 1, 4 or 8.  */\
	abort ();							      \
									      \
      __asm__ __volatile__ ("movl %%eax,%%gs:(%1)\n\t"			      \
			    "movl %%edx,%%gs:4(%1)" :			      \
			    : "A" (__value),				      \
			      "r" (offsetof (struct _pthread_descr_struct,    \
					     member)));			      \
    }									      \
})
#endif

/* We want the OS to assign stack addresses.  */
#define FLOATING_STACKS	1

/* Maximum size of the stack if the rlimit is unlimited.  */
#define ARCH_STACK_MAX_SIZE	8*1024*1024

#endif				/* __ngpt_useldt_ia32_h__ */
