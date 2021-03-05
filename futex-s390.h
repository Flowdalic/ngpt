#ifndef __NR_futex
#define __NR_futex 238
#endif

#define __CS_LOOP(old_val, new_val, counter, op_val)		 \
	__asm__ __volatile__("   l   %0,0(%3)\n"		 \
			     "0: lr  %1,%0\n"			 \
			     "   ahi %1,%4\n"			 \
			     "   cs  %0,%1,0(%3)\n"		 \
			     "   jl  0b"			 \
                             : "=&d" (old_val), "=&d" (new_val), \
                               "+m" (*counter)      \
                             : "a" (counter), "K" (op_val) : "cc" );

/* Atomic dec: return new value. */
static __inline__ int __futex_down(int *counter)
{
    int newval, oldval;

    /* Don't decrement if already negative. */
    oldval = *counter;
    if (oldval < 0)
	return oldval;
    __CS_LOOP(oldval, newval, counter, -1);
    return newval;
}

/* Atomic inc: return 1 if counter incremented from 0 to 1. */
static __inline__ int __futex_up(int *counter)
{
    int newval, oldval;
    __CS_LOOP(oldval, newval, counter, 1);
    return (newval == 1);
}

/* Simple atomic inc. */
static __inline__ void __atomic_inc(int *counter)
{
    int newval, oldval;
    __CS_LOOP(oldval, newval, counter, 1);
}

/* Atomic dec: return 1 if result is negative */
static __inline__ int __furwock_dec_negative(int *counter)
{
    int newval, oldval;
    __CS_LOOP(oldval, newval, counter, -1);
    return newval < 0;
}

/* Commit the write, so it happens before we send the semaphore to
   anyone else */
static __inline__ void __futex_commit(void)
{
    __asm__ __volatile__ ("bcr 15,0");
}

