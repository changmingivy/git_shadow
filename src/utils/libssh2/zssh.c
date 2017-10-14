#ifndef _Z
    #include "../../zmain.c"
#endif

//#include "libssh2.h"

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

    zssh_exec(zpSshCcurIf->p_HostIpStrAddr, zpSshCcurIf->p_HostServPort, zpSshCcurIf->p_Cmd,
            zpSshCcurIf->p_UserName, zpSshCcurIf->p_PubKeyPath, zpSshCcurIf->p_PrivateKeyPath, zpSshCcurIf->p_PassWd, zpSshCcurIf->zAuthType,
            zpSshCcurIf->p_RemoteOutPutBuf, zpSshCcurIf->RemoteOutPutBufSiz, zpSshCcurIf->p_CcurLock);

    pthread_mutex_lock(zpSshCcurIf->p_CcurLock);
    (* (zpSshCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpSshCcurIf->p_CcurLock);
    pthread_cond_signal(zpSshCcurIf->p_CcurCond);

    return NULL;
};

/* 简化参数版函数 */
void *
zssh_ccur_simple(void  *zpIf) {
    zSshCcurInfo *zpSshCcurIf = (zSshCcurInfo *) zpIf;

    zssh_exec_simple(zpSshCcurIf->p_HostIpStrAddr, zpSshCcurIf->p_Cmd, zpSshCcurIf->p_CcurLock);

    pthread_mutex_lock(zpSshCcurIf->p_CcurLock);
    (* (zpSshCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpSshCcurIf->p_CcurLock);
    pthread_cond_signal(zpSshCcurIf->p_CcurCond);

    return NULL;
};

/* 远程主机初始化专用 */
void *
zssh_ccur_simple_init_host(void  *zpIf) {
    zSshCcurInfo *zpSshCcurIf = (zSshCcurInfo *) zpIf;

    _ui zHostId = zconvert_ip_str_to_bin(zpSshCcurIf->p_HostIpStrAddr);
    zDpResInfo *zpTmpIf = zpGlobRepoIf[zpSshCcurIf->RepoId]->p_DpResHashIf[zHostId % zDpHashSiz];
    for (; NULL != zpTmpIf; zpTmpIf = zpTmpIf->p_next) {
        if (zHostId == zpTmpIf->ClientAddr) {
            if (0 == zssh_exec_simple(zpSshCcurIf->p_HostIpStrAddr, zpSshCcurIf->p_Cmd, zpSshCcurIf->p_CcurLock)) {
                zpTmpIf->InitState = 1;
            } else {
                zpTmpIf->InitState = -1;
                zpGlobRepoIf[zpSshCcurIf->RepoId]->ResType[0] = -1;
            }

            pthread_mutex_lock(zpSshCcurIf->p_CcurLock);
            (* (zpSshCcurIf->p_TaskCnt))++;
            pthread_mutex_unlock(zpSshCcurIf->p_CcurLock);
            pthread_cond_signal(zpSshCcurIf->p_CcurCond);

            break;
        }
    }

    return NULL;
};
