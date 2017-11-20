#ifndef ZLOCALUTILS_H
#define ZLOCALUTILS_H

#include <stdio.h>
#include "zCommon.h"

struct zNativeUtils__ {
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);

    void (* json_parse) (char *, char *, zJsonValueType__, void *, _i);
};

#endif  // #ifndef ZLOCALUTILS_H
