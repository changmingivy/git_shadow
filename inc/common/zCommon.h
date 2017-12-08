#ifndef ZCOMMON_H
#define ZCOMMON_H

#define zBytes(zNum) ((_i)((zNum) * sizeof(char)))
#define zSizeOf(zObj) ((_i)sizeof(zObj))

#define __z1 __attribute__ ((__nonnull__))

typedef enum {
    zProtoTcp = 0,
    zProtoUdp = 1,
    zProtoSctp = 3,
    zProtoNone = 4
} znet_proto_t;

typedef enum {
    zIpTypeV4 = 4,
    zIpTypeV6 = 6,
    zIpTypeNone = 9
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
#define zSet_Bit(zObj, zWhich) do {\
    (zObj) |= ((((zObj) >> (zWhich)) | 1) << (zWhich));\
} while(0)

// Unset bit meaning set a bit to 0;
// Index from 1.
#define zUnSet_Bit(zObj, zWhich) do {\
    (zObj) &= ~(((~(zObj) >> (zWhich)) | 1) << (zWhich));\
} while(0)

// Check bit meaning check if a bit is 1;
// Index from 1.
#define zCheck_Bit(zObj, zWhich) ((zObj) ^ ((zObj) & ~(((~(zObj) >> (zWhich)) | 1) << (zWhich))))

/*
 * =>>> Print Current Time <<<=
 */
#define /*_i*/ zPrint_Time(/*void*/) {\
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
#define zPrint_Err(zErrNo, zCause, zMsg) do {\
    zPrint_Time();\
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

#define zPrint_Err_Easy(zMsg) zPrint_Err(0, NULL, (zMsg))
#define zPrint_Err_Easy_Sys() zPrint_Err(errno, "", NULL)

#define zCheck_Null_Return(zRes, __VA_ARGS__) do{\
    if (NULL == (zRes)) {\
        zPrint_Err(errno, #zRes " == NULL", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Null_Exit(zRes) do{\
    if (NULL == (zRes)) {\
        zPrint_Err(errno, #zRes " == NULL", "");\
        _exit(1);\
    }\
} while(0)

#define zCheck_Negative_Return(zRes, __VA_ARGS__) do{\
    if (0 > (zRes)) {\
        zPrint_Err(errno, #zRes " < 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Negative_Exit(zRes) do{\
    if (0 > (zRes)) {\
        zPrint_Err(errno, #zRes " < 0", "");\
        _exit(1);\
    }\
} while(0)

#define zCheck_NotZero_Exit(zRes) do{\
    if (0 != (zRes)) {\
        zPrint_Err(errno, #zRes " < 0", "");\
        _exit(1);\
    }\
} while(0)

#define zCheck_Pthread_Func_Return(zRet, __VA_ARGS__) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPrint_Err(zX, #zRet " != 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Pthread_Func_Exit(zRet) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPrint_Err(zX, #zRet " != 0", "");\
        _exit(1);\
    }\
} while(0)

#define zLog_Err(zMSG) do{\
    syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", zMSG);\
} while(0)

/*
 * =>>> Memory Management <<<=
 */

#define zMem_Alloc(zpRet, zType, zCnt) do {\
    zCheck_Null_Exit( zpRet = malloc((zCnt) * sizeof(zType)) );\
} while(0)

#define zMem_Re_Alloc(zpRet, zType, zCnt, zpOldAddr) do {\
    zCheck_Null_Exit( zpRet = realloc((zpOldAddr), (zCnt) * sizeof(zType)) );\
} while(0)

#define zMem_C_Alloc(zpRet, zType, zCnt) do {\
    zCheck_Null_Exit( zpRet = calloc(zCnt, sizeof(zType)) );\
} while(0)

/*
#define zMap_Alloc(zpRet, zType, zCnt) do {\
    if (MAP_FAILED == ((zpRet) = mmap(NULL, (zCnt) * sizeof(zType), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED, -1, 0))) {\
        zPrint_Err(0, NULL, "mmap failed!");\
        _exit(1);\
    }\
} while(0)

#define zMap_Free(zpRet, zType, zCnt) do {\
    munmap(zpRet, (zCnt) * sizeof(zType));\
} while(0)
*/

/*
 * 信号处理，屏蔽除 SIGKILL、SIGSTOP、SIGSEGV、SIGALRM、SIGCHLD、SIGCLD、SIGUSR1、SIGUSR2 之外的所有信号，合计 24 种
 */
#define /*void*/ zIgnoreAllSignal(/*void*/) {\
    _i zSigSet[24] = {\
        SIGFPE, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,\
        SIGTERM, SIGBUS, SIGHUP, SIGSYS,\
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
}

#endif  //  #ifndef ZCOMMON_H
