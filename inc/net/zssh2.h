#ifndef ZLIBSSH_H
#define ZLIBSSH_H

#include "zCommon.h"
#include <semaphore.h>

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, znet_auth_t, char *, _ui, sem_t *, char *);
};

#endif  // #ifndef ZLIBSSH_H
