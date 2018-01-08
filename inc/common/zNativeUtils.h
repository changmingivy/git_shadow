#ifndef ZLOCALUTILS_H
#define ZLOCALUTILS_H

#include <stdio.h>
#include "zRun.h"
#include "zCommon.h"

struct zNativeUtils__ {
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);

    _i (* path_del) (char *);
    _i (* path_cp) (char *, char *);
};

#endif  // #ifndef ZLOCALUTILS_H
