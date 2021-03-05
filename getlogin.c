/*
**  NGPT - Next Generation POSIX Threading
**  Copyright (C) 1991,93,95,96,97,99,2000,2001 Free Software Foundation, Inc.
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
**  getlogin.c: getlogin implementation.
*/
			     /* ``Unix -- where you can do anything
			        in two keystrokes, or less...'' 
			        -- Unknown  */

#define _PTHREAD_PRIVATE
#include "pthread.h"
#include "pth_p.h"
#undef  _PTHREAD_PRIVATE
#include <pwd.h>
#include <sys/types.h>

extern char * getlogin(void);
char * getlogin(void)
{
    struct passwd *pwd;
    uid_t uid = geteuid();

    pwd = getpwuid(uid);
    if (pwd == NULL) {
	if (errno == ENOMEM)
	    return_errno(NULL,ENOMEM);
	else
	    return_errno(NULL,ENOENT);
    }

    return pwd->pw_name;
}
strong_alias(getlogin, __getlogin);

extern int getlogin_r(char *name, size_t name_len);
int getlogin_r(char *name, size_t name_len)
{
    struct passwd *pwd;
    uid_t uid = geteuid();
    size_t needed = 0;

    pwd = getpwuid(uid);
    if (pwd == NULL) {
	if (errno == ENOMEM)
	    return_errno(ENOMEM,ENOMEM);
	else
	    return_errno(ENOENT,ENOENT);
    }

    needed = strlen(pwd->pw_name) +1;

    if (needed > name_len)
	return_errno(ERANGE,ERANGE);

    memcpy(name, pwd->pw_name, needed);

    return 0;
}
strong_alias(getlogin_r, __getlogin_r);
