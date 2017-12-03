#include "zLibSsh.h"

#include <unistd.h>
#include <sys/select.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#define OPENSSL_THREAD_DEFINES
#include "libssh2.h"

extern struct zNetUtils__ zNetUtils_;

static _i
zssh_exec(char *zpHostIpAddr, char *zpHostPort, char *zpCmd,
        const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd, zAuthType__ zAuthType,
        char *zpRemoteOutPutBuf, _ui zSiz, pthread_mutex_t *zpCcurLock, char *zpErrBufOUT);

struct zLibSsh__ zLibSsh_ = {
    .exec = zssh_exec
};

/* select events dirven */
static _i
zwait_socket(_i zSd, LIBSSH2_SESSION *zSession) {
    struct timeval zTimeOut;
    fd_set zFd;
    fd_set *zWriteFd = NULL;
    fd_set *zReadFd = NULL;
    _i zDirection;

    zTimeOut.tv_sec = 10;
    zTimeOut.tv_usec = 0;

    FD_ZERO(&zFd);
    FD_SET(zSd, &zFd);

    /* now make sure we wait in the correct direction */
    zDirection = libssh2_session_block_directions(zSession);

    if(zDirection & LIBSSH2_SESSION_BLOCK_INBOUND) {
        zReadFd = &zFd;
    }

    if(zDirection & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
        zWriteFd = &zFd;
    }

    return select(zSd + 1, zReadFd, zWriteFd, NULL, &zTimeOut);
}

/*
 * 专用于多线程环境，必须指定 zpCcurLock 参数
 * zAuthType 置为 0 表示密码认证，置为 1 则表示 rsa 公钥认证
 * 若不需要远程执行结果返回，zpRemoteOutPutBuf 置为 NULL
 */
    /*
     * << 错误类型 >>
     * err1 bit[0]:服务端错误
     * err2 bit[1]:网络不通
     * err3 bit[2]:SSH 连接认证失败
     * err4 bit[3]:目标端磁盘容量不足
     * err5 bit[4]:目标端权限不足
     * err6 bit[5]:目标端文件冲突
     * err7 bit[6]:目标端布署后动作执行失败
     * err8 bit[7]:目标端收到重复布署指令(同一目标机的多个不同IP)
     */
static _i
zssh_exec(
        char *zpHostIpAddr, char *zpHostPort, char *zpCmd,
        const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd, zAuthType__ zAuthType,
        char *zpRemoteOutPutBuf, _ui zSiz,
        pthread_mutex_t *zpCcurLock,
        char *zpErrBufOUT __attribute__ ((__unused__))/* size: 256 */
        ) {

    _i zSd, zRet, zErrNo;
    LIBSSH2_SESSION *zSession;
    LIBSSH2_CHANNEL *zChannel;
    //char *zpExitSingal=(char *) -1;

    pthread_mutex_lock(zpCcurLock);
    if (0 != (zRet = libssh2_init(0))) {
        pthread_mutex_unlock(zpCcurLock);
        zPrint_Err(0, NULL, "libssh2_init(0): failed");
        return -1;
    }

    if (NULL == (zSession = libssh2_session_init())) {  // need lock ???
        pthread_mutex_unlock(zpCcurLock);
        libssh2_exit();
        zPrint_Err(0, NULL, "libssh2_session_init(): failed");
        return -1;
    }
    pthread_mutex_unlock(zpCcurLock);

    if (0 > (zSd = zNetUtils_.tcp_conn(zpHostIpAddr, zpHostPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        libssh2_session_free(zSession);
        libssh2_exit();
        zPrint_Err(0, NULL, "libssh2 tcp connect: failed");
        return -2;  /* 网络不通 */
    }

    /* tell libssh2 we want it all done non-blocking */
    libssh2_session_set_blocking(zSession, 0);

    while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_session_handshake(zSession, zSd)));

    if (0 != zRet) {
        libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        zPrint_Err(0, NULL, "libssh2_session_handshake failed");
        return -2;  /* 网络不通 */
    }

    if (zPassWordAuth == zAuthType) {  /* authenticate via zpPassWd */
        while (LIBSSH2_ERROR_EAGAIN ==
                (zRet = libssh2_userauth_password(zSession, zpUserName, zpPassWd)));
    } else {  /* public key */
        while (LIBSSH2_ERROR_EAGAIN ==
                (zRet = libssh2_userauth_publickey_fromfile(zSession, zpUserName, zpPubKeyPath, zpPrivateKeyPath, zpPassWd)));
    }

    if (0 != zRet) {
        libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        zPrint_Err(0, NULL, "libssh2: user auth failed(password and publickey)");
        return -3;  /* 认证失败 */
    }

    /* Exec non-blocking on the remote host */
    while(NULL == (zChannel= libssh2_channel_open_session(zSession))) {  // 会带来段错误：&& (LIBSSH2_ERROR_EAGAIN == libssh2_session_last_error(zSession, NULL, NULL,0))
        zwait_socket(zSd, zSession);
    }

    if(NULL == zChannel) {
        libssh2_session_disconnect(zSession, "Bye");
        libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        zPrint_Err(0, NULL, "libssh2_channel_open_session failed");
        return -1;
    }

    /* 多线程环境，必须复制到自身的栈中进行处理 */
    while(LIBSSH2_ERROR_EAGAIN ==
            (zRet = libssh2_channel_exec(zChannel, zpCmd))) {
        zwait_socket(zSd, zSession);
    }

    if( 0 != zRet) {
        libssh2_session_disconnect(zSession, "");
        libssh2_session_free(zSession);
        libssh2_channel_free(zChannel);
        libssh2_exit();
        close(zSd);
        zPrint_Err(0, NULL, "libssh2_channel_exec failed");
        return -1;
    }

    if (NULL != zpRemoteOutPutBuf) {
        for(;;) {
            _i zRet;
            do {
                if(0 < (zRet = libssh2_channel_read(zChannel, zpRemoteOutPutBuf, zSiz))) {
                    zpRemoteOutPutBuf += zRet;
                    zSiz -= zRet;
                } else {
                    if(LIBSSH2_ERROR_EAGAIN != zRet) {
                        libssh2_channel_free(zChannel);
                        libssh2_session_disconnect(zSession, "");
                        libssh2_session_free(zSession);
                        libssh2_exit();
                        close(zSd);
                        zPrint_Err(0, NULL, "libssh2_channel_read: failed");
                        return -1;
                    }
                }
            } while(0 < zRet);

            if( zRet == LIBSSH2_ERROR_EAGAIN ) {
                zwait_socket(zSd, zSession);
            } else {
                (zpRemoteOutPutBuf + 1)[0] = '\0';
                break;
            }
        }
    }

    zErrNo = -1;
    while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_close(zChannel))) { zwait_socket(zSd, zSession); }
    if(0 == zRet) {
        zErrNo = libssh2_channel_get_exit_status(zChannel);
        if (206 == zErrNo) {
            zErrNo = -6;  /* 文件冲突 */
        } else if (202 == zErrNo) {
            zErrNo = -2;  /* 目标机无法连接服务端 */
        } else if (203 == zErrNo) {
            zErrNo = -3;  /* 磁盘满 */
        } else {
            zErrNo = -1;  /* 未知错误 */
        }
        //libssh2_channel_get_exit_signal(zChannel, &zpExitSingal, NULL, NULL, NULL, NULL, NULL);
    }
    //if (NULL != zpExitSingal) {
    //    zErrNo = -1;
    //}

    libssh2_channel_free(zChannel);
    zChannel= NULL;

    libssh2_session_disconnect(zSession, "Bye");
    libssh2_session_free(zSession);
    libssh2_exit();

    close(zSd);
    return zErrNo;
}
