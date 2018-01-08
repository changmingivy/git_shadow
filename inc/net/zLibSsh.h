#ifndef ZLIBSSH_H
#define ZLIBSSH_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include "zRun.h"
#include "zCommon.h"
#include "zNetUtils.h"
#include <semaphore.h>

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, znet_auth_t, char *, _ui, sem_t *, char *);
};

#endif  // #ifndef ZLIBSSH_H
