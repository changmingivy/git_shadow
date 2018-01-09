#ifndef ZDPOPS_H
#define ZDPOPS_H

#include "zCommon.h"
#include "cJSON.h"

struct zDpOps__ {
    _i (* show_dp_process) (cJSON *, _i);

    _i (* print_revs) (cJSON *, _i);
    _i (* print_diff_files) (cJSON *, _i);
    _i (* print_diff_contents) (cJSON *, _i);

    _i (* creat) (cJSON *, _i);

    _i (* dp) (cJSON *, _i);

    _i (* glob_res_confirm) (cJSON *, _i);
    _i (* state_confirm) (cJSON *, _i);
    _i (* state_confirm_inner) (void *, _i, struct sockaddr *, socklen_t);

    _i (* req_file) (cJSON *, _i);

    _i (* tcp_pang) (cJSON *, _i);
    _i (* udp_pang) (void *, _i, struct sockaddr *, socklen_t);

    _i (* repo_update) (cJSON *, _i);
};


#define zIPVEC_CMP(zVec0, zVec1) ((zVec0)[0] == (zVec1)[0] && (zVec0)[1] == (zVec1)[1])

#define /*_i*/ zCONVERT_IPSTR_TO_NUM(/*|_ull [2]|*/ zpIpStr, /*|char *|*/ zpNumVec) ({\
    _i ErrNo = 0;\
    if ('.' == zpIpStr[1] || '.' == zpIpStr[2] || '.' == zpIpStr[3]) {\
        ErrNo = zNetUtils_.to_numaddr(zpIpStr, zIPTypeV4, zpNumVec);\
    } else {\
        ErrNo = zNetUtils_.to_numaddr(zpIpStr, zIPTypeV6, zpNumVec);\
    };\
    ErrNo;  /* 宏返回值 */\
})

#define /*_i*/ zCONVERT_IPNUM_TO_STR(/*|_ull [2]|*/ zpNumVec, /*|char *|*/ zpIpStr) ({\
    _i ErrNo = 0;\
    if (0xff == zpNumVec[1] /* IPv4 */) {\
        ErrNo = zNetUtils_.to_straddr(zpNumVec, zIPTypeV4, zpIpStr);\
    } else {\
        ErrNo = zNetUtils_.to_straddr(zpNumVec, zIPTypeV6, zpIpStr);\
    } \
    ErrNo;  /* 宏返回值 */\
})


#endif  //  #ifndef ZDPOPS_H
