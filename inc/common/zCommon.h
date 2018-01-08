#include <sys/socket.h>

#include "zRun.h"
extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

#ifndef ZCOMMON_H
#define ZCOMMON_H

#define zBYTES(zNum) ((_i)((zNum) * sizeof(char)))
#define zSIZEOF(zObj) ((_i)sizeof(zObj))

typedef enum {
    zProtoTCP = 0,
    zProtoUDP = 1,
    zProtoSCTP = 2,
    zProtoNONE = 3
} znet_proto_t;

typedef enum {
    zIPTypeV4 = 4,
    zIPTypeV6 = 6,
    zIPTypeNONE = 9
} zip_t;

typedef enum {
    zFalse = 0,
    zTrue = 1
} zbool_t;

typedef enum {
    zPubKeyAuth = 0,
    zPassWordAuth = 1,
    zNoneAuth = 3
} znet_auth_t;

typedef enum {
    zStr = 0,
    zI32 = 1,
    zI64 = 2,
    zF32 = 3,
    zF64 = 4
} zjson_value_t;


/*
 * =>>> Aliases For All Basic Types <<<=
 */
#define _s signed short int
#define _us unsigned short int
#define _i signed int
#define _ui unsigned int
#define _l signed long int
#define _ul unsigned long int
#define _ll signed long long int
#define _ull unsigned long long int

#define _f float
#define _d double

#define _c signed char
#define _uc unsigned char

/*
 * =>>> Bit Management <<<=
 */

// Set bit meaning set a bit to 1;
// Index from 1.
#define zSET_BIT(zObj, zWhich) do {\
    (zObj) |= ((((zObj) >> (zWhich)) | 1) << (zWhich));\
} while(0)

// Unset bit meaning set a bit to 0;
// Index from 1.
#define zUNSET_BIT(zObj, zWhich) do {\
    (zObj) &= ~(((~(zObj) >> (zWhich)) | 1) << (zWhich));\
} while(0)

// Check bit meaning check if a bit is 1;
// Index from 1.
#define zCHECK_BIT(zObj, zWhich) ((zObj) ^ ((zObj) & ~(((~(zObj) >> (zWhich)) | 1) << (zWhich))))

/*
 * =>>> Print Current Time <<<=
 */
#define /*_i*/ zPRINT_TIME(/*void*/) {\
    time_t zMarkNow = time(NULL);  /* Mark the time when this process start */\
    struct tm *zpCurrentTime_ = localtime(&zMarkNow);  /* Current time(total secends from 1900-01-01 00:00:00) */\
    fprintf(stderr, "\033[31m[ %d-%d-%d %d:%d:%d ]\033[00m",\
            zpCurrentTime_->tm_year + 1900,\
            zpCurrentTime_->tm_mon + 1,  /* Month (0-11) */\
            zpCurrentTime_->tm_mday,\
            zpCurrentTime_->tm_hour,\
            zpCurrentTime_->tm_min,\
            zpCurrentTime_->tm_sec);\
}

/*
 * =>>> Error Management <<<=
 */
#define zPRINT_ERR(zErrNo, zCause, zMsg) do {\
    zPRINT_TIME();\
    fprintf(stderr,\
    "\033[31;01m[ ERROR ] \033[00m"\
    "\033[31;01m__FIlE__:\033[00m %s; "\
    "\033[31;01m__LINE__:\033[00m %d; "\
    "\033[31;01m__FUNC__:\033[00m %s; "\
    "\033[31;01m__CAUSE__:\033[00m %s; "\
    "\033[31;01m__DETAIL__:\033[00m %s\n",\
    __FILE__,\
    __LINE__,\
    __func__,\
    NULL == (zCause) ? "" : (zCause),\
    NULL == (zCause) ? (NULL == (zMsg) ? "" : (zMsg)) : strerror(zErrNo));\
} while(0)

#define zGIT_SHADOW_LOG_ERR(zErrNo, zCause, zMsg) {\
    time_t zMarkNow = time(NULL);\
    struct tm *zpCurrentTime_ = localtime(&zMarkNow);\
    _i zLen = 1;\
    pid_t zPid = getpid();\
    char zBuf[510];\
    zBuf[0] = '9';\
\
    zLen += snprintf(zBuf + zLen, 510 - zLen,\
            "\033[31m[ %d-%d-%d %d:%d:%d ]\033[00m",\
            zpCurrentTime_->tm_year + 1900,\
            zpCurrentTime_->tm_mon + 1,  /* Month (0-11) */\
            zpCurrentTime_->tm_mday,\
            zpCurrentTime_->tm_hour,\
            zpCurrentTime_->tm_min,\
            zpCurrentTime_->tm_sec);\
\
    if (zPid == zRun_.p_sysInfo_->masterPid) {\
        snprintf(zBuf + zLen, 510 - zLen,\
                "\033[31;01mpid:\033[00m %d; "\
                "\033[31;01mfiLe:\033[00m %s; "\
                "\033[31;01mline:\033[00m %d; "\
                "\033[31;01mfunc:\033[00m %s; "\
                "\033[31;01mcause:\033[00m %s; "\
                "\033[31;01mdetail:\033[00m MASTER %s\n",\
                zPid,\
                __FILE__,\
                __LINE__,\
                __func__,\
                NULL == (zCause) ? "" : (zCause),\
                NULL == (zCause) ? (NULL == (zMsg) ? "" : (zMsg)) : strerror(zErrNo));\
\
        zRun_.p_sysInfo_->ops_udp[9](zBuf, 0, NULL, 0);\
    } else {\
        zLen += snprintf(zBuf + zLen, 510 - zLen,\
                "\033[31;01mpid:\033[00m %d; "\
                "\033[31;01mfiLe:\033[00m %s; "\
                "\033[31;01mline:\033[00m %d; "\
                "\033[31;01mfunc:\033[00m %s; "\
                "\033[31;01mcause:\033[00m %s; "\
                "\033[31;01mdetail:\033[00m %s %s\n",\
                zPid,\
                __FILE__,\
                __LINE__,\
                __func__,\
                NULL == (zCause) ? "" : (zCause),\
                zpRepo_->p_procName, NULL == (zCause) ? (NULL == (zMsg) ? "" : (zMsg)) : strerror(zErrNo));\
\
        sendto(zpRepo_->unSd, zBuf, zLen, MSG_NOSIGNAL,\
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster, zRun_.p_sysInfo_->unAddrLenMaster);\
    }\
}

#define zPRINT_ERR_EASY(zMsg) zGIT_SHADOW_LOG_ERR(0, NULL, (zMsg))
#define zPRINT_ERR_EASY_SYS() zGIT_SHADOW_LOG_ERR(errno, "", NULL)

#define zSYSLOG_ERR(zMSG) do{\
    syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", zMSG);\
} while(0)

#define zCHECK_NULL_RETURN(zRes, __VA_ARGS__) do{\
    if (NULL == (zRes)) {\
        zPRINT_ERR(errno, #zRes " == NULL", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCHECK_NULL_EXIT(zRes) do{\
    if (NULL == (zRes)) {\
        zPRINT_ERR(errno, #zRes " == NULL", "");\
        exit(1);\
    }\
} while(0)

#define zCHECK_NEGATIVE_RETURN(zRes, __VA_ARGS__) do{\
    if (0 > (zRes)) {\
        zPRINT_ERR(errno, #zRes " < 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCHECK_NEGATIVE_EXIT(zRes) do{\
    if (0 > (zRes)) {\
        zPRINT_ERR(errno, #zRes " < 0", "");\
        exit(1);\
    }\
} while(0)

#define zCHECK_NOTZERO_EXIT(zRes) do{\
    if (0 != (zRes)) {\
        zPRINT_ERR(errno, #zRes " < 0", "");\
        exit(1);\
    }\
} while(0)

#define zCHECK_PTHREAD_FUNC_RETURN(zRet, __VA_ARGS__) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPRINT_ERR(zX, #zRet " != 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCHECK_PTHREAD_FUNC_EXIT(zRet) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPRINT_ERR(zX, #zRet " != 0", "");\
        exit(1);\
    }\
} while(0)

/*
 * =>>> Memory Management <<<=
 */

#define zMEM_ALLOC(zpRet, zType, zCnt) do {\
    zCHECK_NULL_EXIT( zpRet = malloc((zCnt) * sizeof(zType)) );\
} while(0)

#define zMEM_RE_ALLOC(zpRet, zType, zCnt, zpOldAddr) do {\
    zCHECK_NULL_EXIT( zpRet = realloc((zpOldAddr), (zCnt) * sizeof(zType)) );\
} while(0)

#define zMEM_C_ALLOC(zpRet, zType, zCnt) do {\
    zCHECK_NULL_EXIT( zpRet = calloc(zCnt, sizeof(zType)) );\
} while(0)

/*
#define zMAP_ALLOC(zpRet, zType, zCnt) do {\
    if (MAP_FAILED == ((zpRet) = mmap(NULL, (zCnt) * sizeof(zType), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED, -1, 0))) {\
        zPRINT_ERR(0, NULL, "mmap failed!");\
        exit(1);\
    }\
} while(0)

#define zMAP_FREE(zpRet, zType, zCnt) do {\
    munmap(zpRet, (zCnt) * sizeof(zType));\
} while(0)
*/

/*
 * 信号处理，屏蔽除 SIGKILL、SIGSTOP、SIGSEGV、SIGCHLD、SIGCLD、SIGUSR1、SIGUSR2 之外的所有信号，合计 25 种
 */
#define /*void*/ zIGNORE_ALL_SIGNAL(/*void*/) {\
    _i zSigSet[25] = {\
        SIGFPE, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,\
        SIGTERM, SIGBUS, SIGHUP, SIGSYS, SIGALRM,\
        SIGTSTP, SIGTTIN, SIGTTOU, SIGURG, SIGXCPU, SIGXFSZ,\
        SIGPROF, SIGWINCH, SIGCONT, SIGPIPE, SIGIOT, SIGIO\
    };\
\
    struct sigaction zSigAction_;\
    zSigAction_.sa_handler = SIG_IGN;\
    sigfillset(&zSigAction_.sa_mask);\
    zSigAction_.sa_flags = 0;\
\
    sigaction(zSigSet[0], &zSigAction_, NULL);\
    sigaction(zSigSet[1], &zSigAction_, NULL);\
    sigaction(zSigSet[2], &zSigAction_, NULL);\
    sigaction(zSigSet[3], &zSigAction_, NULL);\
    sigaction(zSigSet[4], &zSigAction_, NULL);\
    sigaction(zSigSet[5], &zSigAction_, NULL);\
    sigaction(zSigSet[6], &zSigAction_, NULL);\
    sigaction(zSigSet[7], &zSigAction_, NULL);\
    sigaction(zSigSet[8], &zSigAction_, NULL);\
    sigaction(zSigSet[9], &zSigAction_, NULL);\
    sigaction(zSigSet[10], &zSigAction_, NULL);\
    sigaction(zSigSet[11], &zSigAction_, NULL);\
    sigaction(zSigSet[12], &zSigAction_, NULL);\
    sigaction(zSigSet[13], &zSigAction_, NULL);\
    sigaction(zSigSet[14], &zSigAction_, NULL);\
    sigaction(zSigSet[15], &zSigAction_, NULL);\
    sigaction(zSigSet[16], &zSigAction_, NULL);\
    sigaction(zSigSet[17], &zSigAction_, NULL);\
    sigaction(zSigSet[18], &zSigAction_, NULL);\
    sigaction(zSigSet[19], &zSigAction_, NULL);\
    sigaction(zSigSet[20], &zSigAction_, NULL);\
    sigaction(zSigSet[21], &zSigAction_, NULL);\
    sigaction(zSigSet[22], &zSigAction_, NULL);\
    sigaction(zSigSet[23], &zSigAction_, NULL);\
    sigaction(zSigSet[24], &zSigAction_, NULL);\
}

#endif  //  #ifndef ZCOMMON_H
