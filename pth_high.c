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
**  pth_high.c: Pth high-level replacement functions
*/
                             /* ``The difference between a computer
                                  industry job and open-source software
                                  hacking is about 30 hours a week.''
                                         -- Ralf S. Engelschall     */

/*
 *  These functions used by the applications instead of the
 *  regular Unix/POSIX functions. When the regular functions would
 *  block, these variants let only the thread sleep.
 */

#include "pth_p.h"
#include "allocation.h"
#ifdef THREAD_DB
#include "td_manager.h"
#endif

extern int sigisemptyset(__const sigset_t *) __THROW;
extern __off64_t lseek64 (int __fd, __off64_t __offset, int __whence) __THROW;

/* Common pth handler for sleep(3) and usleep(3) */
static unsigned int pth_sleep_time(pth_time_t *offset)
{
    pth_time_t until;
    pth_time_t back;
    pth_event_t ev, ev_sigs = NULL;
    static pth_key_t ev_key1 = PTH_KEY_INIT;
    static pth_key_t ev_key2 = PTH_KEY_INIT;
    int sig;
    pth_descr_t descr = pth_get_native_descr();
    pth_t current = descr->current;
    char c = (int)1;

    /* calculate asleep time */
    pth_time_set_now(&until);
    pth_time_add(&until, offset);
   
    /* and let thread sleep until this time is elapsed or signal */
    ev      = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key1, until);
    if (current && !sigisemptyset(&current->sigactionmask)) {
    	ev_sigs = pth_event(PTH_EVENT_TIME_SIGS|PTH_MODE_STATIC, &ev_key2, 
                            &current->sigactionmask, &sig, FALSE);
    	pth_event_concat(ev, ev_sigs, NULL);
    }
    pth_wait(ev);

    /* determine how woken up - full sleep quantum or signal? */
    pth_time_set_now(&back);
    pth_event_isolate(ev);
    /* if this is not first native, wakeup first native for sigcatch update */
    if (ev_sigs && (pth_first_native.tid != descr->tid)) {
        pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
    }
    if (!PTH_EVENT_OCCURRED(ev)) {
	struct timeval retval;
	if (ev_sigs && ev_sigs->ev_occurred && sig) {
#ifdef NATIVE_SELF
		k_tkill(NATIVE_SELF->tid, sig);
#else
		k_tkill(k_gettid(), sig);
#endif
	}
	pth_time_sub(&until, &back);
	retval = pth_time_to_timeval(&until);
	return retval.tv_sec <= 0 ? 1 : retval.tv_sec;
    }
    return 0;
}


/* Pth variant of usleep(3) */
int pth_usleep(unsigned int usec)
{
    pth_time_t offset;

    if (usec == 0)
	return 0;

    /* calculate asleep time */
    offset = PTH_TIME((long)(usec / 1000000), (long)(usec % 1000000));

    /* call common sleep routine... */
    pth_sleep_time(&offset);

    return 0;
}

/* Pth variant of sleep(3) */
unsigned int pth_sleep(unsigned int sec)
{
    pth_time_t offset;
    int rc = 0;

    if (sec == 0)
	return 0;

    /* calculate asleep time */
    offset = PTH_TIME(sec, 0);

    /* call common sleep routine and return */
    if ((rc = pth_sleep_time(&offset)) != 0)
	return_errno(rc, EINTR);

    return 0;
}
strong_alias(pth_sleep, __pthread_sleep)

extern int sigandset(sigset_t *, __const sigset_t *, __const sigset_t *) __THROW; 

/* Pth variant of POSIX pthread_sigmask(3) */
int pth_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
    int rv;
    char c = (int)1;
    sigset_t tmp;
    pth_descr_t descr = pth_get_native_descr();
    pth_t current = descr->current;

    pth_debug2("pth_sigmask: called with how = %d", how);

#ifdef THREAD_DB
    if (unlikely (__pthread_doing_debug))
	if (set != NULL) {
	    tmp = *set;
	    /* Don't allow __pthread_sig_restart to be unmasked. */
	    switch(how) {
		case SIG_SETMASK:
		    sigaddset(&tmp, __pthread_sig_restart);
		    break;
		case SIG_UNBLOCK:
		    sigdelset(&tmp, __pthread_sig_restart);
		    break;
	    }
	    set = &tmp;
	}
#endif

    /* change the explicitly remembered signal mask copy for the scheduler */
    if (set != NULL)
        pth_sc(sigprocmask)(SIG_SETMASK, &(current->mctx.sigs), NULL);
  
    /* change the real (per-thread saved/restored) signal mask */
    rv = pth_sc(sigprocmask)(how, set, oset);

    if (rv == -1)
	return errno;

    /* Now save away the modified signal mask... */
    pth_sc(sigprocmask)(SIG_SETMASK, NULL, &(current->mctx.sigs));

    if (set == NULL)
	return rv;
    
    sigfillset(&tmp);
    /* calculate the global signal mask */
    _pth_acquire_lock(&pth_sig_lock, descr->tid);
    if (!sigisemptyset(&pth_sigblock))
    	memcpy((void *)&tmp, &pth_sigblock, sizeof(sigset_t));
    sigandset(&tmp, &tmp, &(current->mctx.sigs));
    memcpy((void *)&pth_sigblock, &tmp, sizeof(sigset_t));
    _pth_release_lock(&pth_sig_lock, descr->tid);

    /* if this is not first native, wakeup first native for update */
    if ((pth_first_native.is_used) && 
        (pth_first_native.tid != descr->tid)) {
        pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
    }

    return rv;
}
strong_alias(pth_sigmask, pthread_sigmask)

/* Pth variant of POSIX sigwait(3) */
int pth_sigwait(const sigset_t *set, int *sigp)
{
    return pth_sigwait_ev(set, sigp, NULL);
}
strong_alias(pth_sigwait, __pthread_sigwait)

/* Pth variant of POSIX sigwait(3) with extra events */
int pth_sigwait_ev(const sigset_t *set, int *sigp, pth_event_t ev_extra)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    sigset_t pending;
    int sig;
    char c = (int)1;

    if (set == NULL || sigp == NULL)
        return EINVAL;

    /* check whether signal is already pending */
    if (sigpending(&pending) < 0)
        sigemptyset(&pending);
    for (sig = 1; sig < PTH_NSIG; sig++) {
#ifdef THREAD_DB
	/* if debugging don't let runtime debug signals be masked */
	if (unlikely (__pthread_doing_debug) &&
		(sig == __pthread_sig_restart ||
		 sig == __pthread_sig_cancel  ||
		 sig == __pthread_sig_debug) )
	    continue;
#endif
        if (sigismember(set, sig) && sigismember(&pending, sig)) {
            pth_util_sigdelete(sig);
            *sigp = sig;
            return 0;
        }
    }

    /* create event and wait on it */
    ev = pth_event(PTH_EVENT_SIGS|PTH_MODE_STATIC, &ev_key, set, sigp, TRUE);
    if (ev_extra != NULL)
        pth_event_concat(ev, ev_extra, NULL);
    
    pth_wait(ev);
    if (ev_extra != NULL) {
        pth_event_isolate(ev);
        if (!PTH_EVENT_OCCURRED(ev))
            return EINTR;
    }
    /* if this is not first native, wakeup first native for update */
    if ((pth_first_native.is_used) && 
        (pth_first_native.tid != current_tid())) {
        pth_sc(write)(pth_first_native.sigpipe[1], &c, sizeof(char));
    }

    /* nothing to do, scheduler has already set *sigp for us */
    return 0;
}

int pth_fcntl(int fd, int cmd, long arg)
{
    int rv;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
 
    ev = NULL;
    while ((rv = __libc_fcntl(fd, cmd, arg)) == -1 && (errno == EAGAIN)) {
	if (ev == NULL) {
	    ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key, PTH_TIMEOUT(0,100000));
	}
	pth_wait(ev);
    }
    return rv;
}

/* Pth variant of select(2) */
int pth_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout)
{
    return pth_select_ev(nfds, readfds, writefds, exceptfds, timeout, NULL);
}
strong_alias(pth_select, __pthread_select)

/* Pth variant of select(2) with extra events */
int pth_select_ev(int nfd, fd_set *rfds, fd_set *wfds,
                  fd_set *efds, struct timeval *timeout, pth_event_t ev_extra)
{
    struct timeval delay;
    pth_event_t ev;
    pth_event_t ev_select;
    pth_event_t ev_timeout;
    static pth_key_t ev_key_select  = PTH_KEY_INIT;
    static pth_key_t ev_key_timeout = PTH_KEY_INIT;
    fd_set rspare, wspare, espare;
    fd_set *rtmp, *wtmp, *etmp;
    int selected;
    int rc;

    pth_debug2("pth_select_ev: called from thread \"%s\"", pth_get_current()->name);

    /* first deal with the special situation of a plain microsecond delay */
    if (nfd == 0 && rfds == NULL && wfds == NULL && efds == NULL && timeout != NULL) {
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0)
            return_errno(-1, EINVAL);

        ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key_timeout,
                       PTH_TIMEOUT(timeout->tv_sec, timeout->tv_usec));
        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);
        pth_wait(ev);
        if (ev_extra != NULL) {
            pth_event_isolate(ev);
            if (!PTH_EVENT_OCCURRED(ev))
                return_errno(-1, EINTR);
        }
        /* POSIX compliance */
        if (rfds != NULL) FD_ZERO(rfds);
        if (wfds != NULL) FD_ZERO(wfds);
        if (efds != NULL) FD_ZERO(efds);
        return 0;
    }

    /* now directly poll filedescriptor sets to avoid unneccessary
       (and resource consuming because of context switches, etc) event
       handling through the scheduler. We've to be carefully here, because not
       all platforms guarranty us that the sets are unmodified when an error
       or timeout occured. */
    delay.tv_sec  = 0;
    delay.tv_usec = 0;
    rtmp = NULL;
    if (rfds != NULL) {
        rspare = *rfds;
        rtmp = &rspare;
    }
    wtmp = NULL;
    if (wfds != NULL) {
        wspare = *wfds;
        wtmp = &wspare;
    }
    etmp = NULL;
    if (efds != NULL) {
        espare = *efds;
        etmp = &espare;
    }
    while ((rc = pth_sc(select)(nfd, rtmp, wtmp, etmp, &delay)) < 0 && errno == EINTR) ;
    if (rc > 0) {
        if (rfds != NULL)
            *rfds = rspare;
        if (wfds != NULL)
            *wfds = wspare;
        if (efds != NULL)
            *efds = espare;
        return rc;
    }
    if (rc == 0 && timeout != NULL) {
        if (timeout->tv_sec == 0 && timeout->tv_usec == 0) {
            /* POSIX compliance */
            if (rfds != NULL) FD_ZERO(rfds);
            if (wfds != NULL) FD_ZERO(wfds);
            if (efds != NULL) FD_ZERO(efds);
            return 0;
        }
    }

    /* suspend current thread until one fd is ready or the timeout occurred */
    rc = -1;
    ev = ev_select = pth_event(PTH_EVENT_SELECT|PTH_MODE_STATIC,
                               &ev_key_select, &rc, nfd, rfds, wfds, efds);
    ev_timeout = NULL;
    if (timeout != NULL) {
        ev_timeout = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key_timeout,
                               PTH_TIMEOUT(timeout->tv_sec, timeout->tv_usec));
        pth_event_concat(ev, ev_timeout, NULL);
    }
    if (ev_extra != NULL)
        pth_event_concat(ev, ev_extra, NULL);
    pth_wait(ev);
    selected = FALSE;
    pth_event_isolate(ev_select);
    if (PTH_EVENT_OCCURRED(ev_select))
        selected = TRUE;
    if (timeout != NULL) {
        pth_event_isolate(ev_timeout);
        if (PTH_EVENT_OCCURRED(ev_timeout)) {
            selected = TRUE;
            /* POSIX compliance */
            if (rfds != NULL) FD_ZERO(rfds);
            if (wfds != NULL) FD_ZERO(wfds);
            if (efds != NULL) FD_ZERO(efds);
            rc = 0;
        }
    }
    if (ev_extra != NULL && !selected)
        return_errno(-1, EINTR);
    return rc;
}

/* Pth variant of select(2) */
int pth_poll(struct pollfd *pfd, nfds_t nfd, int timeout)
{
    return pth_poll_ev(pfd, nfd, timeout, NULL);
}
strong_alias(pth_poll, __pthread_poll)

/* Pth variant of poll(2) with extra events:
   NOTICE: THIS HAS TO BE BASED ON pth_select(2) BECAUSE
           INTERNALLY THE SCHEDULER IS ONLY select(2) BASED!! */

#define POLLEVENTS (POLLIN  | POLLRDNORM | POLLRDBAND | \
		    POLLOUT | POLLWRNORM | POLLWRBAND | \
		    POLLPRI)

int pth_poll_ev(struct pollfd *pfd, nfds_t nfd, int timeout, pth_event_t ev_extra)
{
    fd_set rfds, wfds, efds;
    struct timeval tv, *ptv;
    int maxfd, rc, ok;
    unsigned int i;
    char data[64];
    struct pollfd *pollfdp;

    pth_debug2("pth_poll_ev: called from thread \"%s\"", CURRENT_NAME());

    /* poll(2) semantics */
    if (pfd == NULL)
        return_errno(-1, EFAULT);

    /* convert timeout number into a timeval structure */
    ptv = &tv;
    if (timeout == 0) {
        /* return immediately */
        ptv->tv_sec  = 0;
        ptv->tv_usec = 0;
    }
    else if (timeout == INFTIM /* (-1) */) {
        /* wait forever */
        ptv = NULL;
    }
    else if (timeout > 0) {
        /* return after timeout */
        ptv->tv_sec  = timeout / 1000;
        ptv->tv_usec = (timeout % 1000) * 1000;
    }
    else
        return_errno(-1, EINVAL);

    /* create fd sets and determine max fd */
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    for(i = 0, maxfd = -1; i < nfd; i++) {
	pollfdp = &pfd[i];
        if (pollfdp->fd < 0 || (pollfdp->events & POLLEVENTS) == 0)
            continue;
        if (pollfdp->events & (POLLIN  | POLLRDNORM | POLLRDBAND))
            FD_SET(pollfdp->fd, &rfds);
        if (pollfdp->events & (POLLOUT | POLLWRNORM | POLLWRBAND))
            FD_SET(pollfdp->fd, &wfds);
        if (pollfdp->events & POLLPRI)
            FD_SET(pollfdp->fd, &efds);
        if (pollfdp->fd >= maxfd)
            maxfd = pollfdp->fd;
    }
    if (maxfd == -1)
        return_errno(-1, EINVAL);

    /* examine fd sets */
    rc = pth_select_ev(maxfd+1, &rfds, &wfds, &efds, ptv, ev_extra);

    /* establish results */
    if (rc > 0) {
        rc = 0;
        for (i = 0; i < nfd; i++) {
            ok = 0;
	    pollfdp = &pfd[i];
            pollfdp->revents = 0;
            if (pollfdp->fd < 0) {
                pollfdp->revents |= POLLNVAL;
                continue;
            }
            if (FD_ISSET(pollfdp->fd, &rfds)) {
                pollfdp->revents |= POLLIN;
                ok++;
                /* support for POLLHUP */
                if (__recv(pollfdp->fd, data, 64, MSG_PEEK) == -1) {
                    if (   errno == ESHUTDOWN    || errno == ECONNRESET
                        || errno == ECONNABORTED || errno == ENETRESET) {
                        pollfdp->revents &= ~(POLLIN);
                        pollfdp->revents |= POLLHUP;
                        ok--;
                    }
                }
            }
            if (FD_ISSET(pollfdp->fd, &wfds)) {
                pollfdp->revents |= POLLOUT;
                ok++;
            }
            if (FD_ISSET(pollfdp->fd, &efds)) {
		    pollfdp->revents |= POLLPRI;
                ok++;
            }
            if (ok)
                rc++;
        }
    }
    return rc;
}

/* Pth variant of connect(2) */
int pth_connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    return pth_connect_ev(s, addr, addrlen, NULL);
}
strong_alias(pth_connect, __pthread_connect)

/* Pth variant of connect(2) with extra events */
int pth_connect_ev(int s, const struct sockaddr *addr, socklen_t addrlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    int rv, err;
    socklen_t errlen;
    int fdmode;

    pth_debug2("pth_connect_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* force filedescriptor into non-blocking mode */
    fdmode = pth_fdmode(s, PTH_FDMODE_NONBLOCK);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    /* try to connect */
    while (   (rv = pth_sc(connect)(s, (struct sockaddr *)addr, addrlen)) == -1
           && errno == EINTR)
        ;

    /* restore filedescriptor mode */
    errno_shield ( pth_fdmode(s, fdmode); )

    /* when it is still on progress wait until socket is really writeable */
    if (rv == -1 && errno == EINPROGRESS) {
        ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE|PTH_MODE_STATIC, &ev_key, s);
        if (ev_extra != NULL)
            pth_event_concat(ev, ev_extra, NULL);
        pth_wait(ev);
        if (ev_extra != NULL) {
            pth_event_isolate(ev);
            if (!PTH_EVENT_OCCURRED(ev))
                return_errno(-1, EINTR);
        }
        errlen = sizeof(err);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen) == -1)
            return -1;
        if (err == 0)
            return 0;
        return_errno(rv, err);
    }

    pth_debug2("pth_connect_ev: leave to thread \"%s\"", CURRENT_NAME());
    return rv;
}

/* Pth variant of accept(2) */
int pth_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    return pth_accept_ev(s, addr, addrlen, NULL);
}
strong_alias(pth_accept, __pthread_accept)

/* Pth variant of accept(2) with extra events */
int pth_accept_ev(int s, struct sockaddr *addr, socklen_t *addrlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    int fdmode;
    int rv;

    pth_debug2("pth_accept_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* force filedescriptor into non-blocking mode */
    fdmode = pth_fdmode(s, PTH_FDMODE_NONBLOCK);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    /* poll socket via accept */
    ev = NULL;
    while ((rv = pth_sc(accept)(s, addr, addrlen)) == -1
           && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* do lazy event allocation */
        if (ev == NULL) {
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, s);
            if (ev_extra != NULL)
                pth_event_concat(ev, ev_extra, NULL);
        }
        /* wait until accept has a chance */
        pth_wait(ev);
        /* check for the extra events */
        if (ev_extra != NULL) {
            pth_event_isolate(ev);
            if (!PTH_EVENT_OCCURRED(ev)) {
                pth_fdmode(s, fdmode);
                return_errno(-1, EINTR);
            }
        }
    }

    /* restore filedescriptor mode */
    errno_shield (
        pth_fdmode(s, fdmode);
        if (rv != -1)
            pth_fdmode(rv, fdmode);
    )

    pth_debug2("pth_accept_ev: leave to thread \"%s\"", CURRENT_NAME());
    return rv;
}

/* Pth variant of read(2) */
ssize_t pth_read(int fd, void *buf, size_t nbytes)
{
    return pth_read_ev(fd, buf, nbytes, NULL);
}
strong_alias(pth_read, __pthread_read)

/* Pth variant of read(2) with extra event(s) */
ssize_t pth_read_ev(int fd, void *buf, size_t nbytes, pth_event_t ev_extra)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    int fdmode;
    fd_set fds;
    int n;

    pth_debug2("pth_read_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;

    /* poll filedescriptor when not already in non-blocking operation */
    fdmode = pth_fdmode(fd, PTH_FDMODE_POLL);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    if (fdmode == PTH_FDMODE_BLOCK) {
        /* now directly poll filedescriptor for readability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, &fds, NULL, NULL, &delay)) < 0
               && errno == EINTR) ;

        /* when filedescriptor is still not readable,
           let thread sleep until it is or the extra event occurs */
        if (n < 1) {
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, fd);
            if (ev_extra != NULL)
                pth_event_concat(ev, ev_extra, NULL);
            n = pth_wait(ev);
            if (ev_extra != NULL) {
                pth_event_isolate(ev);
                if (!PTH_EVENT_OCCURRED(ev))
                    return_errno(-1, EINTR);
            }
        }
    }

    /* Now perform the actual read. We're now guarrantied to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next read(2) call will not block.  But keep
       in mind, that only 1 next read(2) call is guarrantied to not block
       (except for the EINTR situation). */
    while ((n = pth_sc(read)(fd, buf, nbytes)) < 0
           && errno == EINTR) ;

    pth_debug2("pth_read_ev: leave to thread \"%s\"", CURRENT_NAME());
    return n;
}

/* Pth variant of write(2) */
ssize_t pth_write(int fd, const void *buf, size_t nbytes)
{
    return pth_write_ev(fd, buf, nbytes, NULL);
}
strong_alias(pth_write, __pthread_write)

/* Pth variant of write(2) with extra event(s) */
ssize_t pth_write_ev(int fd, const void *buf, size_t nbytes, pth_event_t ev_extra)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    fd_set fds;
    int fdmode;
    ssize_t rv;
    ssize_t s;
    int n;

    pth_debug2("pth_write_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;

    /* force filedescriptor into non-blocking mode */
    fdmode = pth_fdmode(fd, PTH_FDMODE_NONBLOCK);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    /* Now perform the actual write operation */
    while ((rv = pth_sc(write)(fd, buf, nbytes)) < 0
              && errno == EINTR) ;

    /* poll filedescriptor when not already in non-blocking operation */
    if (fdmode != PTH_FDMODE_NONBLOCK && rv == -1 && errno == EAGAIN) {

	/* restore filedescriptor mode */
	errno_shield ( pth_fdmode(fd, fdmode); )

        /* now directly poll filedescriptor for writeability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, NULL, &fds, NULL, &delay)) < 0
               && errno == EINTR) ;

        rv = 0;
        for (;;) {
            /* when filedescriptor is still not writeable,
               let thread sleep until it is or event occurs */
            if (n < 1) {
                ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE|PTH_MODE_STATIC, &ev_key, fd);
                if (ev_extra != NULL)
                    pth_event_concat(ev, ev_extra, NULL);
                pth_wait(ev);
                if (ev_extra != NULL) {
                    pth_event_isolate(ev);
                    if (!PTH_EVENT_OCCURRED(ev)) {
                        pth_fdmode(fd, fdmode);
                        return_errno(-1, EINTR);
                    }
                }
            }

            /* now perform the actual write operation */
            while ((s = pth_sc(write)(fd, buf, nbytes)) < 0
                   && errno == EINTR) ;
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of write(2). */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                buf = (void *)((char *)buf + s);
                n = 0;
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    } else
	/* restore filedescriptor mode */
	errno_shield ( pth_fdmode(fd, fdmode); )

    pth_debug3("pth_write_ev: leave to thread \"%s\" returns %d", CURRENT_NAME(), rv);
    return rv;
}

/* Pth variant of waitpid(2) */
pid_t pth_waitpid(pid_t wpid, int *status, int options)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    pid_t pid;

    pth_debug2("pth_waitpid: called from thread \"%s\"", CURRENT_NAME());

    for (;;) {
        /* do a non-blocking poll for the pid */
        while (   (pid = pth_sc(waitpid)(wpid, status, options|WNOHANG)) < 0 
               && errno == EINTR) ;

        /* if pid was found or caller requested a polling return immediately */
        if (pid == -1 || pid > 0 || (pid == 0 && (options & WNOHANG)))
            break;

        /* else wait a little bit */
        ev = pth_event(PTH_EVENT_TIME|PTH_MODE_STATIC, &ev_key, PTH_TIMEOUT(0,250000));
        pth_wait(ev);
    }

    pth_debug2("pth_waitpid: leave to thread \"%s\"", CURRENT_NAME());
    return pid;
}
strong_alias(pth_waitpid, __pthread_waitpid)

/* Pth variant of readv(2) */
ssize_t pth_readv(int fd, const struct iovec *iov, int iovcnt)
{
    return pth_readv_ev(fd, iov, iovcnt, NULL);
}
strong_alias(pth_readv, __pthread_readv)

/* Pth variant of readv(2) with extra event(s) */
ssize_t pth_readv_ev(int fd, const struct iovec *iov, int iovcnt, pth_event_t ev_extra)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    fd_set fds;
    int n;
    int fdmode;

    pth_debug2("pth_readv_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (iovcnt <= 0 || iovcnt > UIO_MAXIOV)
        return_errno(-1, EINVAL);

    /* poll filedescriptor when not already in non-blocking operation */
    fdmode = pth_fdmode(fd, PTH_FDMODE_POLL);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    if (fdmode == PTH_FDMODE_BLOCK) {
        /* first directly poll filedescriptor for readability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, &fds, NULL, NULL, &delay)) < 0
               && errno == EINTR) ;

        /* when filedescriptor is still not readable,
           let thread sleep until it is or event occurs */
        if (n < 1) {
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, fd);
            if (ev_extra != NULL)
                pth_event_concat(ev, ev_extra, NULL);
            n = pth_wait(ev);
            if (ev_extra != NULL) {
                pth_event_isolate(ev);
                if (!PTH_EVENT_OCCURRED(ev))
                    return_errno(-1, EINTR);
            }
        }
    }

    /* Now perform the actual read. We're now guarrantied to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next read(2) call will not block.  But keep
       in mind, that only 1 next read(2) call is guarrantied to not block
       (except for the EINTR situation). */
#if PTH_FAKE_RWV
    while ((n = pth_readv_faked(fd, iov, iovcnt)) < 0
           && errno == EINTR) ;
#else
    while ((n = pth_sc(readv)(fd, iov, iovcnt)) < 0
           && errno == EINTR) ;
#endif

    pth_debug2("pth_readv_ev: leave to thread \"%s\"", CURRENT_NAME());
    return n;
}

/* A faked version of readv(2) */
intern ssize_t pth_readv_faked(int fd, const struct iovec *iov, int iovcnt)
{
    char *buffer;
    size_t bytes, copy, rv;
    int i;

    /* determine total number of bytes to read */
    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            return_errno((ssize_t)(-1), EINVAL);
        bytes += iov[i].iov_len;
    }
    if (bytes <= 0)
        return_errno((ssize_t)(-1), EINVAL);

    /* allocate a temporary buffer */
    if ((buffer = (char *)pth_malloc(bytes)) == NULL)
        return (ssize_t)(-1);

    /* read data into temporary buffer (caller guarrantied us to not block) */
    rv = pth_sc(read)(fd, buffer, bytes);

    /* scatter read data into callers vector */
    if (rv > 0) {
        bytes = rv;
        for (i = 0; i < iovcnt; i++) {
            copy = pth_util_min(iov[i].iov_len, bytes);
            memcpy(iov[i].iov_base, buffer, copy);
            buffer += copy;
            bytes  -= copy;
            if (bytes <= 0)
                break;
        }
    }

    /* remove the temporary buffer */
    pth_free_mem(buffer, bytes);

    /* return number of read bytes */
    return(rv);
}

/* Pth variant of writev(2) */
ssize_t pth_writev(int fd, const struct iovec *iov, int iovcnt)
{
    return pth_writev_ev(fd, iov, iovcnt, NULL);
}
strong_alias(pth_writev, __pthread_writev)

/* Pth variant of writev(2) with extra event(s) */
ssize_t pth_writev_ev(int fd, const struct iovec *iov, int iovcnt, pth_event_t ev_extra)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    fd_set fds;
    int fdmode;
    struct iovec *liov;
    int liovcnt;
    size_t nbytes;
    ssize_t rv;
    ssize_t s;
    int n;

    pth_debug2("pth_writev_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (iovcnt <= 0 || iovcnt > UIO_MAXIOV)
        return_errno(-1, EINVAL);

    /* force filedescriptor into non-blocking mode */
    fdmode = pth_fdmode(fd, PTH_FDMODE_NONBLOCK);
    if (fdmode == PTH_FDMODE_ERROR)
          return_errno(-1, EBADF);

    /* Now perform the actual write operation */
#if PTH_FAKE_RWV
    while ((rv = pth_writev_faked(fd, iov, iovcnt)) < 0
               && errno == EINTR) ;
#else
    while ((rv = pth_sc(writev)(fd, iov, iovcnt)) < 0
               && errno == EINTR) ;
#endif

    /* poll filedescriptor when not already in non-blocking operation */
    if (fdmode != PTH_FDMODE_NONBLOCK && rv == -1 && errno == EAGAIN) {

	/* restore filedescriptor mode */
	errno_shield ( pth_fdmode(fd, fdmode); )

        /* init return value and number of bytes to write */
        rv      = 0;
        nbytes  = pth_writev_iov_bytes(iov, iovcnt);

        /* init local iovec structure */
        liov    = NULL;
        liovcnt = 0;
        pth_writev_iov_advance(iov, iovcnt, 0, &liov, &liovcnt);

        /* first directly poll filedescriptor for writeability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, NULL, &fds, NULL, &delay)) < 0
               && errno == EINTR) ;

        for (;;) {
            /* when filedescriptor is still not writeable,
               let thread sleep until it is or event occurs */
            if (n < 1) {
                ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE|PTH_MODE_STATIC, &ev_key, fd);
                if (ev_extra != NULL)
                    pth_event_concat(ev, ev_extra, NULL);
                pth_wait(ev);
                if (ev_extra != NULL) {
                    pth_event_isolate(ev);
                    if (!PTH_EVENT_OCCURRED(ev)) {
                        pth_fdmode(fd, fdmode);
                        return_errno(-1, EINTR);
                    }
                }
            }

            /* Now perform the actual write operation */
#if PTH_FAKE_RWV
            while ((s = pth_writev_faked(fd, liov, liovcnt)) < 0
                   && errno == EINTR) ;
#else
            while ((s = pth_sc(writev)(fd, liov, liovcnt)) < 0
                   && errno == EINTR) ;
#endif
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of writev(2) */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                pth_writev_iov_advance(iov, iovcnt, n, &liov, &liovcnt);
                n = 0;
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    } else
	/* restore filedescriptor mode */
	errno_shield ( pth_fdmode(fd, fdmode); )

    pth_debug2("pth_writev_ev: leave to thread \"%s\"", CURRENT_NAME());
    return rv;
}

/* calculate number of bytes in a struct iovec */
intern ssize_t pth_writev_iov_bytes(const struct iovec *iov, int iovcnt)
{
    ssize_t bytes;
    int i;

    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            continue;
        bytes += iov[i].iov_len;
    }
    return bytes;
}

/* advance the virtual pointer of a struct iov */
intern void pth_writev_iov_advance(const struct iovec *riov, int riovcnt, size_t advance,
                                   struct iovec **liov, int *liovcnt)
{
    static struct iovec siov[UIO_MAXIOV];
    int i;

    if (*liov == NULL && *liovcnt == 0) {
        /* initialize with real (const) structure on first step */
        *liov = (struct iovec *)riov;
        *liovcnt = riovcnt;
    }
    if (advance > 0) {
        if (*liov == riov && *liovcnt == riovcnt) {
            /* reinitialize with a copy to be able to adjust it */
            *liov = &siov[0];
            for (i = 0; i < riovcnt; i++) {
                siov[i].iov_base = riov[i].iov_base;
                siov[i].iov_len  = riov[i].iov_len;
            }
        }
        /* advance the virtual pointer */
        while (*liovcnt > 0 && advance > 0) {
            if ((*liov)->iov_len > advance) {
                (*liov)->iov_base = (char *)((*liov)->iov_base) + advance;
                (*liov)->iov_len -= advance;
                break;
            }
            else {
                advance -= (*liov)->iov_len;
                (*liovcnt)--;
                (*liov)++;
            }
        }
    }
    return;
}

/* A faked version of writev(2) */
intern ssize_t pth_writev_faked(int fd, const struct iovec *iov, int iovcnt)
{
    char *buffer, *cp;
    size_t bytes, to_copy, copy, rv;
    int i;

    /* determine total number of bytes to write */
    bytes = 0;
    for (i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0)
            return_errno((ssize_t)(-1), EINVAL);
        bytes += iov[i].iov_len;
    }
    if (bytes <= 0)
        return_errno((ssize_t)(-1), EINVAL);

    /* allocate a temporary buffer to hold the data */
    if ((buffer = (char *)pth_malloc(bytes)) == NULL)
        return (ssize_t)(-1);

    /* concatenate the data from callers vector into buffer */
    to_copy = bytes;
    cp = buffer;
    for (i = 0; i < iovcnt; i++) {
         copy = pth_util_min(iov[i].iov_len, to_copy);
         memcpy(cp, iov[i].iov_base, copy);
         to_copy -= copy;
         if (to_copy <= 0)
             break;
    }

    /* write continuous chunck of data (caller guarrantied us to not block) */
    rv = pth_sc(write)(fd, buffer, bytes);

    /* remove the temporary buffer */
    pth_free_mem(buffer, bytes);

    return(rv);
}

/* Pth variant of POSIX pread(3) */
ssize_t pth_pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    static pth_mutex_t mutex = PTH_MUTEX_INIT;
    off_t old_offset;
    ssize_t rc;

    /* protect us: pth_read can yield! */
    if (pth_mutex_acquire(&mutex, FALSE, NULL) != 0)
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek(fd, 0, SEEK_CUR)) == (off_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek(fd, offset, SEEK_SET) == (off_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }

    /* perform the read operation */
    rc = pth_read(fd, buf, nbytes);

    /* restore the old offset situation */
    errno_shield ( lseek(fd, old_offset, SEEK_SET); )

    /* unprotect and return result of read */
    pth_mutex_release(&mutex);
    return rc;
}
strong_alias(pth_pread, __pthread_pread)

/* Pth variant of POSIX pwrite(3) */
ssize_t pth_pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    static pth_mutex_t mutex = PTH_MUTEX_INIT;
    off_t old_offset;
    ssize_t rc;

    /* protect us: pth_write can yield! */
    if (pth_mutex_acquire(&mutex, FALSE, NULL) != 0)
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek(fd, 0, SEEK_CUR)) == (off_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek(fd, offset, SEEK_SET) == (off_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }

    /* perform the write operation */
    rc = pth_write(fd, buf, nbytes);

    /* restore the old offset situation */
    errno_shield ( lseek(fd, old_offset, SEEK_SET); )

    /* unprotect and return result of write */
    pth_mutex_release(&mutex);
    return rc;
}
strong_alias(pth_pwrite, __pthread_pwrite)

/* Pth variant of POSIX pread64(3) */
ssize_t pth_pread64(int fd, void *buf, size_t nbytes, __off64_t offset)
{
    static pth_mutex_t mutex = PTH_MUTEX_INIT;
    __off64_t old_offset;
    ssize_t rc;

    /* protect us: pth_read can yield! */
    if (pth_mutex_acquire(&mutex, FALSE, NULL) != 0)
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek64(fd, 0, SEEK_CUR)) == (__off64_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek64(fd, offset, SEEK_SET) == (__off64_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }

    /* perform the read operation */
    rc = pth_read(fd, buf, nbytes);

    /* restore the old offset situation */
    errno_shield ( lseek64(fd, old_offset, SEEK_SET); )

    /* unprotect and return result of read */
    pth_mutex_release(&mutex);
    return rc;
}

/* Pth variant of POSIX pwrite64(3) */
ssize_t pth_pwrite64(int fd, const void *buf, size_t nbytes, __off64_t offset)
{
    static pth_mutex_t mutex = PTH_MUTEX_INIT;
    __off64_t old_offset;
    ssize_t rc;

    /* protect us: pth_write can yield! */
    if (pth_mutex_acquire(&mutex, FALSE, NULL) != 0)
        return (-1);

    /* remember current offset */
    if ((old_offset = lseek64(fd, 0, SEEK_CUR)) == (__off64_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }
    /* seek to requested offset */
    if (lseek64(fd, offset, SEEK_SET) == (__off64_t)(-1)) {
        pth_mutex_release(&mutex);
        return (-1);
    }

    /* perform the write operation */
    rc = pth_write(fd, buf, nbytes);

    /* restore the old offset situation */
    errno_shield ( lseek64(fd, old_offset, SEEK_SET); )

    /* unprotect and return result of write */
    pth_mutex_release(&mutex);
    return rc;
}

/* Pth variant of SUSv2 recvmsg(2) */
ssize_t pth_recvmsg(int fd, struct msghdr *msg, int flags)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    fd_set fds;
    int n;

    pth_debug2("pth_recvmsg: enter from thread \"%s\"", CURRENT_NAME());

    /* poll filedescriptor when not already in non-blocking operation */
    if (pth_fdmode(fd, PTH_FDMODE_POLL) == PTH_FDMODE_BLOCK) {

        /* now directly poll filedescriptor for readability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, &fds, NULL, NULL, &delay)) < 0
               && errno == EINTR) ;

        /* when filedescriptor is still not readable,
           let thread sleep until it is or the extra event occurs */
        if (n < 1) {
            ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, fd);
            n = pth_wait(ev);
        }
    }

    /* Now perform the actual read. We're now guarrantied to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next recvmsg(2) call will not block.  But keep
       in mind, that only 1 next recvmsg(2) call is guarrantied to not block
       (except for the EINTR situation). */
    while ((n = pth_sc(recvmsg)(fd, msg, flags)) < 0
           && errno == EINTR) ;

    pth_debug2("pth_recvmsg: leave to thread \"%s\"", CURRENT_NAME());
    return n;
}
strong_alias(pth_recvmsg, __pthread_recvmsg)

/* Pth variant of SUSv2 sendmsg(2) */
ssize_t pth_sendmsg(int fd, const struct msghdr *msg, int flags)
{
    struct timeval delay;
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    fd_set fds;
    ssize_t rv;
    ssize_t s;
    int n;

    pth_debug2("pth_sendmsg: enter from thread \"%s\"", CURRENT_NAME());

    /* Now perform the actual send operation */
    while ((rv = pth_sc(sendmsg)(fd, msg, flags|MSG_DONTWAIT)) < 0
            && errno == EINTR) ;

    /* poll filedescriptor when not already in non-blocking operation */
    if (rv == -1 && errno == EAGAIN && (!(flags & MSG_DONTWAIT)) &&
	(pth_fdmode(fd, PTH_FDMODE_POLL) == PTH_FDMODE_BLOCK)) { 

        /* now directly poll filedescriptor for writeability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        delay.tv_sec  = 0;
        delay.tv_usec = 0;
        while ((n = pth_sc(select)(fd+1, NULL, &fds, NULL, &delay)) < 0
               && errno == EINTR) ;

        rv = 0;
        for (;;) {
            /* when filedescriptor is still not writeable,
               let thread sleep until it is or event occurs */
            if (n < 1) {
                ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE|PTH_MODE_STATIC, &ev_key, fd);
                pth_wait(ev);
            }

            /* now perform the actual send operation */
            while ((s = pth_sc(sendmsg)(fd, msg, flags)) < 0
                   && errno == EINTR) ;
            if (s > 0)
                rv += s;

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    }

    pth_debug2("pth_sendmsg: leave to thread \"%s\"", CURRENT_NAME());
    return rv;
}
strong_alias(pth_sendmsg, __pthread_sendmsg)

/* Pth variant of SUSv2 recv(2) */
ssize_t pth_recv(int s, void *buf, size_t len, int flags)
{
    return pth_recvfrom_ev(s, buf, len, flags, NULL, 0, NULL);
}
strong_alias(pth_recv, __pthread_recv)

/* Pth variant of SUSv2 recv(2) with extra event(s) */
ssize_t pth_recv_ev(int s, void *buf, size_t len, int flags, pth_event_t ev)
{
    return pth_recvfrom_ev(s, buf, len, flags, NULL, 0, ev);
}

/* Pth variant of SUSv2 recvfrom(2) */
ssize_t pth_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    return pth_recvfrom_ev(s, buf, len, flags, from, fromlen, NULL);
}
strong_alias(pth_recvfrom, __pthread_recvfrom)

/* Pth variant of SUSv2 recvfrom(2) with extra event(s) */
ssize_t pth_recvfrom_ev(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen, pth_event_t ev_extra)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    int n;
    fd_set fds;
    struct timeval delay;

    pth_debug2("pth_recvfrom_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;

    /* now directly poll filedescriptor for readability
       to avoid unneccessary (and resource consuming because of context
       switches, etc) event handling through the scheduler */

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    delay.tv_sec  = 0;
    delay.tv_usec = 0;
    while ((n = pth_sc(select)(fd+1, &fds, NULL, NULL, &delay)) < 0
	    && errno == EINTR) ;

    /* when filedescriptor is still not readable,
       let thread sleep until it is or the extra event occurs */
    if (n < 1 && (pth_fdmode(fd, PTH_FDMODE_POLL) == PTH_FDMODE_BLOCK)) {
	ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_READABLE|PTH_MODE_STATIC, &ev_key, fd);
	if (ev_extra != NULL)
	    pth_event_concat(ev, ev_extra, NULL);
	n = pth_wait(ev);
	if (ev_extra != NULL) {
	    pth_event_isolate(ev);
	    if (!PTH_EVENT_OCCURRED(ev))
		return_errno(-1, EINTR);
	}
    }

    /* Now perform the actual read. We're now guarrantied to not block,
       either because we were already in non-blocking mode or we determined
       above by polling that the next recvfrom(2) call will not block.  But keep
       in mind, that only 1 next recvfrom(2) call is guarrantied to not block
       (except for the EINTR situation). */
    while ((n = pth_sc(recvfrom)(fd, buf, nbytes, flags, from, fromlen)) < 0
           && errno == EINTR) ;

    pth_debug2("pth_recvfrom_ev: leave to thread \"%s\"", CURRENT_NAME());
    return n;
}

/* Pth variant of SUSv2 send(2) */
ssize_t pth_send(int s, const void *buf, size_t len, int flags)
{
    return pth_sendto_ev(s, buf, len, flags, NULL, 0, NULL);
}
strong_alias(pth_send, __pthread_send)

/* Pth variant of SUSv2 send(2) with extra event(s) */
ssize_t pth_send_ev(int s, const void *buf, size_t len, int flags, pth_event_t ev)
{
    return pth_sendto_ev(s, buf, len, flags, NULL, 0, ev);
}

/* Pth variant of SUSv2 sendto(2) */
ssize_t pth_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    return pth_sendto_ev(s, buf, len, flags, to, tolen, NULL);
}
strong_alias(pth_sendto, __pthread_sendto)

/* Pth variant of SUSv2 sendto(2) with extra event(s) */
ssize_t pth_sendto_ev(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen, pth_event_t ev_extra)
{
    pth_event_t ev;
    static pth_key_t ev_key = PTH_KEY_INIT;
    ssize_t rv;
    ssize_t s;
    int n;
    fd_set fds;
    struct timeval delay;

    pth_debug2("pth_sendto_ev: enter from thread \"%s\"", CURRENT_NAME());

    /* POSIX compliance */
    if (nbytes == 0)
        return 0;

    /* Now perform the actual send operation */
    while ((rv = pth_sc(sendto)(fd, buf, nbytes, flags|MSG_DONTWAIT, to, tolen)) < 0
            && errno == EINTR) ;

    /* poll filedescriptor when not already in non-blocking operation */
    if (rv == -1 && errno == EAGAIN && (!(flags & MSG_DONTWAIT)) &&
	(pth_fdmode(fd, PTH_FDMODE_POLL) == PTH_FDMODE_BLOCK)) { 

        /* now directly poll filedescriptor for writeability
           to avoid unneccessary (and resource consuming because of context
           switches, etc) event handling through the scheduler */

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	delay.tv_sec  = 0;
	delay.tv_usec = 0;
	while ((n = pth_sc(select)(fd+1, NULL, &fds, NULL, &delay)) < 0
		&& errno == EINTR) ;

        rv = 0;
        for (;;) {
            /* when filedescriptor is still not writeable,
               let thread sleep until it is or event occurs */
            if (n < 1) {
                ev = pth_event(PTH_EVENT_FD|PTH_UNTIL_FD_WRITEABLE|PTH_MODE_STATIC, &ev_key, fd);
                if (ev_extra != NULL)
                    pth_event_concat(ev, ev_extra, NULL);
                pth_wait(ev);
                if (ev_extra != NULL) {
                    pth_event_isolate(ev);
                    if (!PTH_EVENT_OCCURRED(ev)) {
                        return_errno(-1, EINTR);
                    }
                }
            }

            /* now perform the actual send operation */
            while ((s = pth_sc(sendto)(fd, buf, nbytes, flags, to, tolen)) < 0
                   && errno == EINTR) ;
            if (s > 0)
                rv += s;

            /* although we're physically now in non-blocking mode,
               iterate unless all data is written or an error occurs, because
               we've to mimic the usual blocking I/O behaviour of write(2). */
            if (s > 0 && s < (ssize_t)nbytes) {
                nbytes -= s;
                buf = (void *)((char *)buf + s);
                n = 0;
                continue;
            }

            /* pass error to caller, but not for partial writes (rv > 0) */
            if (s < 0 && rv == 0)
                rv = -1;

            /* stop looping */
            break;
        }
    }

    pth_debug2("pth_sendto_ev: leave to thread \"%s\"", CURRENT_NAME());
    return rv;
}

