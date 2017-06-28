#ifndef _Z
	#include "zmain.c"
#endif

#define zMaxEvents 64

#define UDP 0
#define TCP 1

/****************
 * 模块整体信息 *
 ****************/

/*
 * git_shadow充当TCP服务器角色，接收前端请求与后端主机信息反馈，尽可能使用缓存响应各类请求
 *
 * 对接规则：
 * 		执行动作代号(1byte)＋信息正文
 * 		[OpsMark(l/d/D/R)]+[struct zFileDiffInfo]
 *
 * 代号含义:
 *		 p:显示差异文件路径名称列表
 *		 P:显示单个文件内容的详细差异信息
 *		 d:布署某次commit的单个文件
 *		 D:布署某次commit的所有文件
 *		 l:打印最近十次布署日志
 *		 :打印所有历史布署日志
 *		 r:撤销某次提交的单个文件的更改，只来自前端
 *		 R:撤销某次提交的所有更改，只来自前端
 *		 c:状态确认，前端或后端主机均可返回
 */

// 列出差异文件路径名称列表
void
zlist_diff_files(_i zSd, zFileDiffInfo *zpIf){
	zsendmsg(zSd, zppCacheVecIf[zpIf->RepoId], zpCacheVecSiz[zpIf->RepoId], 0, NULL);  // 直接从缓存中提取
}

// 打印当前版本缓存与CURRENT标签之间的指定文件的内容差异
void
zprint_diff_contents(_i zSd, zFileDiffInfo *zpIf){
	if (zpIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVecIf[zpIf->RepoId]->iov_base))->CacheVersion) {
		zsendmsg(zSd, zpIf->p_DiffContent, zpIf->VecSiz, 0, NULL);  // 直接从缓存中提取
	}
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);  //  若缓存版本不一致，要求前端刷新页面
	}
}

// 列出最近zPreLoadLogSiz次或全部历史布署日志
void
zlist_log(_i zSd, _i zRepoId, _i zMarkAll) {
	_i zVecSiz;
	if ( 0 == zMarkAll ){	// 默认直接直接回复预存的最近zPreLoadLogSiz次记录
		zsendmsg(zSd, zppPreLoadLogVecIf[zRepoId], zpPreLoadLogVecSiz[zRepoId], 0, NULL);
	}
	else {  // 若前端请求列出所有历史记录，从日志文件中读取
		struct stat zStatBufIf;
		zDeployLogInfo *zpMetaLogIf, *zpTmpIf;
		zCheck_Negative_Exit(fstat(zpLogFd[0][zRepoId], &(zStatBufIf)));  // 获取日志属性
	
		zVecSiz = 2 * zStatBufIf.st_size / sizeof(zDeployLogInfo);  // 确定存储缓存区的大小
		struct iovec zVec[zVecSiz];
	
		zpMetaLogIf = (zDeployLogInfo *)mmap(NULL, zStatBufIf.st_size, PROT_READ, MAP_PRIVATE, zpLogFd[0][zRepoId], 0);  // 将meta日志mmap至内存
		zCheck_Null_Exit(zpMetaLogIf);
		madvise(zpMetaLogIf, zStatBufIf.st_size, MADV_WILLNEED);  // 提示内核大量预读
	
		zpTmpIf = zpMetaLogIf + zStatBufIf.st_size / sizeof(zDeployLogInfo) - 1;
		_ul zDataLogSiz = zpTmpIf->offset + zpTmpIf->len;  // 根据meta日志属性确认data日志偏移量
		char *zpDataLog = (char *)mmap(NULL, zDataLogSiz, PROT_READ, MAP_PRIVATE, zpLogFd[1][zRepoId], 0);  // 将data日志mmap至内存
		zCheck_Null_Exit(zpDataLog);
		madvise(zpDataLog, zDataLogSiz, MADV_WILLNEED);  // 提示内核大量预读
	
		for (_i i = 0; i < zVecSiz; i++) {  // 拼装日志信息
			if (0 == i % 2) {
				zVec[i].iov_base = zpMetaLogIf + i / 2;
				zVec[i].iov_len = sizeof(zDeployLogInfo);
			}
			else {
				zVec[i].iov_base = zpDataLog + (zpMetaLogIf + i / 2)->offset;
				zVec[i].iov_len = (zpMetaLogIf + i / 2)->len;
			}
		}
		zsendmsg(zSd, zVec, zVecSiz, 0, NULL);	// 发送结果
		munmap(zpMetaLogIf, zStatBufIf.st_size);  // 解除mmap
		munmap(zpDataLog, zDataLogSiz);
	}
}

void
zmilli_sleep(_i zMilliSec) {  // 毫秒级sleep
	static struct timespec zNanoSecIf = { .tv_sec = 0, };
	zNanoSecIf.tv_nsec  = zMilliSec * 1000000;
	nanosleep(&zNanoSecIf, NULL);
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId, char *zpPathName, _i zPathLen) {
	// write to .git_shadow/log/meta
	struct stat zStatBufIf;
	zCheck_Negative_Exit(fstat(zpLogFd[0][zRepoId], &zStatBufIf));  // 获取当前日志文件属性

	zDeployLogInfo zDeployIf;
	zCheck_Negative_Exit(pread(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo), zStatBufIf.st_size - sizeof(zDeployLogInfo)));  // 读出前一个记录的信息

	zDeployIf.RepoId = zRepoId;  // 代码库ID相同
	zDeployIf.index += 1;  // 布署索引偏移量增加1(即：顺序记录布署批次ID)，用于从sig日志文件中快整定位对应的commit签名
	zDeployIf.offset += zPathLen;  // data日志中对应的文件路径名称位置偏移量
	zDeployIf.TimeStamp = time(NULL);  // 日志时间戳(1900至今的秒数)
	zDeployIf.len = zPathLen;  // 本次布署的文件路径名称长度

	// 其本信息写入.git_shadow/log/meta
	if (sizeof(zDeployLogInfo) != write(zpLogFd[0][zRepoId], &zDeployIf, sizeof(zDeployLogInfo))) {
		zPrint_Err(0, NULL, "Can't write to log/meta!");
		exit(1);
	}
	// 将本次布署的文件路径名称写入.git_shadow/log/data尾部
	if (zPathLen != write(zpLogFd[1][zRepoId], zpPathName, zPathLen)) {
		zPrint_Err(0, NULL, "Can't write to log.data!");
		exit(1);
	}
	// 将本次布署之前的CURRENT标签的40位sig字符串追加写入.git_shadow/log/sig
	if ( 40 != write(zpLogFd[2][zRepoId], zppCurTagSig[zRepoId], 40)) {
		zPrint_Err(0, NULL, "Can't write to log.sig!");
		exit(1);
	}
}

// 执行布署
void
zdeploy(_i zSd, zFileDiffInfo *zpDiffIf, _i zMarkAll) {
	if (zpDiffIf->CacheVersion == ((zFileDiffInfo *)(zppCacheVecIf[zpDiffIf->RepoId]->iov_base))->CacheVersion) {  // 确认缓存版本是否一致
		char zShellBuf[4096];  // 存放SHELL命令字符串
		char *zpLogContents;   // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
		_i zLogSiz;
		if (1 == zMarkAll) { 
			sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -D -P %s", zppRepoPathList[zpDiffIf->RepoId]); 
			zpLogContents = "ALL";
			zLogSiz = 4 * sizeof(char);
		} 
		else { 
			sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -d -P %s %s", zppRepoPathList[zpDiffIf->RepoId], zpDiffIf->path); 
			zpLogContents = zpDiffIf->path;
			zLogSiz = zpDiffIf->PathLen;
		}

		pthread_mutex_lock(&(zpDeployLock[zpDiffIf->RepoId]));  // 加锁，布署没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
		system(zShellBuf);

		_ui zSendBuf[zpTotalHost[zpDiffIf->RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
		_i i;
		do {
			zmilli_sleep(2000);  // 每隔0.2秒向前端返回一次结果

			for (i = 0; i < zpTotalHost[zpDiffIf->RepoId]; i++) {  // 登记尚未确认状态的客户端ip列表
				if (0 == zppDpResList[zpDiffIf->RepoId][i].DeployState) {
					zSendBuf[i] = zppDpResList[zpDiffIf->RepoId][i].ClientAddr;
				}
			}

			zsendto(zSd, zSendBuf, i * sizeof(_ui), NULL);
		} while (zpReplyCnt[zpDiffIf->RepoId] < zpTotalHost[zpDiffIf->RepoId]);  // 等待所有client端确认状态：前端人工标记＋后端自动返回
		zpReplyCnt[zpDiffIf->RepoId] = 0;

		zwrite_log(zpDiffIf->RepoId, zpLogContents, zLogSiz);  // 将本次布署信息写入日志

		for (_i i = 0; i < zpTotalHost[zpDiffIf->RepoId]; i++) {
			zppDpResList[zpDiffIf->RepoId][i].DeployState = 0;  // 重置client状态，以便下次布署使用
		}

		pthread_mutex_unlock(&(zpDeployLock[zpDiffIf->RepoId]));  // 释放锁
	} 
	else {
		zsendto(zSd, "!", 2 * sizeof(char), NULL);  // 若缓存版本不一致，向前端发送“!”标识，要求刷新页面
	}
}

// 依据布署日志，撤销指定文件或整次提交
void
zrevoke_from_log(_i zSd, zDeployLogInfo *zpLogIf, _i zMarkAll){
	char zPathBuf[zpLogIf->len];  // 存放待撤销的目标文件路径
	char zCommitSigBuf[41];  // 存放40位的git commit签名
	zCommitSigBuf[40] = '\0';

	zCheck_Negative_Exit(pread(zpLogFd[1][zpLogIf->RepoId], &zPathBuf, zpLogIf->len, zpLogIf->offset));
	zCheck_Negative_Exit(pread(zpLogFd[2][zpLogIf->RepoId], &zCommitSigBuf, 40 * sizeof(char), 40 * sizeof(char) * zpLogIf->index));

	char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
	char *zpLogContents;  // 布署日志备注信息，默认是文件路径，若是整次提交，标记字符串"ALL"
	_i zLogSiz;
	if (1 == zMarkAll) { 
		sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -R -i %s -P %s", zCommitSigBuf, zppRepoPathList[zpLogIf->RepoId]); 
		zpLogContents = "ALL";
		zLogSiz = 4 * sizeof(char);
	} 
	else { 
		sprintf(zShellBuf, "~git/.git_shadow/scripts/zdeploy.sh -r -i %s -P %s %s", zCommitSigBuf, zppRepoPathList[zpLogIf->RepoId], zPathBuf); 
		zpLogContents = zPathBuf;
		zLogSiz = zpLogIf->len;
	}

	pthread_mutex_lock(&(zpDeployLock[zpLogIf->RepoId]));  // 撤销没有完成之前，阻塞相关请求，如：布署、撤销、更新缓存等
	system(zShellBuf);

	_ui zSendBuf[zpTotalHost[zpLogIf->RepoId]];  // 用于存放尚未返回结果(状态为0)的客户端ip列表
	_i i;
	do {
		zmilli_sleep(2000);  // 每0.2秒统计一次结果，并发往前端

		for (i = 0; i < zpTotalHost[zpLogIf->RepoId]; i++) {
			if (0 == zppDpResList[zpLogIf->RepoId][i].DeployState) {
				zSendBuf[i] = zppDpResList[zpLogIf->RepoId][i].ClientAddr;
			}
		}

		zsendto(zSd, zSendBuf, i * sizeof(_ui), NULL);  // 向前端发送当前未成功的列表
	} while (zpReplyCnt[zpLogIf->RepoId] < zpTotalHost[zpLogIf->RepoId]);  // 一直等待到所有client状态确认为止：前端人工确认＋后端自动确认
		zpReplyCnt[zpLogIf->RepoId] = 0;

	zwrite_log(zpLogIf->RepoId, zpLogContents, zLogSiz);  // 撤销完成，写入日志

	for (_i i = 0; i < zpTotalHost[zpLogIf->RepoId]; i++) {
		zppDpResList[zpLogIf->RepoId][i].DeployState = 0;  // 将本项目各主机状态重置为0
	}

	pthread_mutex_unlock(&(zpDeployLock[zpLogIf->RepoId]));
}

// 接收并更新对应的布署状态确认信息
void
zconfirm_deploy_state(zDeployResInfo *zpDpResIf) {
	zDeployResInfo *zpTmp = zpppDpResHash[zpDpResIf->RepoId][zpDpResIf->ClientAddr % zDeployHashSiz];  // HASH定位
	while (zpTmp != NULL) {  // 单点遍历
		if (zpTmp->ClientAddr == zpDpResIf->ClientAddr) {
			zpTmp->DeployState = 1;
			zpReplyCnt[((zDeployResInfo *)(zpDpResIf+ 1))->RepoId]++;
			return;
		}
		zpTmp = zpTmp->p_next;
	}
	zPrint_Err(0, NULL, "Unknown client reply!!!");
}

// 路由函数
void
zdo_serv(void *zpSd) {
	_i zSd = *((_i *)zpSd);

	char zReqBuf[zCommonBufSiz];
	zrecv_all(zSd, zReqBuf, zCommonBufSiz, NULL);  // 接收前端指令信息

	switch (zReqBuf[0]) {
		case 'p':  // list:列出内容有变动的所有文件路径
			zlist_diff_files(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'P':  // print:打印某个文件的详细变动内容
			zprint_diff_contents(zSd, (zFileDiffInfo *)(zReqBuf + 1));
			break;
		case 'd':  // deploy:布署单个文件
			zdeploy(zSd, (zFileDiffInfo *)(zReqBuf + 1), 0);
			break;
		case 'D':  // DEPLOY:布署当前提交所有文件
			zdeploy(zSd, (zFileDiffInfo *)(zReqBuf + 1), 1);
			break;
		case 'l':  // LIST:打印最近zPreLoadLogSiz次布署日志
			zlist_log(zSd, ((zDeployLogInfo *)(zReqBuf + 1))->RepoId, 0);
			break;
		case 'L':  // LIST:打印所有历史布署日志
			zlist_log(zSd, ((zDeployLogInfo *)(zReqBuf + 1))->RepoId, 1);
			break;
		case 'r':  // revoke:撤销单个文件的更改
			zrevoke_from_log(zSd, (zDeployLogInfo *)(zReqBuf + 1), 0);
			break;
		case 'R':  // REVOKE:撤销某次提交的全部更改
			zrevoke_from_log(zSd, (zDeployLogInfo *)(zReqBuf + 1), 1);
			break;
		case 'c':  // confirm:客户端回复的布署成功确认信息
			zconfirm_deploy_state((zDeployResInfo *)(zReqBuf + 1));
			break;
		default:
			zPrint_Err(0, NULL, "Undefined request");
	}
}

// 启动git_shadow服务器
void
zstart_server(char *zpHost, char *zpPort, _i zServType) {
	struct epoll_event zEv, zEvents[zMaxEvents];
	_i zMajorSd, zConnSd, zEvNum, zEpollSd;

	zMajorSd = zgenerate_serv_SD(zpHost, zpPort, zServType);  // 已经做完bind和listen

	zEpollSd = epoll_create1(0);
	zCheck_Negative_Exit(zEpollSd);

	zEv.events = EPOLLIN;
	zEv.data.fd = zMajorSd;
	zCheck_Negative_Exit(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv));

	for (;;) {
		zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
		zCheck_Negative_Exit(zEvNum);

		for (_i i = 0; i < zEvNum; i++) {
		   if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
			   zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
			   zCheck_Negative_Exit(zConnSd);

			   zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
			   zEv.data.fd = zConnSd;
			   zCheck_Negative_Exit(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv));
			}
			else {
				zAdd_To_Thread_Pool(zdo_serv, &(zEvents[i].data.fd));
			}
		}
	}
}

// 用于向git_shadow返回布署成功的信息
void
zclient_reply(char *zpHost, char *zpPort) {
	zDeployResInfo zDpResIf;
	_i zFd, zSd, zResLen;
	struct iovec zVec[2];
	char zActionMark = 'c';  // confirm:标识这是一条状态确认信息
	zVec[0].iov_base = &zActionMark;
	zVec[0].iov_len = sizeof(char);
	zVec[1].iov_base = &zDpResIf;
	zVec[1].iov_len = sizeof(zDeployResInfo);

	zFd = open(zMetaLogPath, O_RDONLY);
	zCheck_Negative_Exit(zFd);

	zDeployLogInfo zDpLogIf;
	zCheck_Negative_Exit(read(zFd, &zDpLogIf, sizeof(zDeployLogInfo)));
	zDpResIf.RepoId = zDpLogIf.RepoId;  // 标识版本库ID
	close(zFd);

	zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV);  // 以点分格式的ipv4地址连接服务端
	if (-1 == zSd) {
		zPrint_Err(0, NULL, "Connect to server failed.");
		exit(1);
	}

	zFd = open(zSelfIpPath, O_RDONLY);  // 读取本机的所有非回环ip地地，依次发送状态确认信息至服务端
	zCheck_Negative_Exit(zFd);

	_ui zIpv4Bin;
	while (0 != (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
		zCheck_Negative_Exit(zResLen);
		zDpResIf.ClientAddr = zIpv4Bin;  // 标识本机身份：ipv4地址
		if ((sizeof(char) + sizeof(zDeployLogInfo)) != zsendmsg(zSd, zVec, 2, 0, NULL)) {
			zPrint_Err(0, NULL, "Reply to server failed.");
			exit(1);
		}
	}

	close(zFd);
	shutdown(zSd, SHUT_RDWR);
}

#undef zMaxEvents
#undef UDP
#undef TCP
