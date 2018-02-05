#ifndef ZCOMMON_H
#define ZCOMMON_H

#ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
#endif

#include <sys/socket.h>

extern char *zpProcName;
extern size_t zProcNameBufLen;

#define zBYTES(zNum) ((_i)((zNum) * sizeof(char)))
#define zSIZEOF(zObj) ((_i)sizeof(zObj))

/* 内存池结构 */
struct zMemPool__ {
    struct zMemPool__ *p_prev;
    char pool[];
};

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
    time_t MarkNow = time(NULL);  /* Mark the time when this process start */\
    struct tm *pCurrentTime_ = localtime(&MarkNow);  /* Current time(total secends from 1900-01-01 00:00:00) */\
    fprintf(stderr, "\033[31m[ %d-%d-%d %d:%d:%d ]\033[00m",\
            pCurrentTime_->tm_year + 1900,\
            pCurrentTime_->tm_mon + 1,  /* Month (0-11) */\
            pCurrentTime_->tm_mday,\
            pCurrentTime_->tm_hour,\
            pCurrentTime_->tm_min,\
            pCurrentTime_->tm_sec);\
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

#define zGIT_SHADOW_LOG_ERR(zErrNo, zCause, zMsg) do {\
    time_t MarkNow = time(NULL);\
    struct tm *pCurrentTime_ = localtime(&MarkNow);\
    _i Len = 1;\
    pid_t Pid = getpid();\
    char Buf[510];\
    Buf[0] = '9';\
\
    Len += snprintf(Buf + Len, 510 - Len,\
            "\n\033[31m[ %d-%d-%d %d:%d:%d ]\033[00m ",\
            pCurrentTime_->tm_year + 1900,\
            pCurrentTime_->tm_mon + 1,  /* Month (0-11) */\
            pCurrentTime_->tm_mday,\
            pCurrentTime_->tm_hour,\
            pCurrentTime_->tm_min,\
            pCurrentTime_->tm_sec);\
\
    if (NULL == zpRepo_) {\
        snprintf(Buf + Len, 510 - Len,\
                "\033[31;01mpid:\033[00m %d "\
                "\033[31;01mfiLe:\033[00m %s "\
                "\033[31;01mline:\033[00m %d "\
                "\033[31;01mfunc:\033[00m %s "\
                "\n%s",\
                Pid,\
                __FILE__,\
                __LINE__,\
                __func__,\
                NULL == (zCause) ? (NULL == (zMsg) ? "" : (zMsg)) : strerror(zErrNo));\
\
        zRun_.p_sysInfo_->ops_udp[9](Buf + 1, 0, NULL, 0);\
    } else {\
        Len += snprintf(Buf + Len, 510 - Len,\
                "\033[31;01mpid:\033[00m %d "\
                "\033[31;01mfiLe:\033[00m %s "\
                "\033[31;01mline:\033[00m %d "\
                "\033[31;01mfunc:\033[00m %s "\
                "\n%s %s",\
                Pid,\
                __FILE__,\
                __LINE__,\
                __func__,\
                zpProcName, NULL == (zCause) ? (NULL == (zMsg) ? "" : (zMsg)) : strerror(zErrNo));\
\
        sendto(zpRepo_->unSd, Buf, 1 + Len, MSG_NOSIGNAL,\
                (struct sockaddr *) & zRun_.p_sysInfo_->unAddrMaster, zRun_.p_sysInfo_->unAddrLenMaster);\
    }\
} while(0)

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
    _i SigSet[25] = {\
        SIGFPE, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,\
        SIGTERM, SIGBUS, SIGHUP, SIGSYS, SIGALRM,\
        SIGTSTP, SIGTTIN, SIGTTOU, SIGURG, SIGXCPU, SIGXFSZ,\
        SIGPROF, SIGWINCH, SIGCONT, SIGPIPE, SIGIOT, SIGIO\
    };\
\
    struct sigaction SigAction_;\
    SigAction_.sa_handler = SIG_IGN;\
    sigfillset(&SigAction_.sa_mask);\
    SigAction_.sa_flags = 0;\
\
    sigaction(SigSet[0], &SigAction_, NULL);\
    sigaction(SigSet[1], &SigAction_, NULL);\
    sigaction(SigSet[2], &SigAction_, NULL);\
    sigaction(SigSet[3], &SigAction_, NULL);\
    sigaction(SigSet[4], &SigAction_, NULL);\
    sigaction(SigSet[5], &SigAction_, NULL);\
    sigaction(SigSet[6], &SigAction_, NULL);\
    sigaction(SigSet[7], &SigAction_, NULL);\
    sigaction(SigSet[8], &SigAction_, NULL);\
    sigaction(SigSet[9], &SigAction_, NULL);\
    sigaction(SigSet[10], &SigAction_, NULL);\
    sigaction(SigSet[11], &SigAction_, NULL);\
    sigaction(SigSet[12], &SigAction_, NULL);\
    sigaction(SigSet[13], &SigAction_, NULL);\
    sigaction(SigSet[14], &SigAction_, NULL);\
    sigaction(SigSet[15], &SigAction_, NULL);\
    sigaction(SigSet[16], &SigAction_, NULL);\
    sigaction(SigSet[17], &SigAction_, NULL);\
    sigaction(SigSet[18], &SigAction_, NULL);\
    sigaction(SigSet[19], &SigAction_, NULL);\
    sigaction(SigSet[20], &SigAction_, NULL);\
    sigaction(SigSet[21], &SigAction_, NULL);\
    sigaction(SigSet[22], &SigAction_, NULL);\
    sigaction(SigSet[23], &SigAction_, NULL);\
    sigaction(SigSet[24], &SigAction_, NULL);\
}

#endif  //  #ifndef ZCOMMON_H
