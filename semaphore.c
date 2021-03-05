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
**  semaphore.c: POSIX Thread ("Pthread") API for Pth
*/
                             /* ``The nice thing about standards is that
                                  there are so many to choose from.  And if
                                  you really don't like all the standards you
                                  just have to wait another year until the one
                                  arises you are looking for'' 
                                                 -- Tannenbaum, 'Introduction
                                                    to Computer Networks' */

/*
**  HEADER STUFF
*/

/*
 * Include our own Pthread and then the private Pth header.
 * The order here is very important to get correct namespace hiding!
 */
#define _PTHREAD_PRIVATE
#include "pthread.h"
#include "pth_p.h"
#include "allocation.h"

#ifdef _PTHREAD_QLOCK_DEFINED
struct _pthread_fastlock {
    long int __status;
    int __spinlock;
};
#endif

#undef _PTHREAD_PRIVATE
#include "semaphore.h"

extern int __new_sem_init(sem_t *sem, int pshared, unsigned int value);
int __new_sem_init(sem_t *sem, int pshared, unsigned int value)
{
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;
    
    if (value > SEM_VALUE_MAX)
	return_errno(-1, EINVAL);

    if ((sem->__sem_waiting = pth_malloc(sizeof(struct _sem_lock_st))) == NULL)
	return_errno(-1, ENOMEM);
    if (pshared > 0) {
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(&sem->__sem_waiting->__lock, &mattr);
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
	pthread_cond_init(&sem->__sem_waiting->nonzero, &cattr);
    } else {
	pthread_mutex_init(&sem->__sem_waiting->__lock, NULL);
	pthread_cond_init(&sem->__sem_waiting->nonzero, NULL);
    }
    sem->__sem_value = value;
    return 0;
}

extern int __new_sem_wait(sem_t * sem);
int __new_sem_wait(sem_t * sem)
{
    pthread_mutex_lock(&sem->__sem_waiting->__lock);
    if (sem->__sem_value == 0)
	pthread_cond_wait(&sem->__sem_waiting->nonzero, &sem->__sem_waiting->__lock);
    sem->__sem_value--;
    pthread_mutex_unlock(&sem->__sem_waiting->__lock);
    return 0;
}

extern int __new_sem_trywait(sem_t * sem);
int __new_sem_trywait(sem_t * sem)
{
    int retval = 0;

    retval = pthread_mutex_trylock(&sem->__sem_waiting->__lock);
    if (retval == 0) {
	if (sem->__sem_value == 0) {
	    pthread_mutex_unlock(&sem->__sem_waiting->__lock);
	    retval = -1;
	} else {
	    sem->__sem_value--;
	    pthread_mutex_unlock(&sem->__sem_waiting->__lock);
	    retval = 0;
	}
    }
    if (retval != 0)
	return_errno(retval, EAGAIN);
    return retval;
}

extern int __new_sem_post(sem_t * sem);
int __new_sem_post(sem_t * sem)
{
    pthread_mutex_lock(&sem->__sem_waiting->__lock);
    sem->__sem_value++;
    if (sem->__sem_value != 0)
        pthread_cond_signal(&sem->__sem_waiting->nonzero);
    pthread_mutex_unlock(&sem->__sem_waiting->__lock);
    return 0;
}

extern int __new_sem_getvalue(sem_t * sem, int * sval);
int __new_sem_getvalue(sem_t * sem, int * sval)
{
    *sval = sem->__sem_value;
    return 0;
}

extern int __new_sem_destroy(sem_t * sem);
int __new_sem_destroy(sem_t * sem)
{
    int retval;

    retval = pthread_mutex_destroy(&sem->__sem_waiting->__lock);
    if (retval == 0)
	retval = pthread_cond_destroy(&sem->__sem_waiting->nonzero);
    pth_free_mem (sem->__sem_waiting, sizeof(struct _sem_lock_st));
    return retval;
}

extern sem_t *__sem_open(const char *name, int oflag, ...);
sem_t *__sem_open(const char *name, int oflag, ...)
{
    return_errno(SEM_FAILED, ENOSYS);
}
strong_alias(__sem_open, sem_open);

extern int __sem_close(sem_t *sem);
int __sem_close(sem_t *sem)
{
    return_errno(-1, ENOSYS);
}
strong_alias(__sem_close, sem_close);

extern int __sem_unlink(const char *name);
int __sem_unlink(const char *name)
{
    return_errno(-1, ENOSYS);
}
strong_alias(__sem_unlink, sem_unlink);

extern int __sem_timedwait(sem_t *sem, const struct timespec *abstime);
int __sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    return_errno(-1, ENOSYS);
}
strong_alias(__sem_timedwait, sem_timedwait);


versioned_symbol(__new_sem_init, sem_init, GLIBC_2.1)
versioned_symbol(__new_sem_wait, sem_wait, GLIBC_2.1)
versioned_symbol(__new_sem_trywait, sem_trywait, GLIBC_2.1)
versioned_symbol(__new_sem_post, sem_post, GLIBC_2.1)
versioned_symbol(__new_sem_getvalue, sem_getvalue, GLIBC_2.1)
versioned_symbol(__new_sem_destroy, sem_destroy, GLIBC_2.1)
