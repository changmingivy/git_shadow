#ifndef _Z
    #include "../../zmain.c"
#endif

#include "libssh2.h"

/* select events dirven */
_i
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
_i
zssh_exec(char *zpHostIpAddr, char *zpHostPort, char *zpCmd, const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd, _i zAuthType, char *zpRemoteOutPutBuf, _ui zSiz, pthread_mutex_t *zpCcurLock) {

    _i zSd, zRet, zErrNo, zSelfIpDeclareLen;
    LIBSSH2_SESSION *zSession;
    LIBSSH2_CHANNEL *zChannel;
    char *zpExitSingal=(char *) -1;

    pthread_mutex_lock(zpCcurLock);
    if (0 != (zRet = libssh2_init(0))) {
        pthread_mutex_unlock(zpCcurLock);
        return -1;
    }
    pthread_mutex_unlock(zpCcurLock);

    if (NULL == (zSession = libssh2_session_init())) {  // need lock ???
        libssh2_exit();
        return -1;
    }

    if (0 > (zSd = ztcp_connect(zpHostIpAddr, zpHostPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        libssh2_session_free(zSession);
        libssh2_exit();
        return -1;
    }

    /* tell libssh2 we want it all done non-blocking */
    libssh2_session_set_blocking(zSession, 0);

    while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_session_handshake(zSession, zSd)));
    if (0 != zRet) {
        libssh2_session_free(zSession);
        libssh2_exit();
        return -1;
    }

    if (0 == zAuthType) {  /* authenticate via zpPassWd */
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_password(zSession, zpUserName, zpPassWd)));
    } else {  /* public key */
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_publickey_fromfile(zSession, zpUserName, zpPubKeyPath, zpPrivateKeyPath, zpPassWd)));
    }
    if (0 != zRet) {
        libssh2_session_free(zSession);
        libssh2_exit();
        return -1;
    }

    /* Exec non-blocking on the remove host */
    while((NULL ==  (zChannel= libssh2_channel_open_session(zSession)))
            && (LIBSSH2_ERROR_EAGAIN == libssh2_session_last_error(zSession, NULL, NULL,0))) {
        zwait_socket(zSd, zSession);
    }
    if(NULL == zChannel) {
        libssh2_session_disconnect(zSession, "");
        libssh2_session_free(zSession);
        libssh2_exit();
        return -1;
    }

    /* 在命令的最前端追加用于告知自身IP的Shell变量声明 */
    zSelfIpDeclareLen = sprintf(zpCmd, "export ____zSelfIp='%s';", zpHostIpAddr);
    for (_i zCnter = zSelfIpDeclareLen; zCnter < zSshSelfIpDeclareBufSiz; zCnter++) {
        zpCmd[zCnter] = ' ';
    }

    while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_exec(zChannel, zpCmd))) { zwait_socket(zSd, zSession); }
    if( 0 != zRet) {
        libssh2_channel_free(zChannel);
        libssh2_session_disconnect(zSession, "");
        libssh2_session_free(zSession);
        libssh2_exit();
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

    close(zSd);
    libssh2_exit();

    return zErrNo;
}

/* 简化参数版函数 */
_i
zssh_exec_simple(char *zpHostIpAddr, char *zpCmd, pthread_mutex_t *zpCcurLock) {
    return zssh_exec(zpHostIpAddr, "22", zpCmd, "git", "/home/git/.ssh/id_rsa.pub", "/home/git/.ssh/id_rsa", NULL, 1, NULL, 0, zpCcurLock);
}

/*
 * 线程并发函数
 */
void *
zssh_ccur(void  *zpIf) {
    zSshCcurInfo *zpSshCcurIf = (zSshCcurInfo *) zpIf;

    zssh_exec(zpSshCcurIf->zpHostIpAddr, zpSshCcurIf->zpHostServPort, zpSshCcurIf->zpCmd,
            zpSshCcurIf->zpUserName, zpSshCcurIf->zpPubKeyPath, zpSshCcurIf->zpPrivateKeyPath, zpSshCcurIf->zpPassWd, zpSshCcurIf->zAuthType,
            zpSshCcurIf->zpRemoteOutPutBuf, zpSshCcurIf->zRemoteOutPutBufSiz, zpSshCcurIf->zpCcurLock);

    pthread_mutex_lock(zpSshCcurIf->zpCcurLock);
    (* (zpSshCcurIf->zpTaskCnt))++;
    pthread_mutex_unlock(zpSshCcurIf->zpCcurLock);
    pthread_cond_signal(zpSshCcurIf->zpCcurCond);

    return NULL;
};

/* 简化参数版函数 */
void *
zssh_ccur_simple(void  *zpIf) {
    zSshCcurInfo *zpSshCcurIf = (zSshCcurInfo *) zpIf;

    zssh_exec_simple(zpSshCcurIf->zpHostIpAddr, zpSshCcurIf->zpCmd, zpSshCcurIf->zpCcurLock);

    pthread_mutex_lock(zpSshCcurIf->zpCcurLock);
    (* (zpSshCcurIf->zpTaskCnt))++;
    pthread_mutex_unlock(zpSshCcurIf->zpCcurLock);
    pthread_cond_signal(zpSshCcurIf->zpCcurCond);

    return NULL;
};



// // !!! TEST !!!
// pthread_mutex_t zTestLock = PTHREAD_MUTEX_INITIALIZER;
// _i
// main(void) {
//     static char zBuf[4096];
//     static zSshCcurInfo zSshCcurIf;
//
//     zSshCcurIf.zpHostIpAddr = "127.0.0.1";
//     zSshCcurIf.zpHostServPort = "22";
//     zSshCcurIf.zpCmd = "echo \"libssh2 test [`date`]\" >> /tmp/testfile";
//     zSshCcurIf.zAuthType = 1;
//     zSshCcurIf.zpUserName = "fh";
//     zSshCcurIf.zpPubKeyPath = "/home/fh/.ssh/id_rsa.pub";
//     zSshCcurIf.zpPrivateKeyPath = "/home/fh/.ssh/id_rsa";
//     zSshCcurIf.zpPassWd = NULL;
//     zSshCcurIf.zpRemoteOutPutBuf = zBuf;
//     zSshCcurIf.zRemoteOutPutBufSiz = 4096;
//     zSshCcurIf.zpCcurLock = &zTestLock;
//
//     for (_i zCnter = 0; zCnter < 200; zCnter++) {
//     //    fprintf(stderr, "%d\n", zCnter);
//         zAdd_To_Thread_Pool(zssh_ccur, &zSshCcurIf);
//     }
//
//     //zssh_exec("127.0.0.1", "22", "printf 'Hello!\n'; echo \"libssh2 test [`date`]\" >> /tmp/testfile", "fh", "/home/fh/.ssh/id_rsa.pub", "/home/fh/.ssh/id_rsa", "", 1, zBuf, 4096, NULL);
//     return 0;
// }
