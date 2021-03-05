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
**  pth_syscall.c: Pth direct syscall support
*/
                             /* ``Free Software: generous programmers
                                  from around the world all join
                                  forces to help you shoot yourself
                                  in the foot for free.''
                                                 -- Unknown         */
#include "pth_p.h"
#include "queue.h"

#if cpp

#if PTH_SYSCALL_HARD
#include <sys/syscall.h>
#ifdef HAVE_SYS_SOCKETCALL_H
#include <sys/socketcall.h>
#else
#if defined(__linux__)
#define SOCKOP_socket		1
#define SOCKOP_bind		2
#define SOCKOP_connect		3
#define SOCKOP_listen		4
#define SOCKOP_accept		5
#define SOCKOP_getsockname	6
#define SOCKOP_getpeername	7
#define SOCKOP_socketpair	8
#define SOCKOP_send		9
#define SOCKOP_recv		10
#define SOCKOP_sendto		11
#define SOCKOP_recvfrom		12
#define SOCKOP_shutdown		13
#define SOCKOP_setsockopt	14
#define SOCKOP_getsockopt	15
#define SOCKOP_sendmsg		16
#define SOCKOP_recvmsg		17
#endif
#endif
#define pth_sc(func) PTH_SC_##func
#else
#define pth_sc(func) func
#endif

#endif /* cpp */

/* some exported variables for object layer checks */
int pth_syscall_soft = PTH_SYSCALL_SOFT;
int pth_syscall_hard = PTH_SYSCALL_HARD;

extern int __modify_ldt (int, struct modify_ldt_ldt_s *, size_t) 
     __attribute__ ((weak));
extern void __libc_siglongjmp (sigjmp_buf env, int val)
     __attribute__ ((noreturn));
extern void __libc_longjmp (sigjmp_buf env, int val)
     __attribute__ ((noreturn));
#if cpp
extern int __libc_sigaction (int sig, const struct sigaction *act, struct sigaction *oldact)
     __attribute__ ((weak));
extern int __libc_fcntl(int fd, int cmd, ...)
     __attribute__ ((weak));
#endif
extern int __libc_close(int fd)
     __attribute__ ((weak));
extern int __libc_fsync(int fd)
     __attribute__ ((weak));
extern int __libc_lseek(int fd, off_t offset, int whence)
     __attribute__ ((weak));
extern __off64_t __libc_lseek64(int fd, __off64_t offset, int whence)
     __attribute__ ((weak));
extern int __libc_msync(__ptr_t addr, size_t length, int flags)
     __attribute__ ((weak));
extern int __libc_nanosleep(const struct timespec *requested_time, struct timespec *remaining)
     __attribute__ ((weak));
extern int __libc_open(const char *pathname, int flags, ...)
     __attribute__ ((weak));
extern int __libc_pause(void)
     __attribute__ ((weak));
extern int __libc_system(const char *line)
     __attribute__ ((weak));
extern int __libc_tcdrain(int fd)
     __attribute__ ((weak));
extern pid_t __libc_wait(__WAIT_STATUS_DEFN stat_loc)
     __attribute__ ((weak));


/* Pth hard wrapper for syscall close(3) */
#if cpp
#if defined(SYS_close)
#define PTH_SC_close() ((void)syscall(SYS_close))
#else
#define PTH_SC_close __close
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __close(int fd);
int __close(int fd)
{
    return __libc_close(fd);
}
strong_alias (__close, close)
#endif

/* Pth hard wrapper for syscall fcntl(3) */
#if cpp
#if defined(SYS_fcntl)
#define PTH_SC_fcntl() ((void)syscall(SYS_fcntl))
#else
#define PTH_SC_fcntl __fcntl
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __fcntl(int fd, int cmd, long arg);
int __fcntl(int fd, int cmd, long arg)
{
    if (pth_number_of_natives == 1 && cmd == F_SETLKW)
	return pth_fcntl(fd, F_SETLK, arg);
    return __libc_fcntl(fd, cmd, arg);
}
strong_alias (__fcntl, fcntl)
#endif

/* Pth hard wrapper for syscall fsync(3) */
#if cpp
#if defined(SYS_fsync)
#define PTH_SC_fsync() ((void)syscall(SYS_fsync))
#else
#define PTH_SC_fsync __fsync
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __fsync(int fd);
int __fsync(int fd)
{
    return __libc_fsync(fd);
}
strong_alias (__fsync, fsync)
#endif

/* Pth hard wrapper for syscall lseek(3) */
#if cpp
#if defined(SYS_lseek)
#define PTH_SC_lseek() ((void)syscall(SYS_lseek))
#else
#define PTH_SC_lseek __lseek
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __lseek(int fd, off_t offset, int whence);
int __lseek(int fd, off_t offset, int whence)
{
    return __libc_lseek(fd, offset, whence);
}
strong_alias (__lseek, lseek)
#endif

/* Pth hard wrapper for syscall lseek64(3) */
#if cpp
#if defined(SYS_lseek64)
#define PTH_SC_lseek64() ((void)syscall(SYS_lseek64))
#else
#define PTH_SC_lseek64 lseek64
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern __off64_t lseek64(int fd, __off64_t offset, int whence);
__off64_t lseek64(int fd, __off64_t offset, int whence)
{
    return __libc_lseek64(fd, offset, whence);
}
#endif

/* Pth hard wrapper for syscall msync(3) */
#if cpp
#if defined(SYS_msync)
#define PTH_SC_msync() ((void)syscall(SYS_msync))
#else
#define PTH_SC_msync __msync
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __msync(__ptr_t addr, size_t length, int flags);
int __msync(__ptr_t addr, size_t length, int flags)
{
    return __libc_msync(addr, length, flags);
}
strong_alias (__msync, msync)
#endif

/* Pth hard wrapper for syscall nanosleep(3) */
#if cpp
#if defined(SYS_nanosleep)
#define PTH_SC_nanosleep() ((void)syscall(SYS_nanosleep))
#else
#define PTH_SC_nanosleep __nanosleep
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __nanosleep(const struct timespec *requested_time, struct timespec *remaining);
int __nanosleep(const struct timespec *requested_time, struct timespec *remaining)
{
    int rc;
    pth_descr_t descr = pth_get_native_descr();
    rc = __libc_nanosleep(requested_time, remaining);
    if (!rc && !descr->is_bounded && pth_threads_count <= 5)
	pth_yield(NULL);
    return rc;
}
strong_alias (__nanosleep, nanosleep)
#endif

/* Pth hard wrapper for syscall open(3) */
#if cpp
#if defined(SYS_open)
#define PTH_SC_open() ((void)syscall(SYS_open))
#else
#define PTH_SC_open __open
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __open(const char *pathname, int flags, int mode);
int __open(const char *pathname, int flags, int mode)
{
    return __libc_open(pathname, flags, mode);
}
strong_alias (__open, open)
#endif

/* Pth hard wrapper for syscall open64(3) */
extern int __open64(const char *pathname, int flags, int mode);
int __open64(const char *pathname, int flags, int mode)
{
#ifndef O_LARGEFILE64
#define O_LARGEFILE64	0100000		/* hack, from fcntl.h, FIXME */
#endif
  return __libc_open(pathname, flags | O_LARGEFILE64, mode);
}
strong_alias (__open64, open64)

/* Pth hard wrapper for syscall pause(3) */
#if cpp
#if defined(SYS_pause)
#define PTH_SC_pause() ((void)syscall(SYS_pause))
#else
#define PTH_SC_pause __pause
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __pause(void);
int __pause(void)
{
    return __libc_pause();
}
strong_alias (__pause, pause)
#endif

/* Pth hard wrapper for syscall system(3) */
#if cpp
#if defined(SYS_system)
#define PTH_SC_system() ((void)syscall(SYS_system))
#else
#define PTH_SC_system __system
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __system(const char *line);
int __system(const char *line)
{
    return __libc_system(line);
}
strong_alias (__system, system)
#endif

/* Pth hard wrapper for syscall tcdrain(3) */
#if cpp
#if defined(SYS_tcdrain)
#define PTH_SC_tcdrain() ((void)syscall(SYS_tcdrain))
#else
#define PTH_SC_tcdrain __tcdrain
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __tcdrain(int fd);
int __tcdrain(int fd)
{
    return __libc_tcdrain(fd);
}
strong_alias (__tcdrain, tcdrain)
#endif

/* Pth hard wrapper for syscall wait(3) */
#if cpp
#if defined(SYS_wait)
#define PTH_SC_wait() ((void)syscall(SYS_wait))
#else
#define PTH_SC_wait __wait
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __wait(__WAIT_STATUS_DEFN stat_loc);
int __wait(__WAIT_STATUS_DEFN stat_loc)
{
    return __libc_wait(stat_loc);
}
strong_alias (__wait, wait)
#endif

/* Pth hard wrapper for syscall sigaction(3) */
#if cpp
#if defined(SYS_sigaction)
#define PTH_SC_sigaction(a1,a2,a3) ((int)syscall(SYS_sigaction,(a1),(a2),(a3)))
#else
#define PTH_SC_sigaction __sigaction
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD /* && defined(SYS_sigaction) */
extern int __sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int __sigaction(int sig, const struct sigaction *act, struct sigaction *oldact)
{
    pth_t current = pth_get_current();

    /* save the signal */
    if (act != NULL && current != NULL) {
	if (act->sa_handler != SIG_IGN && act->sa_handler != SIG_DFL && sig > 0 && sig < NSIG)
	    sigaddset(&current->sigactionmask, sig);
    }
    return __libc_sigaction(sig, act, oldact);
}
#endif

/* Pth hard wrapper for syscall siglongjmp(3) */
#if cpp
#if defined(SYS_siglongjmp)
#define PTH_SC_siglongjmp() ((void)syscall(SYS_siglongjmp))
#else
#define PTH_SC_siglongjmp __siglongjmp
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD /* && defined(SYS_siglongjmp) */
extern void __siglongjmp(sigjmp_buf env, int val);
void __siglongjmp(sigjmp_buf env, int val)
{
    __libc_siglongjmp(env, val);
}
#endif

/* Pth hard wrapper for syscall longjmp(3) */
#if cpp
#if defined(SYS_longjmp)
#define PTH_SC_longjmp() ((void)syscall(SYS_longjmp))
#else
#define PTH_SC_longjmp __longjmp
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD /* && defined(SYS_longjmp) */
extern void __longjmp(sigjmp_buf env, int val);
void __longjmp(sigjmp_buf env, int val)
{
    __libc_longjmp(env, val);
}
#endif

/* Pth hard wrapper for syscall fork(2) */
#if cpp
#if defined(SYS_fork)
#define PTH_SC_fork() ((pid_t)syscall(SYS_fork))
#else
#define PTH_SC_fork __fork
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_fork)
extern pid_t __fork(void);
pid_t __fork(void)
{
    return pth_fork();
}
#endif

/* Pth hard wrapper for syscall _exit(2) */
#if cpp
#if defined(SYS_exit)
#define PTH_SC_exit(a1) ((void)syscall(SYS_exit,(a1)))
#else
#define PTH_SC_exit __exit
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_exit)
extern void __exit(int);
void __exit(int status)
{
    pth_kill();	/* need to kill native threads */
    syscall(SYS_exit,status);
}
#endif

/* Pth hard wrapper for sleep(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
extern unsigned int __sleep(unsigned int sec);
unsigned int __sleep(unsigned int sec)
{
    return pth_sleep(sec);
}
#endif

/* Pth hard wrapper for sigprocmask(2) */
#if cpp
#if defined(SYS_sigprocmask)
#define PTH_SC_sigprocmask(a1,a2,a3) ((int)syscall(SYS_sigprocmask,(a1),(a2),(a3)))
#else
#define PTH_SC_sigprocmask __sigprocmask
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD
extern int __sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int __sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
    pth_debug2("sigprocmask: called with how = %d", how);
    return pth_sigmask(how, set, oset);
}
#endif

/* Pth hard wrapper for sigwait(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
extern int __sigwait(const sigset_t *set, int *sigp);
int __sigwait(const sigset_t *set, int *sigp)
{
    return pth_sigwait(set, sigp);
}
#endif

/* Pth hard wrapper for sigwaitinfo(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
extern int __sigwaitinfo(const sigset_t *set, siginfo_t *sigp);
int __sigwaitinfo(const sigset_t *set, siginfo_t *sigp)
{
    int sig = 0;
    int err = 0;

    err = pth_sigwait(set, &sig);

    if (err == 0 && sigp != NULL)
	sigp->si_signo = sig;

    return err;
}
#endif

/* Pth hard wrapper for syscall waitpid(2) */
#if cpp
#if defined(SYS_waitpid)
#define PTH_SC_waitpid(a1,a2,a3) ((int)syscall(SYS_waitpid,(a1),(a2),(a3)))
#else
#define PTH_SC_waitpid __waitpid
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_waitpid)
extern pid_t __waitpid(pid_t wpid, int *status, int options);
pid_t __waitpid(pid_t wpid, int *status, int options)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(waitpid)(wpid, status, options);
    return pth_waitpid(wpid, status, options);
}
#endif

#if defined(SYS_socketcall) && defined(SOCKOP_connect) && defined(SOCKOP_accept) /* mainly Linux */
intern int _pth_socketcall(int call, ...)
{
    va_list ap;
    unsigned long args[6];

    va_start(ap, call);
    switch (call) {
        case SOCKOP_connect:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, struct sockaddr *);
            args[2] = (unsigned long)va_arg(ap, int);
            break;
        case SOCKOP_accept:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, struct sockaddr *);
            args[2] = (unsigned long)va_arg(ap, int *);
            break;
        case SOCKOP_send:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, void *);
            args[2] = (unsigned long)va_arg(ap, int);
            args[3] = (unsigned long)va_arg(ap, int);
            break;
        case SOCKOP_recv:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, void *);
            args[2] = (unsigned long)va_arg(ap, int);
            args[3] = (unsigned long)va_arg(ap, int);
            break;
        case SOCKOP_sendto:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, void *);
            args[2] = (unsigned long)va_arg(ap, int);
            args[3] = (unsigned long)va_arg(ap, int);
            args[4] = (unsigned long)va_arg(ap, struct sockaddr *);
            args[5] = (unsigned long)va_arg(ap, int);
            break;
        case SOCKOP_recvfrom:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, void *);
            args[2] = (unsigned long)va_arg(ap, int);
            args[3] = (unsigned long)va_arg(ap, int);
            args[4] = (unsigned long)va_arg(ap, struct sockaddr *);
            args[5] = (unsigned long)va_arg(ap, int *);
            break;
        case SOCKOP_sendmsg:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, struct msghdr *);
            args[2] = (unsigned long)va_arg(ap, int);
            break;
        case SOCKOP_recvmsg:
            args[0] = (unsigned long)va_arg(ap, int);
            args[1] = (unsigned long)va_arg(ap, struct msghdr *);
            args[2] = (unsigned long)va_arg(ap, int);
            break;
    }
    va_end(ap);
    return syscall(SYS_socketcall, call, args);
}
#endif

/* Pth hard wrapper for syscall connect(2) */
#if cpp
#if defined(SYS_connect)
#define PTH_SC_connect(a1,a2,a3) ((int)syscall(SYS_connect,(a1),(a2),(a3)))
#elif defined(SYS_socketcall) && defined(SOCKOP_connect) /* mainly Linux */
#define PTH_SC_connect(a1,a2,a3) ((int)_pth_socketcall(SOCKOP_connect,(a1),(a2),(a3)))
#else
#define PTH_SC_connect connect
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_connect) || (defined(SYS_socketcall) && defined(SOCKOP_connect)))
extern int __connect(int s, const struct sockaddr *addr, socklen_t addrlen);
int __connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(connect)(s, addr, addrlen);
    return pth_connect_ev(s, addr, addrlen, NULL);
}
#endif

/* Pth hard wrapper for syscall accept(2) */
#if cpp
#if defined(SYS_accept)
#define PTH_SC_accept(a1,a2,a3) ((int)syscall(SYS_accept,(a1),(a2),(a3)))
#elif defined(SYS_socketcall) && defined(SOCKOP_accept) /* mainly Linux */
#define PTH_SC_accept(a1,a2,a3) ((int)_pth_socketcall(SOCKOP_accept,(a1),(a2),(a3)))
#else
#define PTH_SC_accept accept
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_accept) || (defined(SYS_socketcall) && defined(SOCKOP_accept)))
extern int __accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int __accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(accept)(s, addr, addrlen);
    return pth_accept_ev(s, addr, addrlen, NULL);
}
#endif

/* Pth hard wrapper for syscall select(2) */
#if cpp
#if defined(SYS__newselect) /* mainly Linux */
#define PTH_SC_select(a1,a2,a3,a4,a5) ((int)syscall(SYS__newselect,(a1),(a2),(a3),(a4),(a5)))
#elif defined(SYS_select)
#define PTH_SC_select(a1,a2,a3,a4,a5) ((int)syscall(SYS_select,(a1),(a2),(a3),(a4),(a5)))
#else
#define PTH_SC_select __select
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && (defined(SYS__newselect) || defined(SYS_select))
extern int __select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
int __select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr && (descr->is_bounded || pth_active_threads == 1))
        return pth_sc(select)(nfds, readfds, writefds, exceptfds, timeout);
    return pth_select_ev(nfds, readfds, writefds, exceptfds, timeout, NULL);
}
#endif

/* Pth hard wrapper for syscall poll(2) */
#if cpp
#if defined(SYS_poll)
#define PTH_SC_poll(a1,a2,a3) ((int)syscall(SYS_poll,(a1),(a2),(a3)))
#else
#define PTH_SC_poll __poll_2_1
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_poll)
extern int __poll_2_1(struct pollfd *pfd, nfds_t nfd, int timeout);
int __poll_2_1(struct pollfd *pfd, nfds_t nfd, int timeout)
{
    pth_descr_t descr = pth_get_native_descr();

    if (descr && descr->is_bounded)
	return pth_sc(poll)(pfd, nfd, timeout);
    return pth_poll_ev(pfd, nfd, timeout, NULL);
}
#endif

/* Pth hard wrapper for syscall poll(2) */
#if cpp
#if defined(SYS_poll)
#define PTH_SC_poll(a1,a2,a3) ((int)syscall(SYS_poll,(a1),(a2),(a3)))
#else
#define PTH_SC_poll poll_2_0
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_poll)
extern int poll_2_0(struct pollfd *pfd, nfds_t nfd, int timeout);
int poll_2_0(struct pollfd *pfd, nfds_t nfd, int timeout)
{
    pth_descr_t descr = pth_get_native_descr();

    if (descr && descr->is_bounded)
	return pth_sc(poll)(pfd, nfd, timeout);
    return pth_poll_ev(pfd, nfd, timeout, NULL);
}
#endif

/* Pth hard wrapper for syscall read(2) */
#if cpp
#if defined(SYS_read)
#define PTH_SC_read(a1,a2,a3) ((ssize_t)syscall(SYS_read,(a1),(a2),(a3)))
#else
#define PTH_SC_read __read
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_read)
extern ssize_t __read(int fd, void *buf, size_t nbytes);
ssize_t __read(int fd, void *buf, size_t nbytes)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr && (descr->is_bounded || pth_active_threads == 1))
	return pth_sc(read)(fd, buf, nbytes);
    return pth_read_ev(fd, buf, nbytes, NULL);
}
#endif

/* Pth hard wrapper for syscall write(2) */
#if cpp
#if defined(SYS_write)
#define PTH_SC_write(a1,a2,a3) ((ssize_t)syscall(SYS_write,(a1),(a2),(a3)))
#else
#define PTH_SC_write __write
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_write)
extern ssize_t __write(int fd, const void *buf, size_t nbytes);
ssize_t __write(int fd, const void *buf, size_t nbytes)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr && (descr->is_bounded || pth_active_threads == 1))
	return pth_sc(write)(fd, buf, nbytes);
    return pth_write_ev(fd, buf, nbytes, NULL);
}
#endif

/* Pth hard wrapper for syscall readv(2) */
#if cpp
#if defined(SYS_readv)
#define PTH_SC_readv(a1,a2,a3) ((ssize_t)syscall(SYS_readv,(a1),(a2),(a3)))
#else
#define PTH_SC_readv __readv
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_readv)
extern ssize_t __readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t __readv(int fd, const struct iovec *iov, int iovcnt)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr && (descr->is_bounded || pth_active_threads == 1))
	return pth_sc(readv)(fd, iov, iovcnt);
    return pth_readv_ev(fd, iov, iovcnt, NULL);
}
#endif

/* Pth hard wrapper for syscall writev(2) */
#if cpp
#if defined(SYS_writev)
#define PTH_SC_writev(a1,a2,a3) ((ssize_t)syscall(SYS_writev,(a1),(a2),(a3)))
#else
#define PTH_SC_writev __writev
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && defined(SYS_writev)
extern ssize_t __writev(int fd, const struct iovec *iov, int iovcnt);
ssize_t __writev(int fd, const struct iovec *iov, int iovcnt)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr && (descr->is_bounded || pth_active_threads == 1))
	return pth_sc(writev)(fd, iov, iovcnt);
    return pth_writev_ev(fd, iov, iovcnt, NULL);
}
#endif

/* Pth hard wrapper for pread(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
ssize_t __pread(int, void *, size_t, off_t);
ssize_t __pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    return pth_pread(fd, buf, nbytes, offset);
}
#endif

/* Pth hard wrapper for pread64(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
ssize_t __pread64(int, void *, size_t, __off64_t);
ssize_t __pread64(int fd, void *buf, size_t nbytes, __off64_t offset)
{
    return pth_pread64(fd, buf, nbytes, offset);
}
#endif

/* Pth hard wrapper for pwrite(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
ssize_t __pwrite(int, const void *, size_t, off_t);
ssize_t __pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    return pth_pwrite(fd, buf, nbytes, offset);
}
#endif

/* Pth hard wrapper for pwrite64(3) [internally fully emulated] */
#if PTH_SYSCALL_HARD
ssize_t __pwrite64(int, const void *, size_t, __off64_t);
ssize_t __pwrite64(int fd, const void *buf, size_t nbytes, __off64_t offset)
{
    return pth_pwrite64(fd, buf, nbytes, offset);
}
#endif

/* Pth hard wrapper for syscall recv(2) */
#if cpp
#if defined(SYS_recv)
#define PTH_SC_recv(a1,a2,a3,a4) ((ssize_t)syscall(SYS_recvfrom,(a1),(a2),(a3),(a4)))
#elif defined(SYS_socketcall) && defined(SOCKOP_recv) /* mainly Linux */
#define PTH_SC_recv(a1,a2,a3,a4) ((int)_pth_socketcall(SOCKOP_recv,(a1),(a2),(a3),(a4)))
#else
#define PTH_SC_recv __recv
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_recv) || (defined(SYS_socketcall) && defined(SOCKOP_recv)))
extern ssize_t __recv(int fd, void *buf, size_t nbytes, int flags);
ssize_t __recv(int fd, void *buf, size_t nbytes, int flags)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(recv)(fd, buf, nbytes, flags);
    return pth_recv(fd, buf, nbytes, flags);
}
#endif

/* Pth hard wrapper for syscall recvmsg(2) */
#if cpp
#if defined(SYS_recvmsg)
#define PTH_SC_recvmsg(a1,a2,a3) ((ssize_t)syscall(SYS_recvmsg,(a1),(a2),(a3)))
#elif defined(SYS_socketcall) && defined(SOCKOP_recvmsg) /* mainly Linux */
#define PTH_SC_recvmsg(a1,a2,a3) ((int)_pth_socketcall(SOCKOP_recvmsg,(a1),(a2),(a3)))
#else
#define PTH_SC_recvmsg recvmsg
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_recvmsg) || (defined(SYS_socketcall) && defined(SOCKOP_recvmsg)))
extern ssize_t __recvmsg(int fd, struct msghdr *msg, int flags);
ssize_t __recvmsg(int fd, struct msghdr *msg, int flags)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(recvmsg)(fd, msg, flags);
    return pth_recvmsg(fd, msg, flags);
}
#endif

/* Pth hard wrapper for syscall recvfrom(2) */
#if cpp
#if defined(SYS_recvfrom)
#define PTH_SC_recvfrom(a1,a2,a3,a4,a5,a6) ((ssize_t)syscall(SYS_recvfrom,(a1),(a2),(a3),(a4),(a5),(a6)))
#elif defined(SYS_socketcall) && defined(SOCKOP_recvfrom) /* mainly Linux */
#define PTH_SC_recvfrom(a1,a2,a3,a4,a5,a6) ((int)_pth_socketcall(SOCKOP_recvfrom,(a1),(a2),(a3),(a4),(a5),(a6)))
#else
#define PTH_SC_recvfrom recvfrom
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_recvfrom) || (defined(SYS_socketcall) && defined(SOCKOP_recvfrom)))
extern ssize_t __recvfrom(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t __recvfrom(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(recvfrom)(fd, buf, nbytes, flags, from, fromlen);
    return pth_recvfrom(fd, buf, nbytes, flags, from, fromlen);
}
#endif

/* Pth hard wrapper for syscall send(2) */
#if cpp
#if defined(SYS_send)
#define PTH_SC_send(a1,a2,a3,a4) ((ssize_t)syscall(SYS_send,(a1),(a2),(a3),(a4)))
#elif defined(SYS_socketcall) && defined(SOCKOP_send) /* mainly Linux */
#define PTH_SC_send(a1,a2,a3,a4) ((int)_pth_socketcall(SOCKOP_send,(a1),(a2),(a3),(a4)))
#else
#define PTH_SC_send __send
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_send) || (defined(SYS_socketcall) && defined(SOCKOP_send)))
extern ssize_t __send(int fd, const void *buf, size_t nbytes, int flags);
ssize_t __send(int fd, const void *buf, size_t nbytes, int flags)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(send)(fd, buf, nbytes, flags);
    return pth_send(fd, buf, nbytes, flags);
}
#endif

/* Pth hard wrapper for syscall sendto(2) */
#if cpp
#if defined(SYS_sendto)
#define PTH_SC_sendto(a1,a2,a3,a4,a5,a6) ((ssize_t)syscall(SYS_sendto,(a1),(a2),(a3),(a4),(a5),(a6)))
#elif defined(SYS_socketcall) && defined(SOCKOP_sendto) /* mainly Linux */
#define PTH_SC_sendto(a1,a2,a3,a4,a5,a6) ((int)_pth_socketcall(SOCKOP_sendto,(a1),(a2),(a3),(a4),(a5),(a6)))
#else
#define PTH_SC_sendto sendto
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD &&\
    (defined(SYS_send) || (defined(SYS_socketcall) && defined(SOCKOP_send)))
extern ssize_t __sendto(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen);
ssize_t __sendto(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(sendto)(fd, buf, nbytes, flags, to, tolen);
    return pth_sendto(fd, buf, nbytes, flags, to, tolen);
}
#endif

/* Pth hard wrapper for syscall sendmsg(2) */
#if cpp
#if defined(SYS_sendmsg)
#define PTH_SC_sendmsg(a1,a2,a3) ((ssize_t)syscall(SYS_sendmsg,(a1),(a2),(a3)))
#elif defined(SYS_socketcall) && defined(SOCKOP_sendmsg) /* mainly Linux */
#define PTH_SC_sendmsg(a1,a2,a3) ((int)_pth_socketcall(SOCKOP_sendmsg,(a1),(a2),(a3)))
#else
#define PTH_SC_sendmsg sendmsg
#endif
#endif /* cpp */
#if PTH_SYSCALL_HARD && \
    (defined(SYS_sendmsg) || (defined(SYS_socketcall) && defined(SOCKOP_sendmsg)))
extern ssize_t __sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t __sendmsg(int fd, const struct msghdr *msg, int flags)
{
    pth_descr_t descr = pth_get_native_descr();
    if (descr->current->boundnative)
	return pth_sc(sendmsg)(fd, msg, flags);
    return pth_sendmsg(fd, msg, flags);
}
#endif

/* GLIBC Compatibility */
strong_alias (__exit, _exit)
strong_alias (__fork, fork)
strong_alias (__fork, vfork)
strong_alias (__fork, __vfork)
strong_alias (__sleep, sleep)
strong_alias (__sigwait, sigwait)
strong_alias (__waitpid, waitpid)
strong_alias (__connect, connect)
strong_alias (__accept, accept)
strong_alias (__select, select)
strong_alias (__read, read)
strong_alias (__write, write)
strong_alias (__readv, readv)
strong_alias (__writev, writev)
strong_alias (__pread, pread)
strong_alias (__pwrite, pwrite)
strong_alias (__pread64, pread64)
strong_alias (__pwrite64, pwrite64)
strong_alias (__recv, recv)
strong_alias (__recvmsg, recvmsg)
strong_alias (__send, send)
strong_alias (__sendto, sendto)
strong_alias (__sendmsg, sendmsg)
strong_alias (__recvfrom, recvfrom)
strong_alias (__sigaction, sigaction)
strong_alias (__siglongjmp, siglongjmp)
strong_alias (__longjmp, longjmp)
strong_alias (__sigprocmask, sigprocmask)
strong_alias (__sigwaitinfo, sigwaitinfo)
versioned_symbol(__poll_2_1, __poll, GLIBC_2.1)
versioned_symbol(poll_2_0, poll, GLIBC_2.0)
