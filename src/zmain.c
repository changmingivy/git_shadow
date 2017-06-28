#define _Z
#define _XOPEN_SOURCE 700
//#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <sys/inotify.h>
#include <sys/epoll.h>

#include <pthread.h>
#include <sys/mman.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>

#define zCommonBufSiz 4096 

#include "zutils.h"
#include "zbase_utils.c"
#include "zpcre.c"

/****************
 * 数据结构定义 *
 ****************/
typedef void (* zThreadPoolOps) (void *);  // 线程池回调函数
//----------------------------------
typedef struct zObjInfo {  // 用于存储从配置文件中读到的内容
	struct zObjInfo *p_next;
	char *zpRegexStr;  // 符合此正则表达式的目录或文件将不被inotify监控
	_s CallBackId;  // 回调函数索引，需要main函数中手动维护
	_s RecursiveMark;  // 是否递归标志
	char path[];  // 需要inotify去监控的路径名称
}zObjInfo;

typedef struct zSubObjInfo {
	zPCREInitInfo *zpRegexIf;  // 继承自 zObjInfo
	_i RepoId;  // 每个代码库对应的索引
	_i UpperWid;  // 存储顶层路径的watch id，每个子路径的信息中均保留此项
	_s RecursiveMark;  // 继承自 zObjInfo
	_s EvType;  // 自定义的inotify 事件类型，作为脚本变量提供给外部的SHELL脚本
	zThreadPoolOps CallBack;  // 发生事件中对应的回调函数
	char path[];  // 继承自 zObjInfo 
}zSubObjInfo;
//----------------------------------
typedef struct zFileDiffInfo {
	_ui CacheVersion;  // 文件差异列表及文件内容差异详情的缓存
	_us RepoId;  // 索引每个代码库路径
	_us FileIndex;  // 缓存中每个文件路径的索引

	struct iovec *p_DiffContent;  // 指向具体的文件差异内容，按行存储
	_ui VecSiz;  // 对应于文件差异内容的总行数

	_ui PathLen;  // 文件路径长度，提代给前端使用
	char path[];  // 相对于代码库的路径
} zFileDiffInfo;

typedef struct zDeployLogInfo {  // 布署日志信息的数据结构
	_ui index;  // 索引每条记录在日志文件中的位置
	_us RepoId;  // 标识所属的代码库
	_us len;  // 路径名称长度，可能为“ALL”，代表整体部署某次commit的全部文件
	_ul offset;  // 指明在data日志文件中的SEEK偏移量
	_ul TimeStamp;  // 时间戳
	char path[];  // 相对于代码库的路径
} zDeployLogInfo;

typedef struct zDeployResInfo {
	_ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
	_us RepoId;  // 所属代码库
	_us DeployState;  // 布署状态：已返回确认信息的置为1，否则保持为0
	struct zDeployResInfo *p_next;
} zDeployResInfo;

/************
 * 全局变量 *
 ************/
#define zWatchHashSiz 8192  // 最多可监控的目标总数
_i zInotifyFD;   // inotify 主描述符
zSubObjInfo *zpPathHash[zWatchHashSiz];  // 以watch id建立的HASH索引

zThreadPoolOps zCallBackList[16] = {NULL};  // 索引每个回调函数指针，对应于zObjInfo中的CallBackId

#define zDeployHashSiz 1024  // 布署状态HASH的大小
_i zRepoNum;  // 总共有多少个代码库
char **zppRepoPathList;  // 每个代码库的绝对路径

char **zppCurTagSig;  // 每个代码库当前的CURRENT标签的SHA1 sig
_i *zpLogFd[3];  // 每个代码库的布署日志都需要三个日志文件：meta、data、sig，分别用于存储索引信息、路径名称、SHA1-sig

pthread_rwlock_t *zpRWLock;  // 每个代码库对应一把读写锁
pthread_rwlockattr_t zRWLockAttr;

_i *zpTotalHost;  // 存储每个代码库后端的主机总数
_i *zpReplyCnt;  // 即时统计每个代码库已返回布署状态的主机总数，当其值与zpTotalHost相等时，即表达布署成功

zDeployResInfo ***zpppDpResHash, **zppDpResList;  // 每个代码库对应一个布署状态数据及与之配套的链式HASH

/* 以下全局变量用于提供缓存功能 */
struct  iovec **zppCacheVecIf;  // 每个代码库对应一个缓存区
_i *zpCacheVecSiz;  // 对应于每个代码库的缓存区大小，即：缓存的对象数量

#define zPreLoadLogSiz 16
struct iovec **zppPreLoadLogVecIf;  // 以iovec形式缓存的最近布署日志信息
_ui *zpPreLoadLogVecSiz;

/************
 * 配置文件 *
 ************/
// 以下路径均是相对于所属代码库的顶级路径
#define zAllIpPath ".git_shadow/info/client/host_ip_all.bin"  // 位于各自代码库路径下，以二进制形式存储后端所有主机的ipv4地址
#define zSelfIpPath ".git_shadow/info/client/host_ip_me.bin"  // 整式同上，存储自身的ipv4地址
#define zAllIpPathTxt ".git_shadow/info/client/host_ip_all.txt"  // 存储点分格式的原始字符串ipv4地下信息，如：10.10.10.10
#define zMajorIpPathTxt ".git_shadow/info/client/host_ip_major.txt"  // 与布署中控机直接对接的master机的ipv4地址（点分格式）

#define zMetaLogPath ".git_shadow/log/deploy/meta"  // 元数据日志，以zDeployLogInfo格式存储，主要包含data、sig两个日志文件中数据的索引
#define zDataLogPath ".git_shadow/log/deploy/data"  // 文件路径日志，需要通过meta日志提供的索引访问
#define zSigLogPath ".git_shadow/log/deploy/sig"  // 40位SHA1 sig字符串，需要通过meta日志提供的索引访问

/**********
 * 子模块 *
 **********/
#include "zthread_pool.c"
#include "zinotify_callback.c"
#include "zinotify.c"  // 监控代码库文件变动
#include "znetwork.c"  // 对外提供网络服务

/*************************
 * DEAL WITH CONFIG FILE *
 *************************/
// 读取主配置文件
zObjInfo *
zread_conf_file(const char *zpConfPath) {
//TEST: PASS
	zObjInfo *zpObjIf[3] = {NULL};

	zPCREInitInfo *zpInitIf[6] = {NULL};
	zPCRERetInfo *zpRetIf[6] = {NULL};

	_i zCnt = 0;
	char *zpRes = NULL;
	FILE *zpFile = fopen(zpConfPath, "r");

	struct stat zStatBufIf;

	zpInitIf[0] = zpcre_init("^\\s*\\d\\s+\\d\\s+/[/\\w]+");
	zpInitIf[1] = zpcre_init("^\\d");  // 匹配递归标识符
	zpInitIf[4] = zpcre_init("\\d+(?=\\s+/)");   // 匹配回调函数索引
	zpInitIf[5] = zpcre_init("\\w+(?=\\s+($|#))");   // 匹配用于忽略文件路径的正则表达式字符串
	zpInitIf[2] = zpcre_init("[/\\w]+(?=\\s*$)");  // 匹配要监控的目标的路径
	zpInitIf[3] = zpcre_init("^\\s*($|#)");  // 匹配空白行

	for (_i i = 1; NULL != (zpRes = zget_one_line_from_FILE(zpFile)); i++) {
		zpRes[strlen(zpRes) - 1] = '\0';  // 清除行末的换行符 '\n'

		zpRetIf[3] = zpcre_match(zpInitIf[3], zpRes, 0);  // 若是空白行，跳过
		if (0 < zpRetIf[3]->cnt) {
			zpcre_free_tmpsource(zpRetIf[3]);
			continue;
		}
		zpcre_free_tmpsource(zpRetIf[3]);

		zpRetIf[0] = zpcre_match(zpInitIf[0], zpRes, 0);
		if (0 == zpRetIf[0]->cnt) {
			zpcre_free_tmpsource(zpRetIf[0]);
			zPrint_Time();
			fprintf(stderr, "\033[31m[Line %d] \"%s\": Invalid entry.\033[00m\n", i ,zpRes);  // 若不符合整体格式，跳过
			continue;
		} else {
			zpRetIf[2] = zpcre_match(zpInitIf[2], zpRetIf[0]->p_rets[0], 0);
			if (-1 == lstat(zpRetIf[2]->p_rets[0], &zStatBufIf) 
					|| !S_ISDIR(zStatBufIf.st_mode)) {
				zpcre_free_tmpsource(zpRetIf[2]);
				zpcre_free_tmpsource(zpRetIf[0]);
				zPrint_Time();
				fprintf(stderr, "\033[31m[Line %d] \"%s\": NO such directory or NOT a directory.\033[00m\n", i, zpRes);
				continue;  // 若指定的目标路径不存在，跳过
			}
			zpRetIf[1] = zpcre_match(zpInitIf[1], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[4] = zpcre_match(zpInitIf[4], zpRetIf[0]->p_rets[0], 0);
			zpRetIf[5] = zpcre_match(zpInitIf[5], zpRetIf[0]->p_rets[0], 0);

			zpObjIf[0] = malloc(sizeof(zObjInfo) + 1 + strlen(zpRetIf[2]->p_rets[0]));
			if (0 == zCnt) {
				zCnt++;
				zpObjIf[2] = zpObjIf[1] = zpObjIf[0];
			}
			zpObjIf[1]->p_next = zpObjIf[0];
			zpObjIf[1] = zpObjIf[0];
			zpObjIf[0]->p_next = NULL;  // 必须在此位置设置NULL

			zpObjIf[0]->RecursiveMark = atoi(zpRetIf[1]->p_rets[0]);
			zpObjIf[0]->CallBackId = atoi(zpRetIf[4]->p_rets[0]);
			strcpy(zpObjIf[0]->path, zpRetIf[2]->p_rets[0]);
			if (0 == zpRetIf[5]->cnt) {
				zpObjIf[0]->zpRegexStr = "^[.]{1,2}$";
			} else {
				zMem_Alloc(zpObjIf[0]->zpRegexStr,char, 1 + strlen(zpRetIf[5]->p_rets[0]));
				strcpy(zpObjIf[0]->zpRegexStr, zpRetIf[5]->p_rets[0]);
			}

			zpcre_free_tmpsource(zpRetIf[5]);
			zpcre_free_tmpsource(zpRetIf[4]);
			zpcre_free_tmpsource(zpRetIf[2]);
			zpcre_free_tmpsource(zpRetIf[1]);
			zpcre_free_tmpsource(zpRetIf[0]);

			zpObjIf[0] = zpObjIf[0]->p_next;
		}
	}

	zpcre_free_metasource(zpInitIf[5]);
	zpcre_free_metasource(zpInitIf[4]);
	zpcre_free_metasource(zpInitIf[3]);
	zpcre_free_metasource(zpInitIf[2]);
	zpcre_free_metasource(zpInitIf[1]);
	zpcre_free_metasource(zpInitIf[0]);

	fclose(zpFile);
	return zpObjIf[2];  // 返回头指针
}

// 监控主配置文件的变动
void
zconfig_file_monitor(const char *zpConfPath) {
//TEST: PASS
	_i zConfFD = inotify_init();
	zCheck_Negative_Exit(
			inotify_add_watch(
				zConfFD,
				zpConfPath, 
				IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF
				)
			); 

	char zBuf[zCommonBufSiz] 
		__attribute__ ((aligned(__alignof__(struct inotify_event))));
	ssize_t zLen;

	const struct inotify_event *zpEv;
	char *zpOffset;

	for (;;) {
		zLen = read(zConfFD, zBuf, sizeof(zBuf));
		zCheck_Negative_Exit(zLen);

		for (zpOffset = zBuf; zpOffset < zBuf + zLen; zpOffset += sizeof(struct inotify_event) + zpEv->len) {
			zpEv = (const struct inotify_event *)zpOffset;
			if (zpEv->mask & (IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED)) { return; }
		}
	}
}

/********
 * MAIN *
 ********/
_i
main(_i zArgc, char **zppArgv) {
	_i zFd[2] = {0}, zActionType = 0, zErr = 0;

	char *zpHost, *zpPort;  // 指定服务端自身的Ipv4地址与端口，或者客户端要连接的目标服务器的Ipv4地址与端口
	struct stat zStatIf;

	for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "SCh:p:f:"));) {
		switch (zOpt) {
		case 'S':  // 启动服务端功能
			zActionType = 1; break;
		case 'C':  // 启动客户端功能
			zActionType = 2; break;
		case 'h':
			zpHost = optarg; break;
		case 'p':
			zpPort = optarg; break;
		case 'f':
			if (-1 == stat(optarg, &zStatIf) || !S_ISREG(zStatIf.st_mode)) {  // 若指定的主配置文件不存在或不是普通文件，则报错退出
				zPrint_Time();
				fprintf(stderr, "\033[31;01mConfig file not exists or is not a regular file!\n"
						"Usage: %s -f <Config File Path>\033[00m\n", zppArgv[0]);
				exit(1);
			}
			break;
		default: // zOpt == '?'  // 若指定了无效的选项，报错退出
			zPrint_Time();
		 	fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
			exit(1);
		}
	}

	if (optind >= zArgc) {  // 以上读完之后，剩余的命令行参数用于指定各个代码库的绝地路径；若没有指定代码库路径，则报错退出
		zPrint_Time();
		zPrint_Err(0, NULL, "\033[31;01mNeed at least one argument to special CODE REPO path!\033[00m");
		exit(1);
	}

	zErr = pthread_rwlockattr_setkind_np(&zRWLockAttr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP); // 设置读写锁属性为写优先，如：正在更新缓存、正在布署过程中、正在撤销过程中等，会阻塞查询请求
	if (0 > zErr) {
		zPrint_Err(zErr, NULL, "rwlock set attr failed!");
		exit(1);
	}

	// 代码库总数量
	zRepoNum = 1 + zArgc - optind;

	// 每个代码库近期布署日志信息的缓存
	zMem_Alloc(zppPreLoadLogVecIf, struct iovec *, zRepoNum);
	zMem_Alloc(zpPreLoadLogVecSiz, _ui, zRepoNum);

	// 保存各个代码库的CURRENT标签所对应的SHA1 sig
	zMem_Alloc(zppCurTagSig, char *, zRepoNum);
	// 缓存'git diff'文件路径列表及每个文件内容变动的信息，与每个代码库一一对应
	zMem_Alloc(zppCacheVecIf, struct iovec *, zRepoNum);
	zMem_Alloc(zpCacheVecSiz, _i, zRepoNum);
	// 每个代码库对应meta、data、sig三个日志文件
	zMem_Alloc(zpLogFd[0], _i, zRepoNum);
	zMem_Alloc(zpLogFd[1], _i, zRepoNum);
	zMem_Alloc(zpLogFd[2], _i, zRepoNum);
	// 存储每个代码库对应的主机总数
	zMem_Alloc(zpTotalHost, _i, zRepoNum );
	// 即时存储已返回布署成功信息的主机总数
	zMem_Alloc(zpReplyCnt, _i, zRepoNum );
	// 索引每个代码库绝对路径名称
	zMem_Alloc(zppRepoPathList, char *, zRepoNum);
	// 索引每个代码库的读写锁
	zMem_Alloc(zpRWLock, pthread_rwlock_t, zRepoNum);

	// 每个代码库对应一个线性数组，用于接收每个ECS返回的确认信息
	// 同时基于这个线性数组建立一个HASH索引，以提高写入时的定位速度
	zMem_Alloc(zppDpResList, zDeployResInfo *, zRepoNum);
	zMem_Alloc(zpppDpResHash, zDeployResInfo **, zRepoNum);

	// +++___+++ 需要手动维护每个回调函数的索引 +++___+++
	zCallBackList[0] = zupdate_cache;
	zCallBackList[1] = zupdate_ipv4_db;

	zDeployResInfo *zpTmp = NULL;
	for (_i i = 0; i < zRepoNum; i++) { 
		zppRepoPathList[i] = zppArgv[optind];

		// 打开代码库顶层目录，生成目录fd供接下来的openat使用
		zFd[0] = open(zppArgv[optind], O_RDONLY);
		zCheck_Negative_Exit(zFd[0]);

		// 打开meta日志文件
		zpLogFd[0][i] = openat(zFd[0], zMetaLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
		zCheck_Negative_Exit(zpLogFd[0][i]);
		// 打开data日志文件
		zpLogFd[1][i] = openat(zFd[0], zDataLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
		zCheck_Negative_Exit(zpLogFd[1][i]);
		// 打开sig日志文件
		zpLogFd[2][i] = openat(zFd[0], zSigLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
		zCheck_Negative_Exit(zpLogFd[1][i]);

		// 为每个代码库生成一把读写锁，锁属性设置写者优先
		if (0 != (zErr =pthread_rwlock_init(&(zpRWLock[i]), &zRWLockAttr))) {
			zPrint_Err(zErr, NULL, "Init deploy lock failed!");
			exit(1);
		}

		// 读取所在代码库的所有主机ip地址
		zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY);
		zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));

		close(zFd[0]);  // zFd[0] 用完关闭

		zpTotalHost[i] = zStatIf.st_size / sizeof(_ui);  // 主机总数
		zMem_Alloc(zppDpResList[i], zDeployResInfo, zpTotalHost[i]);  // 分配数组空间，用于顺序读取
		zMem_C_Alloc(zpppDpResHash[i], zDeployResInfo *, zDeployHashSiz);  // 对应的 HASH 索引,用于快速定位写入
		for (_i j = 0; j < zpTotalHost[i]; j++) {
			zppDpResList[i][j].RepoId = i;  // 写入代码库索引值
			zppDpResList[i][j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）

			errno = 0;
			if (sizeof(_ui) != read(zFd[1], &(zppDpResList[i][j].ClientAddr), sizeof(_ui))) { // 读入二进制格式的ipv4地址
				zPrint_Err(errno, NULL, "read client info failed!");
				exit(1);
			}

			zpTmp = zpppDpResHash[i][j % zDeployHashSiz];  // HASH 定位
			if (NULL == zpTmp) {
				zpTmp->p_next = NULL;
				zpppDpResHash[i][j % zDeployHashSiz] = &(zppDpResList[i][j]);  // 若顶层为空，直接指向数组中对应的位置
			} 
			else {
				while (NULL != zpTmp->p_next) { zpTmp = zpTmp->p_next; }  // 若顶层不为空，分配一个新的链表节点指向数据中对应的位置
				zMem_Alloc(zpTmp->p_next, zDeployResInfo, 1);
				zpTmp->p_next->p_next = NULL;
				zpTmp->p_next = &(zppDpResList[i][j]);
			}
		}

		close(zFd[1]);  // zFd[1] 用完关闭
		optind++;  // 参数指针后移
	}

	zdaemonize("/");  // 转换自身为守护进程，解除与终端的关联关系

zReLoad:;
	zInotifyFD = inotify_init();  // 生成inotify master fd
	zCheck_Negative_Exit(zInotifyFD);

	zthread_poll_init();  // 初始化线程池

	zObjInfo *zpObjIf = NULL;
	if (NULL == (zpObjIf = zread_conf_file(zppArgv[2]))) {  // 从主配置文件中读取内容存入链表
		zPrint_Time();
		fprintf(stderr, "\033[31;01mNo valid entry found in config file!!!\n\033[00m\n");
	}
	else {
		do {
			zAdd_To_Thread_Pool(zinotify_add_top_watch, zpObjIf);  // 读到的有效条目，添加到inotify中进行监控
			zpObjIf = zpObjIf->p_next;
		} while (NULL != zpObjIf);
	}

	zAdd_To_Thread_Pool(zinotify_wait, NULL);  // 主线程等待事件发生 
	zconfig_file_monitor(zppArgv[2]);  // 监控自身主配置文件的内容变动

	close(zInotifyFD);  // 主配置文件有变动后，关闭inotify master fd

	pid_t zPid = fork(); // 之后父进程退出，子进程按新的主配置文件内容重新初始化
	zCheck_Negative_Exit(zPid);
	if (0 == zPid) { goto zReLoad; }
	else { exit(0); }
}

