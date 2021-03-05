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
**  pth_data.c: Pth per-thread specific data
*/
                             /* ``Breakthrough ideas
                                  are not from teams.''
                                       --- Hans von Ohain */
#include "pth_p.h"
#include "allocation.h"

#if cpp

struct pth_keytab_st {
    int used;
    void (*destructor)(void *);
};

#endif /* cpp */

static struct pth_keytab_st pth_keytab[PTH_KEY_MAX];
#ifdef THREAD_DB
strong_alias(pth_keytab, pthread_keys)
const int __linuxthreads_pthread_keys_max = PTHREAD_KEYS_MAX;
#endif


intern pth_qlock_t pth_key_lock = pth_qlock_INIT_UNLOCKED;

int __pth_key_create(pth_key_t *key, void (*func)(void *))
{
    for ((*key) = 0; (*key) < PTH_KEY_MAX; (*key)++) {
        if (pth_keytab[(*key)].used == FALSE) {
            pth_keytab[(*key)].used = TRUE;
            pth_keytab[(*key)].destructor = func;
            return TRUE;
        }
    }
    return FALSE;
}

int pth_key_create(pth_key_t *key, void (*func)(void *), void *locker)
{
    pid_t tid = current_tid();

    _pth_acquire_lock(&pth_key_lock, tid);
    for ((*key) = 0; (*key) < PTH_KEY_MAX; (*key)++) {
        if (pth_keytab[(*key)].used == FALSE) {
            pth_keytab[(*key)].used = TRUE;
            pth_keytab[(*key)].destructor = func;
	    _pth_release_lock(&pth_key_lock, tid);
            return TRUE;
        }
    }
    _pth_release_lock(&pth_key_lock, tid);
    return FALSE;
}

int pth_key_delete(pth_key_t key)
{
    if (key >= PTH_KEY_MAX)
        return FALSE;
    if (!pth_keytab[key].used)
        return FALSE;
    pth_acquire_lock(&pth_key_lock);
    pth_keytab[key].used = FALSE;
    pth_release_lock(&pth_key_lock);
    return TRUE;
}

int pth_key_setdata(pth_key_t key, const void *value)
{
    pth_t current = pth_get_current();

    if (key >= PTH_KEY_MAX)
        return EINVAL;
    if (!pth_keytab[key].used)
        return EINVAL;
    if (current->data_value == NULL) {
        current->data_value = (const void **)pth_malloc(sizeof(void *)*PTH_KEY_MAX);
        if (current->data_value == NULL)
            return ENOMEM;
    }
    if (current->data_value[key] == NULL) {
        if (value != NULL)
            current->data_count++;
    } else {
        if (value == NULL)
            current->data_count--;
    }
    current->data_value[key] = value;
    return 0;
}
/* Warning: the following requires that pthread_key_t == pth_key_t */
strong_alias(pth_key_setdata, pthread_setspecific)
strong_alias(pth_key_setdata, __pthread_setspecific)

void *pth_key_getdata(pth_key_t key)
{
    pth_t current = pth_get_current();

    if (unlikely (current == NULL) || unlikely (key >= PTH_KEY_MAX))
        return NULL;
    if (!pth_keytab[key].used)
        return NULL;
    if (current->data_value == NULL)
        return NULL;
    return (void *)current->data_value[key];
}
/* Warning: the following requires that pthread_key_t == pth_key_t */
strong_alias(pth_key_getdata, pthread_getspecific)
strong_alias(pth_key_getdata, __pthread_getspecific)

intern void pth_key_destroydata(pth_t t)
{
    void *data;
    int key;
    int itr;
    void (*destructor)(void *);
    pth_descr_t descr = pth_get_native_descr();

    /* POSIX thread iteration scheme */
    _pth_acquire_lock(&pth_key_lock, descr ? descr->tid : k_gettid());
    for (itr = 0; itr < PTH_DESTRUCTOR_ITERATIONS; itr++) {
        for (key = 0; key < PTH_KEY_MAX; key++) {
            if (t->data_count > 0) {
                destructor = NULL;
                data = NULL;
                if (pth_keytab[key].used) {
                    if (t->data_value[key] != NULL) {
                        data = (void *)t->data_value[key];
                        t->data_value[key] = NULL;
                        t->data_count--;
                        destructor = pth_keytab[key].destructor;
                    }
                }
                if (destructor != NULL) {
		    /* release the lock, destructor may change the native */
		    _pth_release_lock(&pth_key_lock, descr ? descr->tid : k_gettid());
                    destructor(data);
		    descr = pth_get_native_descr();
		    _pth_acquire_lock(&pth_key_lock, descr ? descr->tid : k_gettid());
		}
            }
            if (t->data_count == 0)
                break;
        }
        if (t->data_count == 0)
            break;
    }
    pth_free_mem(t->data_value, sizeof(void *)*PTH_KEY_MAX);
    t->data_value = NULL;
    _pth_release_lock(&pth_key_lock, descr ? descr->tid : k_gettid());
    return;
}

