#ifndef ZDPOPS_H
#define ZDPOPS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
    #endif

    #ifndef _DEFAULT_SOURCE
    #define _DEFAULT_SOURCE
    #endif

    #ifndef _BSD_SOURCE
    #define _BSD_SOURCE
    #endif
#endif

#include <pthread.h>  //  该头文件内部已使用 #define _PTHREAD_H 避免重复
#include <semaphore.h>  //  该头文件内部已使用 #define _SEMAPHORE_H 避免重复
#include <libpq-fe.h>  //  该头文件内部已使用 #define LIBPQ_FE_H 避免重复

#include "zCommon.h"
#include "zNativeUtils.h"
#include "zNetUtils.h"

#include "zLibSsh.h"
#include "zLibGit.h"

#include "zNativeOps.h"
#include "zRun.h"

#include "zPosixReg.h"
#include "zPgSQL.h"
#include "zThreadPool.h"
//#include "zMd5Sum.h"
#include "cJSON.h"

#define zGlobRepoNumLimit 256  // 可以管理的代码库数量上限
#define zCacheSiz 64  // 顶层缓存单元数量取值不能超过 IOV_MAX
#define zDpTraficLimit 256  // 同一项目可同时发出的 push 连接数量上限
#define zDpHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数
#define zSendUnitSiz 8  // sendmsg 单次发送的单元数量，在 Linux 平台上设定为 <=8 的值有助于提升性能
#define zForecastedHostNum 200  // 预测的目标主机数量上限

#define zGlobCommonBufSiz 1024

#define zDpUnLock 0
#define zDpLocked 1

#define zCacheGood 0
#define zCacheDamaged 1

#define zIsCommitDataType 0
#define zIsDpDataType 1

typedef struct __zThreadPool__ {
    pthread_t selfTid;
    pthread_cond_t condVar;

    void * (* func) (void *);
    void *p_param;
} zThreadPool__;

typedef struct __zDpRes__ {
    /*
     * unsigned long long int
     * IPv4 地址只使用第一个成员
     */
    _ull clientAddr[2];

    /*
     * << 布署状态 >>
     * bit[0]:目标端初始化(SSH)成功
     * bit[1]:服务端本地布署动作(git push)成功
     * bit[2]:目标端已收到推送内容(post-update)
     * bit[3]:目标端已确认内容无误(post-update)
     * bit[4]:目标端已确认布署后动作执行成功
     * bit[5]:
     * bit[6]:
     * bit[7]:
     */
    _uc resState;

    /*
     * << 错误类型 >>
     * err1 bit[0]:服务端错误
     * err2 bit[1]:网络不通
     * err3 bit[2]:SSH 连接认证失败
     * err4 bit[3]:目标端磁盘容量不足
     * err5 bit[4]:目标端权限不足
     * err6 bit[5]:目标端文件冲突
     * err7 bit[6]:目标端路径不存在
     * err8 bit[7]:目标端收到重复布署指令(同一目标机的多个不同IP)
     * err9 bit[8]:目标机 IP 格式错误/无法解析
     */
    _ui errState;

    /*
     * 存放本地产生的或目标机返回的错误信息
     */
    char errMsg[256];

    struct __zDpRes__ *p_next;
} zDpRes__;

typedef struct __zDpCcur__ {
    /*
     * 必须放置在首位
     * 线程池会将此指针指向每个线程的元信息
     * 清理特定线程时会用到
     */
    zThreadPool__ *p_threadSource_;

    _i repoId;

    /*
     * 单次动作的身份唯一性标识
     * 布署时为：time_stamp
     */
    _l id;

    /* 单个目标机 Ip，如："10.0.0.1" "::1" */
    char *p_hostIpStrAddr;

    /* 字符串形式的端口号，如："22" */
    char *p_hostServPort;

    /* 需要执行的 SSH 指令集合 */
    char *p_cmd;

    /* SSH 认证类型：公钥或密码 */
    zAuthType__ authType;

    /* 目标机上的用户名称 */
    const char *p_userName;

    /* 服务器上公钥所在路径，如："/home/git/.ssh/id_rsa.pub" */
    const char *p_pubKeyPath;

    /* 服务器上私钥所在路径，如："/home/git/.ssh/id_rsa" */
    const char *p_privateKeyPath;

    /*
     * 公钥认证时，用于提定公钥的密码
     * 密码认证时，用于指定用户登陆密码
     * 留空表示无密码
     */
    const char *p_passWd;

    /* 用于存放远程 SSH 命令的回显信息 */
    char *p_remoteOutPutBuf;
    _ui remoteOutPutBufSiz;

    /* libssh2 中的部分环节需要加锁，才能并发 */
    pthread_mutex_t *p_ccurLock;

    /* 指向目标机 IP 在服务端 HASH 链中的节点 */
    zDpRes__ *p_selfNode;
} zDpCcur__;


/* 用于存放本地操作时需要，但前端不需要的数据 */
typedef struct __zRefData__ {
    /* 传递给 sendmsg 的下一级数据 */
    struct __zVecWrap__ *p_subVecWrap_;

    /* 指向实际的数据存放空间 */
    char *p_data;
} zRefData__;

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
typedef struct __zVecWrap__ {
    /* 此数组中每个成员的 iov_base 字段均指向 p_refData_ 中对应的 p_data 字段 */
    struct iovec *p_vec_;
    _i vecSiz;

    struct __zRefData__ *p_refData_;
} zVecWrap__;


typedef struct __zCacheMeta__ {
    _s opsId;  // 网络交互时，代表操作指令（从 1 开始的连续排列的非负整数）
    _s repoId;  // 项目代号（从 1 开始的非负整数）
    _s commitId;  // 版本号
    _s dataType;  // 缓存类型，zIsCommitDataType/zIsDpDataType
    _i fileId;  // 单个文件在差异文件列表中 index
    _l cacheId;  // 缓存版本代号（最新一次布署的时间戳）

    char *p_filePath;  // git diff --name-only 获得的文件路径
    char *p_treeData;  // 经过处理的 Tree 图显示内容

    /* 以下为 Tree 专属数据 */
    struct __zCacheMeta__ *p_father;  // Tree 父节点
    struct __zCacheMeta__ *p_left;  // Tree 左节点
    struct __zCacheMeta__ *p_firstChild;  // Tree 首子节点：父节点唯一直接相连的子节点
    struct __zCacheMeta__ **pp_resHash;  // Tree 按行号对应的散列

    _i lineNum;  // 行号
    _i offSet;  // 纵向偏移
} zCacheMeta__;


struct zDpOps__ {
    _i (* show_dp_process) (cJSON *, _i);

    _i (* print_revs) (cJSON *, _i);
    _i (* print_diff_files) (cJSON *, _i);
    _i (* print_diff_contents) (cJSON *, _i);

    _i (* creat) (cJSON *, _i);

    _i (* dp) (cJSON *, _i);
    _i (* req_dp) (cJSON *, _i);

    _i (* state_confirm) (cJSON *, _i);

    _i (* lock) (cJSON *, _i);
    _i (* unlock) (cJSON *zpJRoot, _i zSd);

    _i (* req_file) (cJSON *, _i);

    void * (* route) (void *);
};


#define zIpVecCmp(zVec0, zVec1) ((zVec0)[0] == (zVec1)[0] && (zVec0)[1] == (zVec1)[1])

#define /*_i*/ zConvert_IpStr_To_Num(/*|_ull [2]|*/ zpIpStr, /*|char *|*/ zpNumVec) ({\
    _i zErrNo = 0;\
    if ('.' == zpIpStr[1] || '.' == zpIpStr[2] || '.' == zpIpStr[3]) {\
        zErrNo = zNetUtils_.to_numaddr(zpIpStr, zIpTypeV4, zpNumVec);\
    } else {\
        zErrNo = zNetUtils_.to_numaddr(zpIpStr, zIpTypeV6, zpNumVec);\
    };\
    zErrNo;  /* 宏返回值 */\
})

#define /*_i*/ zConvert_IpNum_To_Str(/*|_ull [2]|*/ zpNumVec, /*|char *|*/ zpIpStr) ({\
    _i zErrNo = 0;\
    if (0xff == zpNumVec[1] /* IPv4 */) {\
        zErrNo = zNetUtils_.to_straddr(zpNumVec, zIpTypeV4, zpIpStr);\
    } else {\
        zErrNo = zNetUtils_.to_straddr(zpNumVec, zIpTypeV6, zpIpStr);\
    } \
    zErrNo;  /* 宏返回值 */\
})


#endif  //  #ifndef ZDPOPS_H
