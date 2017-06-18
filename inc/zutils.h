#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef zPTHREAD_H
#define zPTHREAD_H
#include <pthread.h>
#endif

/*
 * =>>> Aliases For All Basic Types <<<=
 */
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
}while(0)

// Unset bit meaning set a bit to 0;
// Index from 1.
#define zUnSet_Bit(zObj, zWhich) do {\
	(zObj) &= ~(((~(zObj) >> (zWhich)) | 1) << (zWhich));\
}while(0)

// Check bit meaning check if a bit is 1;
// Index from 1.
#define zCheck_Bit(zObj, zWhich) ((zObj) ^ ((zObj) & ~(((~(zObj) >> (zWhich)) | 1) << (zWhich))))


/*
 * =>>> Error Management <<<=
 */
#define zPrint_Err(zErrNo, zCause, zCustomContents) do{ fprintf(stderr, "\
	\033[31;01m====[ ERROR ]====\033[00m\n\
	\033[31;01mFile:\033[00m %s\n\
	\033[31;01mLine:\033[00m %d\n\
	\033[31;01mFunc:\033[00m %s\n\
	\033[31;01mCause:\033[00m %s\n\
	\033[31;01mDetail:\033[00m %s\n\n",\
	__FILE__,\
	__LINE__,\
	__func__,\
	zCause == NULL? "" : zCause,\
	(NULL == zCause) ? zCustomContents : strerror(zErrNo)); }while(0)

#define zCheck_Null_Warning(zRes) do{\
	if (NULL == (zRes)) {\
		zPrint_Err(errno, #zRes " == NULL", "");\
	}\
}while(0)

#define zCheck_Null_Exit(zRes) do{\
	if (NULL == (zRes)) {\
		zPrint_Err(errno, #zRes " == NULL", "");\
		exit(1);\
	}\
}while(0)

#define zCheck_Null_Thread_Exit(zRes) do{\
	if (NULL == (zRes)) {\
		zPrint_Err(errno, #zRes " == NULL", "");\
		pthread_exit(NULL);\
	}\
}while(0)

#define zCheck_Negative_Warning(zRes) do{\
	if (0 > (zRes)) {\
		zPrint_Err(errno, #zRes " < 0", "");\
	}\
}while(0)

#define zCheck_Negative_Exit(zRes) do{\
	if (0 > (zRes)) {\
		zPrint_Err(errno, #zRes " < 0", "");\
		exit(1);\
	}\
}while(0)

#define zCheck_Negative_Thread_Exit(zRes) do{\
	if (0 > (zRes)) {\
		zPrint_Err(errno, #zRes " < 0", "");\
		pthread_exit(NULL);\
	}\
}while(0)

#define zCheck_Pthread_Func_Warning(zRet) do{\
	if (0 != (zRet)) {\
		zPrint_Err(zRet, #zRet " != 0", "");\
	}\
}while(0)

#define zCheck_Pthread_Func_Exit(zRet) do{\
	if (0 != (zRet)) {\
		zPrint_Err(zRet, #zRet " != 0", "");\
		exit(1);\
	}\
}while(0)

#define zCheck_Pthread_Func_Thread_Exit(zRet) do{\
	if (0 != (zRet)) {\
		zPrint_Err(zRet, #zRet " != 0", "");\
		pthread_exit(NULL);\
	}\
}while(0)

#define zLog_Err(zMSG) do{\
	syslog(LOG_ERR|LOG_PID|LOG_CONS, "%s", zMSG);\
}while(0)

/*
 * =>>> Memory Management <<<=
 */
void *
zregister_malloc(size_t zSiz);

void *
zregister_realloc(void *zpPrev, size_t zSiz);

void *
zregister_calloc(int zCnt, size_t zSiz);

#define zMem_Alloc(zpReqBuf, zType, zCnt) do {\
	zpReqBuf = (zType *)zregister_malloc((zCnt) * sizeof(zType));\
}while(0)

#define zMem_Re_Alloc(zpReqBuf, zType, zpPrev, zSiz) do {\
	zpReqBuf = (zType *)zregister_realloc(zpPrev, zSiz);\
}while(0)

#define zMem_C_Alloc(zpReqBuf, zType, zCnt) do {\
	zpReqBuf = (zType *)zregister_calloc(zCnt, sizeof(zType));\
}while(0)

#define zFree_Memory_Common(zpObjToFree, zpBridgePointer) do {\
	while (NULL != zpObjToFree) {\
		zpBridgePointer = zpObjToFree->p_next;\
		free(zpObjToFree);\
		zpObjToFree = zpBridgePointer;\
	}\
	zpObjToFree = zpBridgePointer = NULL;\
}while(0)
