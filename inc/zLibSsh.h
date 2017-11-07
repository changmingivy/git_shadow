#ifndef ZLIBSSH_H
#define ZLIBSSH_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include "zCommon.h"
#include "zNetUtils.h"

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, _i, char *, _ui, pthread_mutex_t *);
};


// extern struct zLibSsh__ zLibSsh_;

#endif  // #ifndef ZLIBSSH_H
