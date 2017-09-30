#ifndef _Z
    #include "../../zmain.c"
#endif

#include <libssh2.h>
 
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
 * 多线程并必环境，改须指定 zpCcurLock 参数
 * zAuthType 置为 0 表示密码认证，置为 1 则表示 rsa 公钥认证
 * 若不需要远程执行结果返回，zpRemoteOutPutBuf 置为 NULL
 */
_i
zssh_exec(char *zpHostIpv4Addr, char *zpHostPort, const char *zpCmd,
        const char *zpUserName, const char *zpPubKeyPath, const char *zpPrivateKeyPath, const char *zpPassWd,
        _i zAuthType, char *zpRemoteOutPutBuf, _ui zSiz, pthread_mutex_t *zpCcutLock) {

    _i zSd, zRet, zErrNo;
    LIBSSH2_SESSION *zSession;
    LIBSSH2_CHANNEL *zChannel;
    char *zpExitSingal=(char *) -1;
 
    if (NULL != zpCcutLock) { pthread_mutex_lock(zpCcutLock); }
    if (0 != (zRet = libssh2_init(0))) {
        if (NULL != zpCcutLock) { pthread_mutex_unlock(zpCcutLock); }
        return -1;
    }

    if (0 > (zSd = ztcp_connect(zpHostIpv4Addr, zpHostPort, AI_NUMERICHOST | AI_NUMERICSERV))) { return -1; }
    if (0 != (zSession = libssh2_session_init())) { return -1; }

    /* tell libssh2 we want it all done non-blocking */ 
    libssh2_session_set_blocking(zSession, 0);
 
    while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_session_handshake(zSession, zSd)));
    if (0 != zRet) { return -1; }
 
    if (0 == zAuthType) {  /* authenticate via zpPassWd */ 
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_password(zSession, zpUserName, zpPassWd)));
        if (zRet) { goto shutdown; }
    } else { /* public key */ 
        while (LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_userauth_publickey_fromfile(zSession, zpUserName, zpPubKeyPath, zpPrivateKeyPath, zpPassWd)));
        if (0 != zRet) { goto shutdown; }
    }
 
    /* Exec non-blocking on the remove host */ 
    while((NULL ==  (zChannel= libssh2_channel_open_session(zSession)))
			&& (LIBSSH2_ERROR_EAGAIN == libssh2_session_last_error(zSession,NULL,NULL,0))) {
		zwait_socket(zSd, zSession);
	}
    if(NULL == zChannel) { exit(1); }

    while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_exec(zChannel, zpCmd))) { zwait_socket(zSd, zSession); }
    if( 0 != zRet) { exit(1); }

	if (NULL != zpRemoteOutPutBuf) {
    	for(;;) {
    	    _i zRet;
    	    do {
    	        if(0 < (zRet = libssh2_channel_read(zChannel, zpRemoteOutPutBuf, zSiz))) {
    	            zpRemoteOutPutBuf += zRet;
    	            zSiz -= zRet;
    	        } else {
    	            if(LIBSSH2_ERROR_EAGAIN != zRet) { exit(1); }
    	        }
    	    } while(0 < zRet);
 
    	    /* this is due to blocking that would occur otherwise so we loop on
    	       this condition */ 
    	    if( zRet == LIBSSH2_ERROR_EAGAIN ) { zwait_socket(zSd, zSession); }
    	    else { break; }
    	}
	}

    zErrNo = -1;
	while(LIBSSH2_ERROR_EAGAIN == (zRet = libssh2_channel_close(zChannel))) { zwait_socket(zSd, zSession); } 
    if(0 == zRet) {
        zErrNo = libssh2_channel_get_exit_status(zChannel);
        libssh2_channel_get_exit_signal(zChannel, &zpExitSingal, NULL, NULL, NULL, NULL, NULL);
    }
 
	if (zpExitSingal) { zErrNo = -1; }
 
    libssh2_channel_free(zChannel);
    zChannel= NULL;
 
shutdown:
    libssh2_session_disconnect(zSession, "Bye");
    libssh2_session_free(zSession);
 
    close(zSd);
    libssh2_exit();
 
    return 0;
}
