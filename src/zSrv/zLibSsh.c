#include "zLibSsh.h"

static _i
zssh_exec(char *zpHostIpAddr, char *zpHostPort, char *zpCmd, const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd, _i zAuthType, char *zpRemoteOutPutBuf, _ui zSiz, pthread_mutex_t *zpCcurLock);

struct zLibSsh__ zLibSsh_ = { .exec = zssh_exec };

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

    if(zDirection & LIBSSH2_SESSION_BLOCK_INBOUND) { zReadFd = &zFd; }
    if(zDirection & LIBSSH2_SESSION_BLOCK_OUTBOUND) { zWriteFd = &zFd; }

    return select(zSd + 1, zReadFd, zWriteFd, NULL, &zTimeOut);
}

/*
 * 专用于多线程环境，必须指定 zpCcurLock 参数
 * zAuthType 置为 0 表示密码认证，置为 1 则表示 rsa 公钥认证
 * 若不需要远程执行结果返回，zpRemoteOutPutBuf 置为 NULL
 */
static _i
zssh_exec(char *zpHostIpAddr, char *zpHostPort, char *zpCmd, const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd, _i zAuthType, char *zpRemoteOutPutBuf, _ui zSiz, pthread_mutex_t *zpCcurLock) {

    _i zSd, zRet, zErrNo;
    time_t zBaseTimeStamp;
    LIBSSH2_SESSION *zSession;
    LIBSSH2_CHANNEL *zChannel;
    char *zpExitSingal=(char *) -1;

    pthread_mutex_lock(zpCcurLock);
    if (0 != (zRet = libssh2_init(0))) {
        pthread_mutex_unlock(zpCcurLock);
        return -1;
    }

    if (NULL == (zSession = libssh2_session_init())) {  // need lock ???
        pthread_mutex_unlock(zpCcurLock);
        libssh2_exit();
        return -1;
    }
    pthread_mutex_unlock(zpCcurLock);

    if (0 > (zSd = ztcp_connect(zpHostIpAddr, zpHostPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        libssh2_session_free(zSession);
        libssh2_exit();
        return -1;
    }

    /* tell libssh2 we want it all done non-blocking */
    libssh2_session_set_blocking(zSession, 0);

    zBaseTimeStamp = time(NULL);
    while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_session_handshake(zSession, zSd))) {
        if (10 < (time(NULL) - zBaseTimeStamp)) { goto z0; }
    }

    if (0 != zRet) {
z0: libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        return -1;
    }

    zBaseTimeStamp = time(NULL);
    if (0 == zAuthType) {  /* authenticate via zpPassWd */
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_password(zSession, zpUserName, zpPassWd))) {
            if (10 < (time(NULL) - zBaseTimeStamp)) { goto z1; }
        }
    } else {  /* public key */
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_publickey_fromfile(zSession, zpUserName, zpPubKeyPath, zpPrivateKeyPath, zpPassWd))) {
            if (10 < (time(NULL) - zBaseTimeStamp)) { goto z1; }
        }
    }

    if (0 != zRet) {
z1: libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        return -1;
    }

    /* Exec non-blocking on the remove host */
    zBaseTimeStamp = time(NULL);
    while(NULL == (zChannel= libssh2_channel_open_session(zSession))) {  // 会带来段错误：&& (LIBSSH2_ERROR_EAGAIN == libssh2_session_last_error(zSession, NULL, NULL,0))
        if (10 < (time(NULL) - zBaseTimeStamp)) { goto z2; }
        zwait_socket(zSd, zSession);
    }

    if(NULL == zChannel) {
z2: libssh2_session_disconnect(zSession, "");
        libssh2_session_free(zSession);
        libssh2_exit();
        close(zSd);
        return -1;
    }

    /* 多线程环境，必须复制到自身的栈中进行处理 */
    char zpSelfUnionCmd[zSshSelfIpDeclareBufSiz + strlen(zpCmd)];
    sprintf(zpSelfUnionCmd, "export ____zSelfIp='%s';%s", zpHostIpAddr, zpCmd);

    zBaseTimeStamp = time(NULL);
    while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_exec(zChannel, zpSelfUnionCmd))) {
        if (10 < (time(NULL) - zBaseTimeStamp)) { goto z3; }
        zwait_socket(zSd, zSession);
    }

    if( 0 != zRet) {
z3: libssh2_session_disconnect(zSession, "");
        libssh2_session_free(zSession);
        libssh2_channel_free(zChannel);
        libssh2_exit();
        close(zSd);
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
        libssh2_channel_get_exit_signal(zChannel, &zpExitSingal, NULL, NULL, NULL, NULL, NULL);
    }
    if (NULL != zpExitSingal) { zErrNo = -1; }

    libssh2_channel_free(zChannel);
    zChannel= NULL;

    libssh2_session_disconnect(zSession, "Bye");
    libssh2_session_free(zSession);
    libssh2_exit();

    close(zSd);
    return zErrNo;
}
