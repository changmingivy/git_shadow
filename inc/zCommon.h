
#define zBytes(zNum) ((_i)((zNum) * sizeof(char)))
#define zSizeOf(zObj) ((_i)sizeof(zObj))

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
#define zPrint_Time() do {\
    time_t ____zMarkNow = time(NULL);  /* Mark the time when this process start */\
    struct tm *____zpCurrentTimeIf = localtime(&____zMarkNow);  /* Current time(total secends from 1900-01-01 00:00:00) */\
    fprintf(stderr, "\033[31m[%d-%d-%d %d:%d:%d] \033[00m",\
			____zpCurrentTimeIf->tm_year + 1900,\
			____zpCurrentTimeIf->tm_mon,\
			____zpCurrentTimeIf->tm_mday,\
			____zpCurrentTimeIf->tm_hour,\
			____zpCurrentTimeIf->tm_min,\
			____zpCurrentTimeIf->tm_sec\
			);\
} while(0)

/*
 * =>>> Error Management <<<=
 */
#define zPrint_Err(zErrNo, zCause, zCustomContents) do{ \
    zPrint_Time(); \
    fprintf(stderr,\
    "\033[31;01m\n====[ ERROR ]====\033[00m\n"\
    "\033[31;01mFile:\033[00m %s\n"\
    "\033[31;01mLine:\033[00m %d\n"\
    "\033[31;01mFunc:\033[00m %s\n"\
    "\033[31;01mCause:\033[00m %s\n"\
    "\033[31;01mDetail:\033[00m %s\n\n",\
    __FILE__,\
    __LINE__,\
    __func__,\
    zCause == NULL? "" : zCause,\
    (NULL == zCause) ? zCustomContents : strerror(zErrNo));\
} while(0)

#define zCheck_Null_Return(zRes, __VA_ARGS__) do{\
    void *zpMiddleTmpPoint = zRes;\
    if (NULL == (zpMiddleTmpPoint)) {\
        zPrint_Err(errno, #zRes " == NULL", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Null_Exit(zRes) do{\
    void *zpMiddleTmpPoint = (zRes);\
    if (NULL == (zpMiddleTmpPoint)) {\
        zPrint_Err(errno, #zRes " == NULL", "");\
        exit(1);\
    }\
} while(0)

#define zCheck_Negative_Return(zRes, __VA_ARGS__) do{\
    _i zX = (zRes);\
    if (0 > zX) {\
        zPrint_Err(errno, #zRes " < 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Negative_Exit(zRes) do{\
    _i zX = (zRes);\
    if (0 > zX) {\
        zPrint_Err(errno, #zRes " < 0", "");\
        exit(1);\
    }\
} while(0)

#define zCheck_Pthread_Func_Return(zRet, __VA_ARGS__) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPrint_Err(zRet, #zRet " != 0", "");\
        return __VA_ARGS__;\
    }\
} while(0)

#define zCheck_Pthread_Func_Exit(zRet) do{\
    _i zX = (zRet);\
    if (0 != zX) {\
        zPrint_Err(zRet, #zRet " != 0", "");\
        exit(1);\
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

#define zFree_Memory_Common(zpObjToFree, zpBridgePointer) do {\
    while (NULL != zpObjToFree) {\
        zpBridgePointer = zpObjToFree->p_next;\
        free(zpObjToFree);\
        zpObjToFree = zpBridgePointer;\
    }\
    zpObjToFree = zpBridgePointer = NULL;\
} while(0)

// #define zMap_Alloc(zpRet, zType, zCnt) do {\
// 	if (MAP_FAILED == ((zpRet) = mmap(NULL, (zCnt) * sizeof(zType), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED, -1, 0))) {\
// 		zPrint_Err(0, NULL, "mmap failed!");\
// 		exit(1);\
// 	}\
// } while(0)
// 
// #define zMap_Free(zpRet, zType, zCnt) do {\
// 	munmap(zpRet, (zCnt) * sizeof(zType));\
// } while(0)

/*
 * 信号处理，屏蔽除 SIGKILL、SIGSTOP、SIGSEGV、SIGALRM、SIGCHLD、SIGCLD 之外的所有信号，合计 26 种
 */
#define zIgnoreAllSignal() do {\
    _i ____zSigSet[26] = {\
        SIGFPE, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT,\
        SIGTERM, SIGBUS, SIGHUP, SIGUSR1, SIGSYS, SIGUSR2,\
        SIGTSTP, SIGTTIN, SIGTTOU, SIGURG, SIGXCPU, SIGXFSZ,\
        SIGPROF, SIGWINCH, SIGCONT, SIGPIPE, SIGIOT, SIGIO\
    };\
\
    struct sigaction zSigActionIf;\
    zSigActionIf.sa_handler = SIG_IGN;\
    sigfillset(&zSigActionIf.sa_mask);\
    zSigActionIf.sa_flags = 0;\
\
    sigaction(____zSigSet[0], &zSigActionIf, NULL);\
    sigaction(____zSigSet[1], &zSigActionIf, NULL);\
    sigaction(____zSigSet[2], &zSigActionIf, NULL);\
    sigaction(____zSigSet[3], &zSigActionIf, NULL);\
    sigaction(____zSigSet[4], &zSigActionIf, NULL);\
    sigaction(____zSigSet[5], &zSigActionIf, NULL);\
    sigaction(____zSigSet[6], &zSigActionIf, NULL);\
    sigaction(____zSigSet[7], &zSigActionIf, NULL);\
    sigaction(____zSigSet[8], &zSigActionIf, NULL);\
    sigaction(____zSigSet[9], &zSigActionIf, NULL);\
    sigaction(____zSigSet[10], &zSigActionIf, NULL);\
    sigaction(____zSigSet[11], &zSigActionIf, NULL);\
    sigaction(____zSigSet[12], &zSigActionIf, NULL);\
    sigaction(____zSigSet[13], &zSigActionIf, NULL);\
    sigaction(____zSigSet[14], &zSigActionIf, NULL);\
    sigaction(____zSigSet[15], &zSigActionIf, NULL);\
    sigaction(____zSigSet[16], &zSigActionIf, NULL);\
    sigaction(____zSigSet[17], &zSigActionIf, NULL);\
    sigaction(____zSigSet[18], &zSigActionIf, NULL);\
    sigaction(____zSigSet[19], &zSigActionIf, NULL);\
    sigaction(____zSigSet[20], &zSigActionIf, NULL);\
    sigaction(____zSigSet[21], &zSigActionIf, NULL);\
    sigaction(____zSigSet[22], &zSigActionIf, NULL);\
    sigaction(____zSigSet[23], &zSigActionIf, NULL);\
    sigaction(____zSigSet[24], &zSigActionIf, NULL);\
    sigaction(____zSigSet[25], &zSigActionIf, NULL);\
} while(0)
