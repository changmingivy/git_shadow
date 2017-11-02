#define ZLOCALUTILS_H

#include <stdio.h>

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

struct zLocalUtils__ {
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);
};

extern struct zLocalUtils__ zLocalUtils_;
