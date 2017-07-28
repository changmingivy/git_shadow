#define _Z
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <pwd.h>

#include <pthread.h>
#include <sys/mman.h>

#include <sys/inotify.h>
#include <sys/epoll.h>

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
#define zLogCacheSiz 24  // 预缓存日志数量

#include "../../inc/zutils.h"
#include "../zbase_utils.c"

/****************
 * 数据结构定义 *
 ****************/
typedef void (* zThreadPoolOps) (void *);  // 线程池回调函数
//----------------------------------
typedef struct {
        _s RepoId;  // 每个代码库对应的索引
        _s RecursiveMark;  // 是否递归标志
        _i UpperWid;  // 存储顶层路径的watch id，每个子路径的信息中均保留此项
        char *zpRegexPattern;  // 符合此正则表达式的目录或文件将不被inotify监控
        zThreadPoolOps CallBack;  // 发生事件中对应的回调函数
        char path[];  // 被监控对象的绝对路径名称
} zObjInfo;
//----------------------------------
typedef struct {
        char hints[4];   // 用于填充提示类信息，如：提示从何处开始读取需要的数据
        _i RepoId;  // 索引每个代码库路径
        _ui FileIndex;  // 缓存中每个文件路径的索引
        _l CacheVersion;  // 文件差异列表及文件内容差异详情的缓存

        struct iovec *p_DiffContent;  // 指向具体的文件差异内容，按行存储
        _i VecSiz;  // 对应于文件差异内容的总行数

        _i PathLen;  // 文件路径长度，提供给前端使用
        char path[];  // 相对于代码库的路径
} zFileDiffInfo;

typedef struct {  // 布署日志信息的数据结构
        char hints[4];  // 用于填充提示类信息，如：提示从何处开始读取需要的数据
        _i RepoId;  // 标识所属的代码库
        _ui index;  // 标记是第几条记录(不是数据长度)

        _l TimeStamp;  // 时间戳，提供给前端使用
        _i PathLen;  // 所有文件的路径名称长度总和（包括换行符），提供给前端使用
        char path[];  // 相对于代码库的路径
} zDeployLogInfo;

typedef struct zDeployResInfo {
        char hints[4];  // 用于填充提示类信息，如：提示从何处开始读取需要的数据
        _ui ClientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
        _i RepoId;  // 所属代码库
        _i DeployState;  // 布署状态：已返回确认信息的置为1，否则保持为0
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

char **zppCURRENTsig;  // 每个代码库当前的CURRENT标签的SHA1 sig
_i *zpLogFd[2];  // 每个代码库的布署日志都需要三个日志文件：meta、data、sig，分别用于存储索引信息、路径名称、SHA1-sig

pthread_rwlock_t *zpRWLock;  // 每个代码库对应一把读写锁
pthread_rwlockattr_t zRWLockAttr;

struct  iovec **zppCacheVecIf;  // 用于提供代码差异数据的缓存功能，每个代码库对应一个缓存区
_i *zpCacheVecSiz;  // 对应于每个代码库的缓存区大小，即：缓存的对象数量

_i *zpTotalHost;  // 存储每个代码库后端的主机总数
_i *zpReplyCnt;  // 即时统计每个代码库已返回布署状态的主机总数，当其值与zpTotalHost相等时，即表达布署成功
zDeployResInfo ***zpppDpResHash, **zppDpResList;  // 每个代码库对应一个布署状态数据及与之配套的链式HASH

struct iovec **zppLogCacheVecIf;  // 以iovec形式缓存的每个代码库最近布署日志信息
struct iovec **zppSortedLogCacheVecIf;  // 按时间戳降序排列后的结果，这是向前端发送的最终结果
_i *zpLogCacheVecSiz;
_i *zpLogCacheQueueHeadIndex;

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
#define zRepoIdPath ".git_shadow/info/repo_id"

#define zMetaLogPath ".git_shadow/log/deploy/meta"  // 元数据日志，以zDeployLogInfo格式存储，主要包含data、sig两个日志文件中数据的索引
//#define zDataLogPath ".git_shadow/log/deploy/data"  // 文件路径日志，需要通过meta日志提供的索引访问
#define zSigLogPath ".git_shadow/log/deploy/sig"  // 40位SHA1 sig字符串，需要通过meta日志提供的索引访问
// 启动git_shadow服务器
void
zstart_server(void) {
#define zMaxEvents 64
        // 如下部分定义服务接口

        char zBuf[4096];

        // 如下部分配置 epoll 环境
        struct epoll_event zEv, zEvents[zMaxEvents];
        _i zMajorSd, zConnSd, zEvNum, zEpollSd;

        zMajorSd = zgenerate_serv_SD("10.30.2.126", "30000", 1);  // 返回的 socket 已经做完 bind 和 listen

        zEpollSd = epoll_create1(0);
        zCheck_Negative_Return(zEpollSd,);

        zEv.events = EPOLLIN;
        zEv.data.fd = zMajorSd;
        zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv),);

        _i zCnt = 0;
        // 如下部分启动 epoll 监听服务
        for (;;) {
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return(zEvNum,);

        for (_i i = 0; i < zEvNum; i++, zCnt =0) {
        printf("Recv sd: %d\n", zEvents[i].data.fd);
           if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
                   zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0);
                   zCheck_Negative_Return(zConnSd,);

                   zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
                   zEv.data.fd = zConnSd;
                   zCheck_Negative_Return(epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv),);
                } else {
                   zCnt = recv(zEvents[i].data.fd, zBuf, 4096, 0);
                   if (0 == zCnt) { continue; }
                   printf("RECV:%d:%s, EVNUM: %d\n", zCnt, zBuf, zEvNum);
                   zsendto(zEvents[i].data.fd, zBuf, zCnt, 0, NULL);
                   shutdown(zEvents[i].data.fd, SHUT_RDWR);
                }
           memset(zBuf, 0, 4096);
        }
        }
#undef zMaxEvents
#undef zGetCmdId
}

int
main(void) {
        struct sigaction zSigActionIf;
        zSigActionIf.sa_handler = SIG_IGN;
        sigfillset(&zSigActionIf.sa_mask);
        zSigActionIf.sa_flags = 0;
        sigaction(SIGPIPE, &zSigActionIf, NULL);

        zstart_server();
        return 0;
}
