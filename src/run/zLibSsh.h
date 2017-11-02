#define _XOPEN_SOURCE 700

#include "zNetUtils.h"

struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, _i, char *, _ui, pthread_mutex_t *);
};

#ifndef _SELF_
extern struct zLibSsh__ zLibSsh_;
#endif
