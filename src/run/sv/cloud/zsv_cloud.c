#include "zsv_cloud.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

extern struct zRun__ zRun_;
extern struct zPgSQL__ zPgSQL_;
extern struct zThreadPool__ zThreadPool_;
extern struct zNativeUtils__ zNativeUtils_;

extern zRepo__ *zpRepo_;

/*
 * 定制专用的内存池：开头留一个指针位置，
 * 用于当内存池容量不足时，指向下一块新开辟的内存区
 */
#define zSV_MEM_POOL_SIZ 8 * 1024 * 1024
static void *zpMemPool;
static size_t zMemPoolOffSet;
static pthread_mutex_t zMemPoolLock;

/* 并发插入实例节点时所用 */
static pthread_mutex_t zNodeInsertLock;

static void zsv_cloud_prepare(void);
static void zcloud_data_sync(void);
static void zpg_tb_mgmt(void);

/* 对外接口 */
struct zSVCloud__ zSVCloud_ = {
    .init = zsv_cloud_prepare,
    .data_sync = zcloud_data_sync,
    .tb_mgmt = zpg_tb_mgmt,
};

struct zEcsDisk__ {
    char *p_dev;  // "/dev/vda1"
    struct zEcsDisk__ *p_next;
};

struct zEcsNetIf__ {
    char *p_dev;  // "eth0"
    struct zEcsNetIf__ *p_next;
};

struct zSvEcsData__ {
    /* 可直接取到的不需要额外加工的数据项 */
    _i timeStamp;
    _s cpu;
    _s mem;
    _s load[3];

    /* 分别处于 tcp 的 11 种状态的连接计数 */
    _us tcpState[11];

    /*
     * 取磁盘列表的时，可得到以 GB 为单位的容量，
     * 不需要从监控数据中再取一次，
     * 所有磁盘容量加和之后，换算成 M，保持与 diskSpent 的单位统一
     */
    _i diskTotal;

    /*
     * 首先将 byte 换算成 M(/1024/1024)，以 M 为单位累加计数，
     * 集齐所有磁盘的数据后，计算实例的全局磁盘总使用率
     */
    _i diskSpent;

    /*
     * 首先将 byte 换算成 KB(/1024)，以 KB 为单位累加计数，
     * 集齐所有设备的数据后，取得最终的 sum 值
     */
    _i disk_rdkb;
    _i disk_wrkb;

    _i disk_rdiops;
    _i disk_wriops;

    _i net_rdkb;
    _i net_wrkb;

    _i net_rdiops;
    _i net_wriops;
};

#define zHASH_KEY_SIZ (1 + 23 / sizeof(_ull))  // instanceID(22 char) + '\0'
struct zSvEcs__ {
    /*
     * 以链表形式集齐实例所有 device 的名称，
     * 用于查询对应设备的监控数据
     */
    struct zEcsDisk__ disk;
    struct zEcsNetIf__ netIf;

    /* HASH KEY */
    union {
        _ull hashKey[zHASH_KEY_SIZ];
        char id[sizeof(_ull) * zHASH_KEY_SIZ];
    };

    /* 以 900 秒（15 分钟）为周期同步监控数据，单机数据条数：60 */
    struct zSvEcsData__ ecsSv_[60];

    struct zSvEcs__ *p_next;
};

static void
zmem_pool_init(void) {
    zCHECK_PTHREAD_FUNC_EXIT(
            pthread_mutex_init(&zMemPoolLock, NULL)
            );

    if (NULL == (zpMemPool = malloc(zMEM_POOL_SIZ))) {
        zPRINT_ERR_EASY_SYS();
        exit(1);
    }

    void **zppPrev = zpMemPool;
    zppPrev[0] = NULL;
    zMemPoolOffSet = sizeof(void *);
}

static void
zmem_pool_destroy(void) {
    pthread_mutex_lock(& zMemPoolLock);

    void **zppPrev = zpMemPool;
    while(NULL != zppPrev[0]) {
        zppPrev = zppPrev[0];
        free(zpMemPool);
        zpMemPool = zppPrev;
    }

    free(zpMemPool);
    zpMemPool = NULL;
    zMemPoolOffSet = 0;

    pthread_mutex_unlock(& zMemPoolLock);
    pthread_mutex_destroy(&zMemPoolLock);
}

static void *
zalloc(size_t zSiz) {
    pthread_mutex_lock(& zMemPoolLock);

    /* 检测当前内存池片区剩余空间是否充裕 */
    if ((zSiz + zMemPoolOffSet) > zSV_MEM_POOL_SIZ) {
        /* 请求的内存不能超过单片区最大容量 */
        if (zSiz > (zSV_MEM_POOL_SIZ - sizeof(void *))) {
            zPRINT_ERR_EASY("");
            pthread_mutex_unlock(& zMemPoolLock);
            return NULL;
        }

        /* 新增一片内存，加入内存池 */
        void *zpCur = NULL;
        zMEM_ALLOC(zpCur, char, zSV_MEM_POOL_SIZ);

        /*
         * 首部指针位，指向内存池中的前一片区
         */
        void **zppPrev = zpCur;
        zppPrev[0] = zpMemPool;

        /*
         * 内存池指针更新
         */
        zpMemPool = zpCur;

        /*
         * 新内存片区开头的一个指针大小的空间已经被占用
         * 不能再分配，需要跳过
         */
        zMemPoolOffSet = sizeof(void *);
    }

    /*
     * 分配内存
     */
    void *zpX = zpMemPool + zMemPoolOffSet;
    zMemPoolOffSet += zSiz;

    pthread_mutex_unlock(& zMemPoolLock);

    return zpX;
}

/* 云监控模块 DB 预建 */
static void
zsv_cloud_prepare(void) {
    _i zErrNo,
       zBaseID;
    char zSQLBuf[512];

    zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo,
            "CREATE TABLE IF NOT EXISTS sv_ecs_aliyun "
            "("
            /*
             * aliyun 原始值为毫秒，
             * 存储除以 15000 后的值，即：UNIX 计元至今有多少个 15 秒，
             * 保障各种监控指标的时间戳均能一一对应
             */
            "time_stamp      int NOT NULL,"

            /* 实例 ID，由 22 位字符组成 */
            "instance_id     char(22) NOT NULL,"

            /*
             * = 10 * cpu usage rate，阿里云返回的是去掉 % 的值，如：81.66，
             * 此处乘以 10，以整数形式保留一位小数的精度，如示例便是 817，
             * 内存使用率计算方式同上
             */
            "cpu_rate        smallint NOT NULL,"
            "mem_rate        smallint NOT NULL,"

            /*
             * 磁盘使用率：
             * 此项指标需要取回所有磁盘的已使用空间绝对大小与总空间大小，
             * 之后计算得出最终结果（以百分值的 1000 倍存储），不能直接取阿里云的比率
             */
            "disk_rate       smallint NOT NULL,"

            /*
             * 阿里云返回的值与 top 命令显示的格式一致，如：3.88，
             * 乘以 1000，之后除以 CPU 核心数，以整数整式保留一位小数的精度，
             * 不能乘以 10000，可以会导致 smallint 溢出。
             * = 1000 * system load average recent 1 mins/ 5 mins / 15mins
             */
            "load_1m            smallint NOT NULL,"
            "load_5m            smallint NOT NULL,"
            "load_15m           smallint NOT NULL,"

            /*
             * 同一主机可能拥有多个网卡、磁盘，
             * 仅保留所有设备之和，不存储名细
             * 磁盘与网络流量类指标，原始单位为 bytes，保留除以 1024 后的值，单位：KB
             */
            "disk_rdkb       int NOT NULL,"  /* io_read kbytes/KB */
            "disk_wrkb       int NOT NULL,"  /* io_write kbytes/KB */
            "disk_rdiops     int NOT NULL,"  /* io_read tps */
            "disk_wriops     int NOT NULL,"  /* io_write tps */
            "net_rdkb        int NOT NULL,"  /* io_read kbytes/KB */
            "net_wrkb        int NOT NULL,"  /* io_write kbytes/KB */
            "net_rdiops      int NOT NULL,"  /* io_read tps */
            "net_wriops      int NOT NULL,"  /* io_write tps */

            /* 分别处于 tcp 的 11 种状态的连接计数 */
            "tcp_state_cnt          int[11] NOT NULL"
            ") PARTITION BY RANGE (time_stamp);",
            NULL);

    if (0 != zErrNo) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    /* 每次启动时尝试创建必要的表，按小时分区（1天 == 3600秒） */
    zBaseID = time(NULL) / 3600;
    for (_i zID = 0; zID < 10 * 24; zID++) {
        sprintf(zSQLBuf,
                "CREATE TABLE IF NOT EXISTS sv_ecs_aliyun_%d "
                "PARTITION OF sv_ecs_aliyun FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);
    }
}

/* 定期扩展 DB 云监控分区表 */
static void
zpg_tb_mgmt(void) {
    _i zBaseID,
       zID;

    char zBuf[512];

    /* 创建之后 10 * 24 小时的分区表 */
    zBaseID = time(NULL) / 3600;
    for (zID = 0; zID < 10 * 24; zID++) {
        sprintf(zBuf,
                "CREATE TABLE IF NOT EXISTS sv_ecs_aiyun_%d "
                "PARTITION OF sv_ecs_aiyun FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zBuf, 0, NULL, 0);
    }

    /* 清除 30 * 24 小时之前连续 10 * 24 小时分区表 */
    zBaseID -= 30 * 24;
    for (zID = 0; zID < 10 * 24; zID++) {
        sprintf(zBuf,
                "DROP TABLE IF EXISTS sv_ecs_aliyun_%d;",
                zBaseID - zID);

        zPgSQL_.write_db(zBuf, 0, NULL, 0);
    }
}

/******************
 * 如下：提取数据 *
 ******************/
static const char * const zpUtilPath = "/tmp/aliyun_cmdb";

static const char * const zpAliyunID = "LTAIHYRtkSXC1uTl";
static const char * const zpAliyunKey = "l1eLkvNkVRoPZwV9jwRpmq1xPOefGV";
static const char * const zpRegion[] = {
    "cn-beijing",
    "cn-hangzhou",
};

/* HASH */
static struct zSvEcs__ *zpSvEcsHash_[511];

static void
zecs_node_insert(char *zpInstanceID) {
    // TODO
}

void *
zget_meta_ecs_disk(void *zp __attribute__((__unused__))) {
    //const char * const zpDomain = "ecs.aliyuncs.com";
    //const char * const zpApiName = "DescribeDisks";
    //const char * const zpApiVersion = "2014-05-26";

    return NULL;
}

void *
zget_meta_ecs_netif(void *zp __attribute__((__unused__))) {
    //const char * const zpDomain = "ecs.aliyuncs.com";
    //const char * const zpApiName = "DescribeNetworkInterfaces";
    //const char * const zpApiVersion = "2014-05-26";

    return NULL;
}

#define zGET_CONTENT(pBuf, pCmd) do {\
    FILE *pFile = popen(pCmd, "r");\
    if (NULL == pFile) {\
        zPRINT_ERR_EASY_SYS();\
        exit(1);\
    }\
\
    /* 最前面的 12 个字符，是以 golang %-12d 格式打印的接收到的字节数 */\
    char CntBuf[12] = {'\0'};\
    zNativeUtils_.read_hunk(CntBuf, 11, pFile);\
\
    _i Len = strtol(CntBuf, NULL, 10);\
\
    pBuf = zalloc(1 + Len);\
    zNativeUtils_.read_hunk(pBuf, Len, pFile);\
    pBuf[Len] = '\0';\
\
    pclose(pFile);\
} while(0)

void *
zget_meta_ecs_thread_region_page(void *zp) {
    char *zpContent = NULL;
    zGET_CONTENT(zpContent, zp);

    // TODO json parse

    // 插入信息
    zecs_node_insert(NULL);

    return NULL;
}

void *
zget_meta_ecs_thread_region(void *zp) {
    char *zpContent = NULL;
    char zCmdBuf[512];

    _i zOffSet;

    _i zPageTotal,
       zPageNum;

    /* 固定参数部分 */
    zOffSet = snprintf(zCmdBuf, 512,
            "%s -region %s -userId %s -userKey %s "
            "-domain ecs.aliyuncs.com "
            "-apiName DescribeInstances "
            "-apiVersion 2014-05-26 "
            "Action DescribeInstances "
            "PageSize 100 ",
            zpUtilPath,
            zp,
            zpAliyunID,
            zpAliyunKey);

    zGET_CONTENT(zpContent, zCmdBuf);
    // TODO json parse

    // 插入信息
    zecs_node_insert(NULL);

    /* 计算总页数 */
    zPageTotal = 0;

    zp = zalloc((zPageTotal - 1) * (zOffSet + 24));
    pthread_t zTid[zPageTotal - 1];

    for (zPageNum = 2; zPageNum <= zPageTotal; zPageNum++) {
        zp += (zPageNum - 2) * (zOffSet + 24);
        memcpy(zp, zCmdBuf, zOffSet);

        snprintf(zp + zOffSet, 24,
                "PageNumber %d",
                zPageNum);

        pthread_create(zTid + zPageNum - 2, NULL, zget_meta_ecs_thread_region_page, NULL);
    }

    for (zPageNum = 2; zPageNum <= zPageTotal; zPageNum++) {
        pthread_join(zTid[zPageNum - 2], NULL);
    }

    return NULL;
}

void
zget_meta_ecs(void) {
    pthread_t zTid[2];
    pthread_t zRegionTid[sizeof(zpRegion) / sizeof(void *)];

    _i i;

    // TODO 用屏障保证 ECS 主列表就绪后，disk 与 netif 线程才开始数据插入工作
    pthread_create(zTid, NULL, zget_meta_ecs_disk, NULL);
    pthread_create(zTid + 1, NULL, zget_meta_ecs_netif, NULL);

    for (i = 0; i < (_i) (sizeof(zpRegion) / sizeof(void *)); ++i) {
        pthread_create(zRegionTid + i, NULL, zget_meta_ecs_thread_region, NULL);
    }

    for (i = 0; i < (_i) (sizeof(zpRegion) / sizeof(void *)); ++i) {
        pthread_join(zRegionTid[i], NULL);
    }

    // TODO 屏障解除

    /* 等待两个辅助线程完工 */
    pthread_join(zTid[0], NULL);
    pthread_join(zTid[1], NULL);
}

void
zget_sv_ecs(void) {
    //const char * const zpDomain = "metrics.aliyuncs.com";
    //const char * const zpApiName = "QueryMetricList";
    //const char * const zpApiVersion = "2017-03-01";

}

/* 同步云上数据至本地数据库 */
static void
zcloud_data_sync(void) {
    zmem_pool_init();

    zget_meta_ecs();
    // TODO get_meta_...()

    zget_sv_ecs();

    zmem_pool_destroy();
}

#undef zHASH_KEY_SIZ
#undef zSV_MEM_POOL_SIZ
