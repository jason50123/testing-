/* --- uid_debug.hh ------------------------------------------------------ */
#pragma once
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <pwd.h>

inline void dump_uids(const char *tag)
{
    uid_t r,e,s;  gid_t gr,ge,gs;
    getresuid(&r,&e,&s);
    getresgid(&gr,&ge,&gs);

    fprintf(stderr,
        "[%s] UID  real=%u  eff=%u  saved=%u | "
        "GID  real=%u  eff=%u  saved=%u\n",
        tag, (unsigned)r,(unsigned)e,(unsigned)s,
        (unsigned)gr,(unsigned)ge,(unsigned)gs);
    
}
