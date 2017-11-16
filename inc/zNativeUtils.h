#define ZLOCALUTILS_H

#include <stdio.h>

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

struct zNativeUtils__ {
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);
};

// extern struct zNativeUtils__ zNativeUtils_;
