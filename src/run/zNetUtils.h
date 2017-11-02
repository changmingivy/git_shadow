#include <netdb.h>

#include "zCommon.h"

struct zNetUtils__ {
    _i (* gen_serv_sd) (char *, char *, _i);

    _i (* tcp_conn) (char *, char *, _i);

    _i (* sendto) (_i, void *, size_t, _i, struct sockaddr *);
    _i (* sendmsg) (_i, struct iovec *, size_t, _i, struct sockaddr *);
    _i (* recv_all) (_i, void *, size_t, _i, struct sockaddr *);

    _ui (* to_bin)(const char *zpStrAddr);
    void (* to_str)(_ui zIpBinAddr, char *zpBufOUT);
};


#ifndef _SELF_
extern struct zNetUtils__ zNetUtils_;
#endif
