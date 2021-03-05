/* Private header for thread debug library.  */
#ifndef _THREAD_DBP_H
#define _THREAD_DBP_H	1

#include <stdio.h>
#include "pth.h"
#include "td_manager.h"

#include <string.h>
#include "proc_service.h"
#include "thread_db.h"

/* Indeces for the symbol names.  */
enum
  {
    PTHREAD_THREADS_EVENTS = 0,
    PTHREAD_LAST_EVENT,
    PTHREAD_HANDLES_NUM,
    PTHREAD_HANDLES,
    PTHREAD_NATIVE_LIST,
    PTHREAD_NUMBER_OF_NATIVES,
    PTHREAD_KEYS,
    LINUXTHREADS_PTHREAD_THREADS_MAX,
    LINUXTHREADS_PTHREAD_KEYS_MAX,
    LINUXTHREADS_PTHREAD_SIZEOF_DESCR,
    LINUXTHREADS_CREATE_EVENT,
    LINUXTHREADS_DEATH_EVENT,
    LINUXTHREADS_REAP_EVENT,
    PTHREAD_DOING_DEBUG,
    NUM_MESSAGES
  };


/* Comment out the following for less verbose output.  */
#ifdef TD_DEBUG
extern ssize_t __libc_write (int __fd, __const void *__buf, size_t __n);
# define lib_td_debug(c) if (__lib_td_debug) \
			    __libc_write (2, c "\n", strlen (c "\n"))
extern int __lib_td_debug;
#else
# define lib_td_debug(c)
#endif


/* Handle for a process.  This type is opaque.  */
struct td_thragent
{
  /* Delivered by the debugger and we have to pass it back in the
     proc callbacks.  */
  struct ps_prochandle *ph;

  /* Some cached information.  */

  /* Address of the `__pthread_handles' array.  */
  struct pthread_handle_struct *handles;

  /* Address of the `pthread_kyes' array.  */
  struct pthread_key_struct *keys;

  /* Maximum number of threads.  */
  int pthread_threads_max;

  /* Maximum number of thread-local data keys.  */
  int pthread_keys_max;

  /* Size of 2nd level array for thread-local data keys.  */
  int pthread_key_2ndlevel_size;

  /* Sizeof struct _pthread_descr_struct.  */
  int sizeof_descr;

  /* Pointer to the `__pthread_threads_events' variable in the target.  */
  psaddr_t pthread_threads_eventsp;

  /* Pointer to the `__pthread_last_event' variable in the target.  */
  psaddr_t pthread_last_event;

  /* Pointer to the `__pthread_handles_num' variable.  */
  psaddr_t pthread_handles_num;
};


/* Type used internally to keep track of thread agent descriptors.  */
struct agent_list
{
  td_thragent_t *ta;
  struct agent_list *next;
};

/* List of all known descriptors.  */
extern struct agent_list *__td_agent_list;

/* Function used to test for correct thread agent pointer.  */
static inline int
ta_ok (const td_thragent_t *ta)
{
  struct agent_list *runp = __td_agent_list;

  if (ta == NULL)
    return 0;

  while (runp != NULL && runp->ta != ta)
    runp = runp->next;

  return runp != NULL;
}


/* Internal wrapper around ps_pglobal_lookup.  */
extern int td_lookup (struct ps_prochandle *ps, int idx, psaddr_t *sym_addr);

/* Info from runtime on lwps, for NGPT the descriptor table */
struct targ_lwp_info {
  int			num_lwps;
  struct pth_descr_st	*lwp_info_tab;
  psaddr_t		lwp_info_base_addr;
};

td_err_e get_targ_lwp_info(const td_thragent_t *ta,
			   struct targ_lwp_info *lip);
td_err_e get_targ_lwp_descr(const td_thragent_t *ta,
			    struct targ_lwp_info *lip,
			    psaddr_t descr_addr,
			    struct pth_descr_st **descr);

td_err_e get_targ_lwp_from_thr(const td_thrhandle_t *th,
			       struct _pthread_descr_struct *thr_descr_p,
			       struct targ_lwp_info *lwp_info_p,
			       lwpid_t *ret_lwpid_p);

#if defined(__powerpc__)
/* see ucontext.h -> sigcontext.h: struct sigcontext_struct { regs } */
typedef struct pt_regs *gregset_t;
#endif

void td_utils_gregset_to_prgregset(gregset_t gregset, prgregset_t prgregset);
void td_utils_prgregset_to_gregset(prgregset_t prgregset, gregset_t gregset);

#endif /* thread_dbP.h */
