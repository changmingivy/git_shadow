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
#define zSV_MEM_POOL_SIZ 64 * 1024 * 1024
static void *zpMemPool;
static size_t zMemPoolOffSet;
static pthread_mutex_t zMemPoolLock;

/* 并发插入实例节点时所用 */
static pthread_mutex_t zNodeInsertLock;

static void zenv_init(void);
static void zdb_mgmt(void);
static void zdata_sync(void);

/* 对外接口 */
struct zSVCloud__ zSVCloud_ = {
    .init = zenv_init,
    .data_sync = zdata_sync,
    .tb_mgmt = zdb_mgmt,
};

struct zSvData__ {
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

//struct zDisk__ {
//    char *p_dev;  // "/dev/vda1"
//    struct zDisk__ *p_next;
//};
//
//struct zNetIf__ {
//    char *p_dev;  // "eth0"
//    struct zNetIf__ *p_next;
//};

/* instanceID(22 char) + '\0' */
#define zHASH_KEY_SIZ (1 + 23 / sizeof(_ull))
#define zINSTANCE_ID_BUF_LEN (1 + 23 / sizeof(_ull)) * sizeof(_ull)

struct zSv__ {
    /*
     * 以链表形式集齐实例所有 device 的名称，
     * 用于查询对应设备的监控数据
     */
    //struct zDisk__ disk;
    //struct zNetIf__ netIf;

    /* HASH KEY */
    union {
        _ull hashKey[zHASH_KEY_SIZ];
        char id[zINSTANCE_ID_BUF_LEN];
    };

    /* 以 900 秒（15 分钟）为周期同步监控数据，单机数据条数：60 */
    struct zSvData__ ecsSv_[60];

    struct zSv__ *p_next;
};

static void
zmem_pool_init(void) {
    zCHECK_PTHREAD_FUNC_EXIT(
            pthread_mutex_init(&zMemPoolLock, NULL)
            );

    if (NULL == (zpMemPool = malloc(zSV_MEM_POOL_SIZ))) {
        zPRINT_ERR_EASY_SYS();
        exit(1);
    }

    void **zppPrev = zpMemPool;
    zppPrev[0] = NULL;
    zMemPoolOffSet = sizeof(void *);
}

static void
zmem_pool_destroy(void) {
    pthread_mutex_lock(&zMemPoolLock);

    void **zppPrev = zpMemPool;
    while(NULL != zppPrev[0]) {
        zppPrev = zppPrev[0];
        free(zpMemPool);
        zpMemPool = zppPrev;
    }

    free(zpMemPool);
    zpMemPool = NULL;
    zMemPoolOffSet = 0;

    pthread_mutex_unlock(&zMemPoolLock);
    pthread_mutex_destroy(&zMemPoolLock);
}

static void *
zalloc(size_t zSiz) {
    pthread_mutex_lock(&zMemPoolLock);

    /* 检测当前内存池片区剩余空间是否充裕 */
    if ((zSiz + zMemPoolOffSet) > zSV_MEM_POOL_SIZ) {
        /* 请求的内存不能超过单个片区最大容量 */
        if (zSiz > (zSV_MEM_POOL_SIZ - sizeof(void *))) {
            pthread_mutex_unlock(&zMemPoolLock);
            zPRINT_ERR_EASY("req memory too large!");
            exit(1);
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

    pthread_mutex_unlock(&zMemPoolLock);

    return zpX;
}

/* 云监控模块 DB 预建 */
static void
zenv_init(void) {
    _i zErrNo,
       zBaseID;
    char zSQLBuf[512];

    zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo,
            "CREATE TABLE IF NOT EXISTS sv_aliyun "
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
                "CREATE TABLE IF NOT EXISTS sv_aliyun_%d "
                "PARTITION OF sv_aliyun FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);
    }
}

/* 定期扩展 DB 云监控分区表 */
static void
zdb_mgmt(void) {
    _i zBaseID,
       zID;

    char zBuf[512];

    /* 创建之后 10 * 24 小时的分区表 */
    zBaseID = time(NULL) / 3600;
    for (zID = 0; zID < 10 * 24; zID++) {
        sprintf(zBuf,
                "CREATE TABLE IF NOT EXISTS sv_aiyun_%d "
                "PARTITION OF sv_aiyun FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zBuf, 0, NULL, 0);
    }

    /* 清除 30 * 24 小时之前连续 10 * 24 小时分区表 */
    zBaseID -= 30 * 24;
    for (zID = 0; zID < 10 * 24; zID++) {
        sprintf(zBuf,
                "DROP TABLE IF EXISTS sv_aliyun_%d;",
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
static char * const zpRegion[] = {
    "cn-qingdao",
    "cn-beijing",
    "cn-zhangjiakou",
    "cn-huhehaote",
    "cn-hangzhou",
    "cn-shanghai",
    "cn-shenzhen",
    "cn-hongkong",

    "ap-southeast-1",
    "ap-southeast-2",
    "ap-southeast-3",
    "ap-south-1",
    "ap-northeast-1",

    "us-west-1",
    "us-east-1",

    "eu-central-1",

    "me-east-1",
};

/* HASH */
#define zHASH_SIZ 511
static struct zSv__ *zpSvHash_[zHASH_SIZ];

/* pBuf 相当于此宏的 “返回值” */
#define zGET_CONTENT(pCmd) ({\
    FILE *pFile = popen(pCmd, "r");\
    if (NULL == pFile) {\
        zPRINT_ERR_EASY_SYS();\
        exit(1);\
    }\
\
    /* 最前面的 12 个字符，是以 golang %-12d 格式打印的接收到的字节数 */\
    char CntBuf[12];\
    if (12 != zNativeUtils_.read_hunk(CntBuf, 12, pFile)) {\
        zPRINT_ERR_EASY("popen exec err!");\
        pclose(pFile);\
        exit(1);\
    }\
    CntBuf[11] = '\0';\
\
    _i Len = strtol(CntBuf, NULL, 10);\
\
    char *pBuf = zalloc(1 + Len);\
    zNativeUtils_.read_hunk(pBuf, Len, pFile);\
    pBuf[Len] = '\0';\
\
    pclose(pFile);\
    pBuf;\
})

#define zNODE_INSERT(zpItem_) do {\
    pthread_mutex_lock(&zNodeInsertLock);\
\
    if (NULL == zpSvHash_[zpSv_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ]) {\
        zpSvHash_[zpSv_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ] = zpItem_;\
    } else {\
        struct zSv__ *pTmp_ = zpSvHash_[zpSv_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ];\
        while (NULL != pTmp_->p_next) {\
            pTmp_ = pTmp_->p_next;\
        }\
\
        pTmp_->p_next = zpItem_;\
    }\
\
    pthread_mutex_unlock(&zNodeInsertLock);\
} while(0)

static _i
znode_parse_and_insert(void *zpJTransRoot, char *zpContent) {
    cJSON *zpJRoot = NULL,
          *zpJTmp = NULL,
          *zpJ = NULL;

    _i zErrNo = 0;
    struct zSv__ *zpSv_ = NULL;

    if (NULL == zpContent) {
        zErrNo = -1;
        goto zEndMark;
    }

    if (NULL == zpJTransRoot) {
        zpJRoot = cJSON_Parse(zpContent);
        if (NULL == zpJRoot) {
            zPRINT_ERR_EASY("");
            return -1;
        }
    } else {
        zpJRoot = zpJTransRoot;
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "Instances");
    if (NULL == zpJ) {
        zPRINT_ERR_EASY("");
        zErrNo = -1;
        goto zEndMark;
    }

    zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "Instance");
    if (NULL == zpJTmp) {
        zPRINT_ERR_EASY("");
        zErrNo = -1;
        goto zEndMark;
    }

    for (zpJ = zpJTmp->child; NULL != zpJ; zpJ = zpJ->next) {
        zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "InstanceId");

        if (cJSON_IsString(zpJTmp) && NULL != zpJTmp->valuestring) {
            zpSv_ = zalloc(sizeof(struct zSv__));
            zpSv_->p_next = NULL;

            /* 同时复制末尾的 '\0'，共计 23 bytes；剩余空间清零 */
            memcpy(zpSv_->id, zpJTmp->valuestring, 23);
            memset(zpSv_->id + 23, 0, zINSTANCE_ID_BUF_LEN - 23);

            /* insert new node */
            zNODE_INSERT(zpSv_);
        } else {
            zPRINT_ERR_EASY("InstanceId invalid ?");
            zErrNo = -1;
        }
    }

zEndMark:
    cJSON_Delete(zpJRoot);
    return zErrNo;
}

void *
zget_meta_thread_region_page(void *zp) {
    znode_parse_and_insert(NULL, zGET_CONTENT(zp));
    return NULL;
}

#define zPAGE_SIZE 100
void *
zget_meta_thread_region(void *zp/* zpRegion */) {
    char *zpContent = NULL;
    char zCmdBuf[512];

    _i zOffSet = 0;

    _i zPageTotal = 0,
       zPageNum = 0;

    cJSON *zpJRoot = NULL,
          *zpJ = NULL;

    /* 固定不变的参数 */
    zOffSet = snprintf(zCmdBuf, 512,
            "%s "
            "-region %s "
            "-userId %s "
            "-userKey %s "
            "-domain ecs.aliyuncs.com "
            "-apiName DescribeInstances "
            "-apiVersion 2014-05-26 "
            "Action DescribeInstances "
            "PageSize %d ",
            zpUtilPath,
            zp,
            zpAliyunID,
            zpAliyunKey,
            zPAGE_SIZE);

    /* call outer cmd: aliyun_cmdb */
    zpContent = zGET_CONTENT(zCmdBuf);

    zpJRoot = cJSON_Parse(zpContent);
    if (NULL == zpJRoot) {
        zPRINT_ERR_EASY("");
        return (void *) -1;
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "TotalCount");
    if (cJSON_IsNumber(zpJ)) {
        /* total pages */
        if (0 == zpJ->valueint % zPAGE_SIZE) {
            zPageTotal = zpJ->valueint / zPAGE_SIZE;
        } else {
            zPageTotal = 1 + zpJ->valueint / zPAGE_SIZE;
        }
    } else {
        zPRINT_ERR_EASY("TotalCount invalid ?");
        return (void *) -1;
    }

    /* first page */
    znode_parse_and_insert(zpJRoot, zpContent);

    /* multi pages */
    if (1 < zPageTotal) {
        zp = zalloc((zPageTotal - 1) * (zOffSet + 24));
        pthread_t zTid[zPageTotal - 1];

        for (zPageNum = 2; zPageNum <= zPageTotal; ++zPageNum) {
            zp += (zPageNum - 2) * (zOffSet + 24);
            memcpy(zp, zCmdBuf, zOffSet);
            snprintf(zp + zOffSet, 24, "PageNumber %d", zPageNum); 
            pthread_create(zTid + zPageNum - 2, NULL, zget_meta_thread_region_page, zp);
        }

        for (zPageNum = 2; zPageNum <= zPageTotal; zPageNum++) {
            pthread_join(zTid[zPageNum - 2], NULL);
        }
    }

    return NULL;
}

void
zget_meta(void) {
    pthread_t zTid[sizeof(zpRegion) / sizeof(void *)];
    _i i;

    /* 提取所有实例 ID */
    for (i = 0; i < (_i) (sizeof(zpRegion) / sizeof(void *)); ++i) {
        pthread_create(zTid + i, NULL, zget_meta_thread_region, zpRegion[i]);
    }

    for (i = 0; i < (_i) (sizeof(zpRegion) / sizeof(void *)); ++i) {
        pthread_join(zTid[i], NULL);
    }
}

void
zget_sv(void) {
    //const char * const zpDomain = "metrics.aliyuncs.com";
    //const char * const zpApiName = "QueryMetricList";
    //const char * const zpApiVersion = "2017-03-01";
}

/* 同步云上数据至本地数据库 */
static void
zdata_sync(void) {
    zmem_pool_init();

    zget_meta();
    zget_sv();

    zmem_pool_destroy();
}

#undef zINSTANCE_ID_BUF_LEN
#undef zHASH_KEY_SIZ
#undef zSV_MEM_POOL_SIZ
