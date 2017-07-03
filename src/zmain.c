#define _Z
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
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

#include "../inc/zutils.h"
#include "ops/zbase_utils.c"
#include "pcre2/zpcre.c"

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
    _i CacheVersion;  // 文件差异列表及文件内容差异详情的缓存
    _i RepoId;  // 索引每个代码库路径
    _i FileIndex;  // 缓存中每个文件路径的索引

    struct iovec *p_DiffContent;  // 指向具体的文件差异内容，按行存储
    _i VecSiz;  // 对应于文件差异内容的总行数

    _i PathLen;  // 文件路径长度，提代给前端使用
    char path[];  // 相对于代码库的路径
} zFileDiffInfo;

typedef struct {  // 布署日志信息的数据结构
    _i index;  // 索引每条记录在日志文件中的位置
    _i RepoId;  // 标识所属的代码库
    _l offset;  // 指明在data日志文件中的SEEK偏移量
    _l TimeStamp;  // 时间戳
    _i PathLen;  // 路径名称长度，可能为“ALL”，代表整体部署某次commit的全部文件
    char path[];  // 相对于代码库的路径
} zDeployLogInfo;

typedef struct zDeployResInfo {
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
#define zMaxRepoNum 1024
_i zRepoNum;  // 总共有多少个代码库
char **zppRepoPathList;  // 每个代码库的绝对路径

#define zWatchHashSiz 8192  // 最多可监控的目标总数
_i zInotifyFD;   // inotify 主描述符
zObjInfo *zpObjHash[zWatchHashSiz] = {NULL};  // 以watch id建立的HASH索引

zThreadPoolOps zCallBackList[16];  // 索引每个回调函数指针，对应于zObjInfo中的CallBackId

#define zDeployHashSiz 1024  // 布署状态HASH的大小
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
#include "md5_sig/zgenerate_sig_md5.c"  // 生成MD5 checksum检验和
#include "ops/zthread_pool.c"
#include "inotify/zinotify_callback.c"
#include "inotify/zinotify.c"  // 监控代码库文件变动
#include "ops/zinit_from_conf.c"  // 读取主配置文件
#include "ops/znetwork.c"  // 对外提供网络服务

/***************************
 * +++___ main 函数 ___+++ *
 ***************************/
_i
main(_i zArgc, char **zppArgv) {
    char *zpConfFilePath = NULL;
    struct stat zStatIf;
    _i zActionType = 0;
    zNetServInfo zNetServIf;  // 指定服务端自身的Ipv4地址与端口，或者客户端要连接的目标服务器的Ipv4地址与端口
    zNetServIf.zServType = TCP;

    for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "CUh:p:f:"));) {
        switch (zOpt) {
        case 'C':  // 启动客户端功能
            zActionType = 1; break;
        case 'h':
            zNetServIf.p_host= optarg; break;
        case 'p':
            zNetServIf.p_port = optarg; break;
        case 'U':
            zNetServIf.zServType = UDP;
        case 'f':
            if (-1 == stat(optarg, &zStatIf) || !S_ISREG(zStatIf.st_mode)) {  // 若指定的主配置文件不存在或不是普通文件，则报错退出
                zPrint_Time();
                fprintf(stderr, "\033[31;01mConfig file not exists or is not a regular file!\n"
                        "Usage: %s -f <Config File Path>\033[00m\n", zppArgv[0]);
                exit(1);
            }
            zpConfFilePath = optarg;
            break;
        default: // zOpt == '?'  // 若指定了无效的选项，报错退出
            zPrint_Time();
             fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
            exit(1);
        }
    }

    if (1 == zActionType) {  // 客户端功能，用于在ECS上由git hook自动执行，向服务端发送状态确认信息
        zupdate_ipv4_db_self(AT_FDCWD);  // 回应之前客户端将更新自身的ipv4地址库
        zclient_reply(zNetServIf.p_host, zNetServIf.p_port);
        return 0;
    }

//    zdaemonize("/");  // 转换自身为守护进程，解除与终端的关联关系

zReLoad:;
    _i zFd[2] = {0}, zRet = 0;
    zInotifyFD = inotify_init();  // 生成inotify master fd
    zCheck_Negative_Exit(zInotifyFD);

    zthread_poll_init();  // 初始化线程池

    // +++___+++ 需要手动维护每个回调函数的索引 +++___+++
    zCallBackList[0] = zupdate_cache;
    zCallBackList[1] = zupdate_ipv4_db_all;

    // 解析主配置文件，并将有效条目添加到监控队列
    zparse_conf_and_add_top_watch(zpConfFilePath);

    zRet = pthread_rwlockattr_setkind_np(&zRWLockAttr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP); // 设置读写锁属性为写优先，如：正在更新缓存、正在布署过程中、正在撤销过程中等，会阻塞查询请求
    if (0 > zRet) {
        zPrint_Err(zRet, NULL, "rwlock set attr failed!");
        exit(1);
    }

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
    // 索引每个代码库的读写锁
    zMem_Alloc(zpRWLock, pthread_rwlock_t, zRepoNum);

    // 每个代码库对应一个线性数组，用于接收每个ECS返回的确认信息
    // 同时基于这个线性数组建立一个HASH索引，以提高写入时的定位速度
    zMem_Alloc(zppDpResList, zDeployResInfo *, zRepoNum);
    zMem_Alloc(zpppDpResHash, zDeployResInfo **, zRepoNum);

    for (_i i = 0; i < zRepoNum; i++) {
        // 打开代码库顶层目录，生成目录fd供接下来的openat使用
        zFd[0] = open(zppRepoPathList[i], O_RDONLY);
        zCheck_Negative_Exit(zFd[0]);

		#define zCheck_Dir_Status_Exit(zRet) do {\
			if (-1 == (zRet) && errno != EEXIST) {\
		            zPrint_Err(errno, NULL, "Can't create directory!");\
		            exit(1);\
			}\
		} while(0)

        // 如果 .git_shadow 路径不存在，创建之，并从远程拉取该代码库的客户端ipv4列表
        // 需要--主动--从远程拉取该代码库的客户端ipv4列表 ???
		zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow", 0700));
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow/log", 0700));
        zCheck_Dir_Status_Exit(mkdirat(zFd[0], ".git_shadow/log/deploy", 0700));

        // 为每个代码库生成一把读写锁，锁属性设置写者优先
        if (0 != (zRet =pthread_rwlock_init(&(zpRWLock[i]), &zRWLockAttr))) {
            zPrint_Err(zRet, NULL, "Init deploy lock failed!");
            exit(1);
        }

        zupdate_ipv4_db_all(&i);
        zgenerate_cache(i);

        // 打开meta日志文件
        zpLogFd[0][i] = openat(zFd[0], zMetaLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[0][i]);
        // 打开data日志文件
        zpLogFd[1][i] = openat(zFd[0], zDataLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[1][i]);
        // 打开sig日志文件
        zpLogFd[2][i] = openat(zFd[0], zSigLogPath, O_RDWR | O_CREAT | O_APPEND, 0600);
        zCheck_Negative_Exit(zpLogFd[2][i]);

        close(zFd[0]);  // zFd[0] 用完关闭

        // 更新zpppDpResHash 与 **zppDpResList，每个代码库对应一个布署状态数据及与之配套的链式HASH
        zupdate_ipv4_db_hash(i);
    }

    zAdd_To_Thread_Pool(zinotify_wait, NULL);  // 主线程等待事件发生
    zAdd_To_Thread_Pool(zstart_server, &zNetServIf);  // 启动网络服务
    zconfig_file_monitor(zpConfFilePath);  // 监控自身主配置文件的内容变动

    close(zInotifyFD);  // 主配置文件有变动后，关闭inotify master fd

    pid_t zPid = fork(); // 之后父进程退出，子进程按新的主配置文件内容重新初始化
    zCheck_Negative_Exit(zPid);
    if (0 == zPid) { goto zReLoad; }
    else { exit(0); }
}
