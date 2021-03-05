
/* Atomic dec: return new value. */
static __inline__ int __futex_down(int *counter)
{
    int val;

    /* Don't decrement if already negative. */
    val = *counter;
    if (val < 0)
    	return val;

    __asm__ __volatile__ ("fetchadd4.rel %0=[%1], -1;;"
			  : "=&r" (val) : "r" ("*counter"): "memory");

    /* We know if it's zero... */
    return val;
}

/* Atomic inc: return 1 if counter incremented from 0 to 1. */
static __inline__ int __futex_up(int *c)
{
    int val;

    __asm__ __volatile__ ("fetchadd4.acq %0=[%1], 1;;"
			  : "=&r" (val) : "r" ("*counter"): "memory");
    return val == 1;
}

/* Simple atomic increment. */
static __inline__ void __atomic_inc(int *c)
{
    int val;	/* should be able to dump later */
    __asm__ __volatile__ ("fetchadd4.acq %0=[%1], 1;;"
			  : "=&r" (val) : "r" ("*counter"): "memory");
}

/* Atomic decrement, and return 1 if result is negative. */
static __inline__ int __furwock_dec_negative(int *c)
{
    int val;

    __asm__ __volatile__ ("fetchadd4.acq %0=[%1], -1;;"
			  : "=&r" (val) : "r" ("*counter"): "memory");
    return val < -1;
}

/* 
 * Commit the write, so it happens before we send the semaphore to
 * anyone else 
 */
static __inline__ void __futex_commit(void)
{
}
