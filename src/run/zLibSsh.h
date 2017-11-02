#define ZLIBSSH_H
#define _XOPEN_SOURCE 700

#ifndef ZNETUTILS_H
#include "zNetUtils.h"
#endif

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, _i, char *, _ui, pthread_mutex_t *);
};


extern struct zLibSsh__ zLibSsh_;
