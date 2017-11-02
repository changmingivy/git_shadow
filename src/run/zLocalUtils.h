#include <stdio.h>

#include "zCommon.h"

struct zNativeUtils__ {
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);
};

#ifndef _SELF_
extern struct zNativeUtils__ zNativeUtils_;
#endif
