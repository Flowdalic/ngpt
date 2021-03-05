/*
**  pth_vers.c -- Version Information for Next Generation POSIX Threading (syntax: C/C++)
**  [automatically generated and maintained by GNU shtool]
*/

#ifdef _PTH_VERS_C_AS_HEADER_

#ifndef _PTH_VERS_C_
#define _PTH_VERS_C_

#define PTH_INTERNAL_VERSION 0x202201

typedef struct {
    const int   v_hex;
    const char *v_short;
    const char *v_long;
    const char *v_tex;
    const char *v_gnu;
    const char *v_web;
    const char *v_sccs;
    const char *v_rcs;
} pth_internal_version_t;

extern pth_internal_version_t pth_internal_version;

#endif /* _PTH_VERS_C_ */

#else /* _PTH_VERS_C_AS_HEADER_ */

#define _PTH_VERS_C_AS_HEADER_
#include "pth_vers.c"
#undef  _PTH_VERS_C_AS_HEADER_

pth_internal_version_t pth_internal_version = {
    0x202201,
    "2.2.1",
    "2.2.1 (20-Mar-2003)",
    "This is Next Generation POSIX Threading, Version 2.2.1 (20-Mar-2003)",
    "Next Generation POSIX Threading 2.2.1 (20-Mar-2003)",
    "Next Generation POSIX Threading/2.2.1",
    "@(#)Next Generation POSIX Threading 2.2.1 (20-Mar-2003)",
    "$Id: pth_vers.c,v 1.130 2003/03/20 23:09:00 sdesai Exp $"
};

#endif /* _PTH_VERS_C_AS_HEADER_ */

