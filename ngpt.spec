##
##  NGPT - Next Generation POSIX Threading
##  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
##  Portions Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
##
##  This file is part of NGPT a non-preemptive thread scheduling
##  library which can be found at http://www.ibm.com/developer
##
##  This library is free software; you can redistribute it and/or
##  modify it under the terms of the GNU Lesser General Public
##  License as published by the Free Software Foundation; either
##  version 2.1 of the License, or (at your option) any later version.
##
##  This library is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
##  Lesser General Public License for more details.
##
##  You should have received a copy of the GNU Lesser General Public
##  License along with this library; if not, write to the Free Software
##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
##  USA.
##
##  ngpt.spec: RPM specification 
##

#   This is a specification file for the RedHat Package Manager (RPM).
#   It is part of the ngpt source tree and this way directly included in
#   ngpt distribution tarballs. This way one can use a simple `rpm -tb
#   ngpt-X.Y.Z.tar.gz' command to build binary RPM packages from a ngpt
#   distribution tarball.

%define prefix /usr
%define ver 2.2.1
%define rel 1

Summary:    NGPT - Next Generation POSIX Threading
Name:       ngpt
Version:    %{ver}
Release:    %{rel}
Group:      System Environment/Libraries
Vendor:	    IBM
Copyright:  LGPL
URL:        http://oss.software.ibm.com/developerworks/opensource/pthreads
Packager:   IBM Corporation <http://oss.software.ibm.com/developerworks/opensource>
Source:     http://oss.software.ibm.com/developerworks/opensource/pthreads/download/stable/source/ngpt-%{ver}.tar.gz
BuildRoot:  /tmp/ngpt-%{ver}-root
Obsoletes:  linuxthreads
AutoReqProv: no
Requires:   glibc > 2.2.4

%description
NGPT is the user-level portion of a POSIX pthreads library which provides
non-preemptive priority-based scheduling for multiple threads of
execution (aka ``multithreading'') inside event-driven applications. All
threads run in the same address space of the server application, but each
thread has it's own individual program-counter, run-time stack, signal
mask and errno variable.

On SMP machines, this library will use an M:N threading model if enabled
resulting in significantly improved performance.

%package devel
Summary:    NGPT Development Package
Group:	    Development/Libraries
AutoReqProv: no
Requires:   ngpt = %{ver}
Requires:   glibc > 2.2.4
Obsoletes:  linuxthreads-devel

%description devel
Headers, static libraries, and documentation for NGPT.

%prep

%setup

%build
%ifarch i386
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{prefix} --enable-batch --enable-optimize --enable-hp_timing --enable-pthread --enable-syscall-hard --enable-maintainer --with-mctx-mth=mcsc --with-mctx-dsp=sc --with-mctx-stk=mc --enable-thread_db
%endif
%ifnarch i386
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=%{prefix} --enable-batch --enable-optimize --enable-pthread --enable-syscall-hard --enable-maintainer --with-mctx-mth=mcsc --with-mctx-dsp=sc --with-mctx-stk=mc --enable-thread_db
%endif
make
make test

%install
rm -rf $RPM_BUILD_ROOT
make rpm-install prefix=$RPM_BUILD_ROOT%{prefix}

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig
%{prefix}/bin/ngptinit
##
## Leave the following commented out until we achieve better binary compatibility
## with glibc/LinuxThreads.
## (cd /lib && cp -f %{prefix}/lib/libpthread.so libpthread-0.9.so && ln -f -s libpthread-0.9.so libpthread.so.0)
## (cd /lib && cp -f %{prefix}/lib/libthread_db.so libthread_db.so.1)
## /sbin/ldconfig
##
## At this point there will also need to be rc.d entry for creating the shared segment.
##

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
%doc ANNOUNCE AUTHORS COPYING ChangeLog HACKING HISTORY INSTALL NEWS PORTING README SUPPORT TESTS THANKS TODO USERS
%{prefix}/lib/libpthread.so
%{prefix}/lib/libpthread.so.*
%{prefix}/lib/libthread_db.so.*
%{prefix}/bin/ngptc
%{prefix}/bin/ngptinit

%files devel
%defattr(-,root,root)
%{prefix}/bin/pthread-config
%{prefix}/include/pthread.h
%{prefix}/include/semaphore.h
%{prefix}/include/bits/pthreadtypes.h
%{prefix}/include/bits/sigthread.h
%{prefix}/include/bits/local_lim.h
%{prefix}/include/bits/posix_opt.h
%{prefix}/lib/libpthread.a
%{prefix}/lib/libpthread.la
%{prefix}/man/man1/pthread-config.1.gz
%{prefix}/man/man3/pthread.3.gz

