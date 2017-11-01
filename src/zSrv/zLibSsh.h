#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <sys/select.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "libssh2.h"
#include "zCommon.h"
#include "zNetUtils.h"

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, _i, char *, _ui, pthread_mutex_t *);
};

extern struct zLibSsh__ zLibSsh_;
