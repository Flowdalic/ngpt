/*
 * Bridge between NGPT runtime and libthread_db interfaces
 */
#include "pth_p.h"				/* NGPT Glue */

typedef struct pth_st	*pthread_descr;		/* NGPT Glue */
#define _pthread_descr_struct pth_st		/* NGPT Glue */
typedef pth_qlock_t	_pthread_fastlock;	/* NGPT Glue */
#define p_eventbuf	td_events		/* NGPT Glue */
#define p_report_events	td_report_events	/* NGPT Glue */
#define p_specific	data_value		/* NGPT Glue */
#define p_priority	prio			/* NGPT Glue */

/* Maximum number of debug handles for NGPT threads. Note that 
   this limits the number of threads we can debug, not the number
   of threads that NGPT can handle. Make dynamic later in concert
   with gdb, as it assumes a static __pthread_handles array of 
   this size to be there before the debug APP starts.            */
#define MAX_TD_DEBUG_HANDLES	10240		/* 4 bytes each */
#define MAX_NATIVE_LIST		PTH_MAX_SCHEDULERS /* cover runtime */

/* Whether to use debugger additional actions for thread creation
   (set to 1 by gdb) */
extern volatile int __pthread_threads_debug;

/* If libthread_db is involved to setup thread debugging */
extern volatile int __pthread_doing_debug;

/* File descriptor for sending requests to the thread manager. */
/* Initially -1, meaning that the thread manager is not running. */
extern int __pthread_manager_request;

/* Other end of the pipe for sending requests to the thread manager. */
extern int __pthread_manager_reader;

typedef struct pthread_handle_struct *pthread_handle;

struct pthread_handle_struct {
    pth_t		h_descr;	/* Thread descriptor/NULL if invalid */
};

/* array of thread handles used for debug control */
extern struct pthread_handle_struct __pthread_handles[MAX_TD_DEBUG_HANDLES];

/* The type of messages sent to the thread manager thread */

struct pthread_request {
  pth_t req_thread;			/* Thread doing the request	*/
  pth_qlock_t  *req_thread_lock;	/* wait for completion lock	*/
  enum {				/* Request kind			*/
    REQ_CREATE, REQ_FREE, REQ_PROCESS_EXIT, REQ_MAIN_THREAD_EXIT,
    REQ_POST, REQ_DEBUG, REQ_KICK
  } req_kind;
  union {				/* Arguments for request	*/
    struct {				/* For REQ_CREATE:		*/
      pth_attr_t   attr;		/* - thread attributes:		*/
      void * (*fn)(void *);		/*   start function		*/
      void * arg;			/*   argument to start function */
      sigset_t mask;			/*   signal mask		*/
    } create;
    struct {				/* For REQ_FREE:		*/
      pth_t thread_id;			/*   identifier of thread to free */
    } free;
    struct {				/* For REQ_PROCESS_EXIT:	*/
      int code;				/*   exit status		*/
    } exit;
    void * post;			/* For REQ_POST: the semaphore	*/
  } req_args;
};

/* defined for structure used for pthread_keys array */
#define pthread_keys_st pth_keytab_st
#define pthread_key_struct pth_keytab_st

/* Signals used for communication with gdb and thread suspend/resume */
extern int __pthread_sig_restart;
extern int __pthread_sig_cancel;
extern int __pthread_sig_debug;

/* Return the handle corresponding to a thread id */
static inline pthread_handle thread_handle(int id)
{
  return &__pthread_handles[id];
}

/* thread structure for manager thread */
extern pth_t __pthread_manager_thread;

/* Lock for manager thread setup synchronization */
pth_qlock_t  manager_setup_lock;

/* Validate a thread id */
static inline int invalid_thread_id(pth_t id)
{
  return id->td_tid != id;
}

/* determine if handle is existing and valid */
static inline int nonexisting_handle(pthread_handle h, pth_t id)
{
  return h->h_descr == NULL || h->h_descr->td_tid != id;
}

/* Validate a thread handle. Must have acquired h->h_spinlock before. */
static inline int invalid_handle(pthread_handle h, pth_t id)
{
  return nonexisting_handle(h, id);
}

/* A thread event has occured, notify the thread manager */
extern void __pthread_signal_thread_event(pth_t curr, pth_t t, int td_event);

/* Initialize the thread manager */
extern int __pthread_initialize_manager(void);

/* complete a thread handle after thread tcb is built */
extern void __pthread_handle_complete (pth_t new_thread);

/* True if thread state indicates in exit */
#define td_thread_terminated(t)	((t)->state == PTH_STATE_DEAD || \
				 (t)->state == PTH_STATE_EXIT)

/* True if thread state indicates that it exited */
#define td_thread_exited(t)	((t)->state == PTH_STATE_EXIT)

/* True if thread state indicates that it can't be joined */
#define td_thread_detached(t)	((t)->joinable == 0)

/* Make a thread manager request and wait for completion */
static inline void td_manager_submit_request(struct pthread_request *req,
						       pth_qlock_t *lock)
{
    /* Submit the request, optionally with locking until completed */
    if ((req->req_thread_lock = lock) != NULL)
	/* Acquire on behalf of thread manager, who will release it */
	_pth_acquire_lock(lock, 1);
    pth_sc(write)(__pthread_manager_request,
				 (char *)req, sizeof(struct pthread_request));
    if (lock != NULL) {
	pth_acquire_lock(lock);		/* do acquire by current */
	pth_release_lock(lock);		/* do release by current */
    }
}

/* handle the restart signal */
void __pthread_handle_sigrestart(int sig);

/* handle a debug signal */
extern void __pthread_handle_sigdebug(int sig);

/* sync pth active thread list and __pthread_handles */
extern void __td_sync_handles(void);

/* wait for a restart signal, sync between manager and client threads */
extern void __pthread_wait_for_restart_signal(pth_t t);
