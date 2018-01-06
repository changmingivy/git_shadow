#ifndef ZDPOPS_H
#define ZDPOPS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
    #endif

    #ifndef _DEFAULT_SOURCE
    #define _DEFAULT_SOURCE
    #endif

    #ifndef _BSD_SOURCE
    #define _BSD_SOURCE
    #endif
#endif

#include <pthread.h>  //  该头文件内部已使用 #define _PTHREAD_H 避免重复
#include <semaphore.h>  //  该头文件内部已使用 #define _SEMAPHORE_H 避免重复
#include <libpq-fe.h>  //  该头文件内部已使用 #define LIBPQ_FE_H 避免重复

#include "zCommon.h"
#include "zNativeUtils.h"
#include "zNetUtils.h"

#include "zLibSsh.h"
#include "zLibGit.h"

#include "zNativeOps.h"
#include "zRun.h"

#include "zPosixReg.h"
#include "zThreadPool.h"
#include "zPgSQL.h"
//#include "zMd5Sum.h"
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
    _i (* state_confirm_inner) (void *, struct sockaddr *, socklen_t);

    _i (* req_file) (cJSON *, _i);

    _i (* tcp_pang) (cJSON *, _i);
    _i (* udp_pang) (void *, _i, struct sockaddr *, socklen_t);

    _i (* repo_update) (cJSON *, _i);
};


#define zIPVEC_CMP(zVec0, zVec1) ((zVec0)[0] == (zVec1)[0] && (zVec0)[1] == (zVec1)[1])

#define /*_i*/ zCONVERT_IPSTR_TO_NUM(/*|_ull [2]|*/ zpIpStr, /*|char *|*/ zpNumVec) ({\
    _i zErrNo = 0;\
    if ('.' == zpIpStr[1] || '.' == zpIpStr[2] || '.' == zpIpStr[3]) {\
        zErrNo = zNetUtils_.to_numaddr(zpIpStr, zIPTypeV4, zpNumVec);\
    } else {\
        zErrNo = zNetUtils_.to_numaddr(zpIpStr, zIPTypeV6, zpNumVec);\
    };\
    zErrNo;  /* 宏返回值 */\
})

#define /*_i*/ zCONVERT_IPNUM_TO_STR(/*|_ull [2]|*/ zpNumVec, /*|char *|*/ zpIpStr) ({\
    _i zErrNo = 0;\
    if (0xff == zpNumVec[1] /* IPv4 */) {\
        zErrNo = zNetUtils_.to_straddr(zpNumVec, zIPTypeV4, zpIpStr);\
    } else {\
        zErrNo = zNetUtils_.to_straddr(zpNumVec, zIPTypeV6, zpIpStr);\
    } \
    zErrNo;  /* 宏返回值 */\
})


#endif  //  #ifndef ZDPOPS_H
