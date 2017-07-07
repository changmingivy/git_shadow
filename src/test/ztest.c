#define _Z
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <pthread.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <sys/inotify.h>
#include <sys/epoll.h>

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
#include <ctype.h>

#define zCommonBufSiz 4096
#define zMaxRepoNum 1024
#define zWatchHashSiz 8192  // 最多可监控的路径总数
#define zDeployHashSiz 1024  // 布署状态HASH的大小
#define zPreLoadLogSiz 64  // 预缓存日志数量

#include "../../inc/zutils.h"
#include "../zbase_utils.c"
#include "../pcre2/zpcre.c"

#define zTypeConvert(zSrcObj, zTypeTo) ((zTypeTo)(zSrcObj))

/****************
 * 数据结构定义 *
 ****************/
typedef void (* zThreadPoolOps) (void *);  // 线程池回调函数
//----------------------------------
typedef struct {
    _us RepoId;  // 每个代码库对应的索引
    _us RecursiveMark;  // 是否递归标志
    _i UpperWid;  // 存储顶层路径的watch id，每个子路径的信息中均保留此项
    char *zpRegexPattern;  // 符合此正则表达式的目录或文件将不被inotify监控
    zThreadPoolOps CallBack;  // 发生事件中对应的回调函数
    char path[];  // 被监控对象的绝对路径名称
} zObjInfo;
//----------------------------------
typedef struct {
    char zHint[4];   // 用于块充提示类信息，如：提示从何处开始读取需要的数据
    _ui RepoId;  // 索引每个代码库路径
    _ui FileIndex;  // 缓存中每个文件路径的索引
    _l CacheVersion;  // 文件差异列表及文件内容差异详情的缓存

    struct iovec *p_DiffContent;  // 指向具体的文件差异内容，按行存储
    _i VecSiz;  // 对应于文件差异内容的总行数

    _i PathLen;  // 文件路径长度，提供给前端使用
    char path[];  // 相对于代码库的路径
} zFileDiffInfo;

typedef struct {  // 布署日志信息的数据结构
    char zHint[4];  // 用于块充提示类信息，如：提示从何处开始读取需要的数据
    _ui RepoId;  // 标识所属的代码库
    _ui index;  // 索引每条记录在日志文件中的位置
    _l offset;  // 指明在data日志文件中的SEEK偏移量

    _l TimeStamp;  // 时间戳，提供给前端使用
    _i PathLen;  // 路径名称长度，可能为“ALL”，代表整体部署某次commit的全部文件，提供给前端使用
    char path[];  // 相对于代码库的路径
} zDeployLogInfo;

typedef struct zDeployResInfo {
    char zHint[4];  // 用于块充提示类信息，如：提示从何处开始读取需要的数据
    _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _us RepoId;  // 所属代码库
    _us DeployState;  // 布署状态：已返回确认信息的置为1，否则保持为0
    struct zDeployResInfo *p_next;
} zDeployResInfo;

typedef struct zNetServInfo {
    char *p_host;  // 字符串形式的ipv4点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
} zNetServInfo;

/************
 * 全局变量 *
 ************/
_i zRepoNum;  // 总共有多少个代码库
char **zppRepoPathList;  // 每个代码库的绝对路径

_i zInotifyFD;   // inotify 主描述符
zObjInfo *zpObjHash[zWatchHashSiz];  // 以watch id建立的HASH索引

zThreadPoolOps zCallBackList[16];  // 索引每个回调函数指针，对应于zObjInfo中的CallBackId

char **zppCurTagSig;  // 每个代码库当前的CURRENT标签的SHA1 sig
_i *zpLogFd[3];  // 每个代码库的布署日志都需要三个日志文件：meta、data、sig，分别用于存储索引信息、路径名称、SHA1-sig

pthread_rwlock_t *zpRWLock;  // 每个代码库对应一把读写锁
pthread_rwlockattr_t zRWLockAttr;

struct  iovec **zppCacheVecIf;  // 用于提供代码差异数据的缓存功能，每个代码库对应一个缓存区
_i *zpCacheVecSiz;  // 对应于每个代码库的缓存区大小，即：缓存的对象数量

_i *zpTotalHost;  // 存储每个代码库后端的主机总数
_i *zpReplyCnt;  // 即时统计每个代码库已返回布署状态的主机总数，当其值与zpTotalHost相等时，即表达布署成功
zDeployResInfo ***zpppDpResHash, **zppDpResList;  // 每个代码库对应一个布署状态数据及与之配套的链式HASH

struct iovec **zppPreLoadLogVecIf;  // 以iovec形式缓存的每个代码库最近布署日志信息
_i *zpPreLoadLogVecSiz;

#define UDP 0
#define TCP 1

/************
 * 配置文件 *
 ************/
// 以下路径均是相对于所属代码库的顶级路径
#define zAllIpPath ".git_shadow/info/client_ip_all.bin"  // 位于各自代码库路径下，以二进制形式存储后端所有主机的ipv4地址
#define zSelfIpPath ".git_shadow/info/client_ip_self.bin"  // 格式同上，存储客户端自身的ipv4地址
#define zAllIpPathTxt ".git_shadow/info/client_ip_all.txt"  // 存储点分格式的原始字符串ipv4地下信息，如：10.10.10.10
#define zMajorIpPathTxt ".git_shadow/info/client_ip_major.txt"  // 与布署中控机直接对接的master机的ipv4地址（点分格式），目前是zdeploy.sh使用，后续版本使用libgit2库之后，将转为内部直接使用

#define zMetaLogPath ".git_shadow/log/deploy/meta"  // 元数据日志，以zDeployLogInfo格式存储，主要包含data、sig两个日志文件中数据的索引
#define zDataLogPath ".git_shadow/log/deploy/data"  // 文件路径日志，需要通过meta日志提供的索引访问
#define zSigLogPath ".git_shadow/log/deploy/sig"  // 40位SHA1 sig字符串，需要通过meta日志提供的索引访问

/**********
 * 子模块 *
 **********/
#include "../md5_sig/zgenerate_sig_md5.c"  // 生成MD5 checksum检验和
#include "../thread_pool/zthread_pool.c"
#include "../inotify/zinotify_callback.c"
#include "../inotify/zinotify.c"  // 监控代码库文件变动
#include "../zinit.c"  // 读取主配置文件
#include "../net/znetwork.c"  // 对外提供网络服务

// 此函数仅用作测试
void
ztest_print(void) {
    printf("RepoNum: %d\n", zRepoNum);
    for (_i i = 0; i < zRepoNum; i++) {
        printf("RepoPath: %s\n", zppRepoPathList[i]);
    }

    printf("zInotifyFD: %d\n", zInotifyFD);
    for (_i i = 0; i < zWatchHashSiz; i++) {
        if (NULL != zpObjHash[i]) {
            printf("OBJPath: %s\n", zpObjHash[i]->path);
            printf("zpRegexPattern: %s\n", zpObjHash[i]->zpRegexPattern);
            printf("UpperWid: %d\n", zpObjHash[i]->UpperWid);
            printf("RecursiveMark: %d\n", zpObjHash[i]->RecursiveMark);
            printf("CallBack: %p\n", zpObjHash[i]->CallBack);
        }
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("zppCurTagSig: ");
        for (_i z = 0; z < 40; z++) {
            printf("%c", zppCurTagSig[i][z]);
        }
        printf("\n");
        printf("CacheVecSiz: %d\n", zpCacheVecSiz[i]);
        printf("zPreLoadLogSiz: %d\n", zpPreLoadLogVecSiz[i]);

        for (_i j = 0; j < zpCacheVecSiz[i]; j++) {
            printf("CacheDiffFilePath: %s, CacheDiffFilePathLen: %zd\n", ((zFileDiffInfo *)zppCacheVecIf[i][j].iov_base)->path, zppCacheVecIf[i][j].iov_len);
        }

        for (_i j = 0; j < zpPreLoadLogVecSiz[i]; j++) {
            printf("PreloadLog: %s, PreloadLogLen: %zd\n", zppPreLoadLogVecIf[i][j].iov_base,zppPreLoadLogVecIf[i][j].iov_len);
        }
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("LogFd-meta: %d\n", zpLogFd[0][i]);
        printf("LogFd-data: %d\n", zpLogFd[1][i]);
        printf("LogFd-sig: %d\n", zpLogFd[2][i]);
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("Totalhost: %d\n", zpTotalHost[i]);
        printf("zpReplyCnt: %d\n", zpReplyCnt[i]);
        for (_i j = 0; j < zpTotalHost[i]; j++) {
            printf("ClientAddr: %d\n", zppDpResList[i][j].ClientAddr);
            printf("RepoId: %d\n", zppDpResList[i][j].RepoId);
            printf("DeployState: %d\n", zppDpResList[i][j].DeployState);
            printf("next: %p\n", zppDpResList[i][j].p_next);
        }

        for (_i k = 0; k < zDeployHashSiz; k++) {
            if (NULL == zpppDpResHash[i][k]) {continue;}
            else {
                do {
                    printf("HashClientAddr: %d\n", zpppDpResHash[i][k]->ClientAddr);
                    printf("hashRepoId: %d\n", zpppDpResHash[i][k]->RepoId);
                    printf("hashDeployState: %d\n", zpppDpResHash[i][k]->DeployState);
                    printf("hashnext: %p\n", zpppDpResHash[i][k]->p_next);

                    zpppDpResHash[i][k] = zpppDpResHash[i][k]->p_next;
                } while(NULL != zpppDpResHash[i][k]);
            }
        }
    }
}

void
zclient(char *zpX) {
	char zBuf[4096];
    _i zSd = ztcp_connect("127.0.0.1", "20000", AI_NUMERICHOST | AI_NUMERICSERV);  // 以点分格式的ipv4地址连接服务端
    if (-1 == zSd) {
        zPrint_Err(0, NULL, "Connect to server failed.");
        exit(1);
    }

	zCheck_Negative_Return(
			zsendto(zSd, zpX, strlen(zpX), 0, NULL),
			);

	zCheck_Negative_Return(
			zrecv_all(zSd, zBuf, 4096, 0, NULL),
			);

	printf("%s\n", zBuf);
    shutdown(zSd, SHUT_RDWR);
}


_i
main(_i zArgc, char **zppArgv) {
	printf("%d, %s\n", zArgc, zppArgv[1]);
	zclient(zppArgv[1]);
}
