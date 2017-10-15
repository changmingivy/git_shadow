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

    _i zSd, zRet, zErrNo;
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

    /* 多线程环境，必须复制到自身的栈中进行处理 */
    char zpSelfUnionCmd[zSshSelfIpDeclareBufSiz + strlen(zpCmd)];
    sprintf(zpSelfUnionCmd, "export ____zSelfIp='%s';%s", zpHostIpAddr, zpCmd);

    while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_exec(zChannel, zpSelfUnionCmd))) { zwait_socket(zSd, zSession); }
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
    zDpCcurInfo *zpDpCcurIf = (zDpCcurInfo *) zpIf;

    zssh_exec(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_HostServPort, zpDpCcurIf->p_Cmd,
            zpDpCcurIf->p_UserName, zpDpCcurIf->p_PubKeyPath, zpDpCcurIf->p_PrivateKeyPath, zpDpCcurIf->p_PassWd, zpDpCcurIf->zAuthType,
            zpDpCcurIf->p_RemoteOutPutBuf, zpDpCcurIf->RemoteOutPutBufSiz, zpDpCcurIf->p_CcurLock);

    pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
    (* (zpDpCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
    pthread_cond_signal(zpDpCcurIf->p_CcurCond);

    return NULL;
};

/* 简化参数版函数 */
void *
zssh_ccur_simple(void  *zpIf) {
    zDpCcurInfo *zpDpCcurIf = (zDpCcurInfo *) zpIf;

    zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_Cmd, zpDpCcurIf->p_CcurLock);

    pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
    (* (zpDpCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
    pthread_cond_signal(zpDpCcurIf->p_CcurCond);

    return NULL;
};

/* 远程主机初始化专用 */
void *
zssh_ccur_simple_init_host(void  *zpIf) {
    zDpCcurInfo *zpDpCcurIf = (zDpCcurInfo *) zpIf;

    _ui zHostId = zconvert_ip_str_to_bin(zpDpCcurIf->p_HostIpStrAddr);
    zDpResInfo *zpTmpIf = zpGlobRepoIf[zpDpCcurIf->RepoId]->p_DpResHashIf[zHostId % zDpHashSiz];
    for (; NULL != zpTmpIf; zpTmpIf = zpTmpIf->p_next) {
        if (zHostId == zpTmpIf->ClientAddr) {
            if (0 == zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_Cmd, zpDpCcurIf->p_CcurLock)) {
                zpTmpIf->InitState = 1;
            } else {
                zpTmpIf->InitState = -1;
                zpGlobRepoIf[zpDpCcurIf->RepoId]->ResType[0] = -1;
            }

            pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
            (* (zpDpCcurIf->p_TaskCnt))++;
            pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
            pthread_cond_signal(zpDpCcurIf->p_CcurCond);

            break;
        }
    }

    return NULL;
};
