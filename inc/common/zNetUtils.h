#ifndef ZNETUTILS_H
#define ZNETUTILS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include <netdb.h>
#include "zCommon.h"

struct zNetUtils__ {
    _i (* gen_serv_sd) (char *, char *, znet_proto_t);

    _i (* conn) (char *, char *, znet_proto_t, _i);

    _i (* sendto) (_i, void *, size_t, _i, struct sockaddr *, zip_t);
    _i (* send_nosignal) (_i, void *, size_t);
    _i (* sendmsg) (_i, struct iovec *, size_t, _i, struct sockaddr *, zip_t);
    _i (* recv_all) (_i, void *, size_t, _i, struct sockaddr *);

    _i (* to_numaddr) (const char *, zip_t, _ull *);
    _i (* to_straddr) (_ull *, zip_t, char *);
};

#endif  // #ifndef ZNETUTILS_H
