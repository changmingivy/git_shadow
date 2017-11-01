#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <sys/select.h>

#include "libssh2.h"
#include "zCommon.h"
#include "zNetUtils.h"


#define zSshSelfIpDeclareBufSiz zSizeOf("export ____zSelfIp='192.168.100.100';")


struct zLibSsh__ {
    _i (* exec) (char *, char *, char *, const char *, const char *, const char *, const char *, _i, char *, _ui, pthread_mutex_t *);
};

extern struct zLibSsh__ zLibSsh_;
