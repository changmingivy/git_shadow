#include "zecs.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

/* 专用内存池初始容量：16M */
#define zSV_MEM_POOL_SIZ 16 * 1024 * 1024

/* ECS 元信息分页并发查询 */
#define zMETA_PAGE_SIZ 100

/* check pthread functions error */
#define zCHECK_PT_ERR(zRet) do {\
    if (0 != (zRet)) {\
        zPRINT_ERR_EASY("pthread_create() err!");\
        exit(1);\
    }\
} while(0)

extern struct zRun__ zRun_;
extern struct zPgSQL__ zPgSQL_;
extern struct zThreadPool__ zThreadPool_;
extern struct zNativeUtils__ zNativeUtils_;

extern zRepo__ *zpRepo_;

/*
 * 定制专用的内存池：开头留一个指针位置，
 * 用于当内存池容量不足时，指向下一块新开辟的内存区
 */
static void *zpMemPool;
static size_t zMemPoolOffSet;
static pthread_mutex_t zMemPoolLock;

/* 并发插入实例节点时所用 */
static pthread_mutex_t zNodeInsertLock;

/*
 * func declare
 */
static void zenv_init(void);
static void zdb_mgmt(void);
static void zdata_sync(void);

/* public interface */
struct zSvAliyunEcs__ zSvAliyunEcs_ = {
    .init = zenv_init,
    .data_sync = zdata_sync,
    .tb_mgmt = zdb_mgmt,
};

static void
zmem_pool_init(void) {
    zCHECK_PT_ERR(pthread_mutex_init(&zMemPoolLock, NULL));

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

static struct zRegion__ zRegion_[] = {
    {"cn-qingdao", 0},
    {"cn-beijing", 0},
    {"cn-zhangjiakou", 0},
    {"cn-huhehaote", 0},
    {"cn-hangzhou", 0},
    {"cn-shanghai", 0},
    {"cn-shenzhen", 0},
    {"cn-hongkong", 0},

    {"ap-southeast-1", 0},
    {"ap-southeast-2", 0},
    {"ap-southeast-3", 0},
    {"ap-south-1", 0},
    {"ap-northeast-1", 0},

    {"us-west-1", 0},
    {"us-east-1", 0},

    {"eu-central-1", 0},

    {"me-east-1", 0},
};

static const char * const zpDomain = "metrics.aliyuncs.com";
static const char * const zpApiName = "QueryMetricList";
static const char * const zpApiVersion = "2017-03-01";

/*
 * 上一次同步数据的毫秒时间戳（UNIX timestamp * 1000 之后的值）上限 + 1，
 * aliyun 查询时的时间区间是前后均闭合的，故使用时将上限设置为 15s 边界数减 1ms，
 * 以确保区间查询统一为前闭后开的形式，避免数据重复
 */
static _ll zPrevStamp;  /* 15000 毫秒的整数倍 */

/* 生成元数据时使用线性线结构，匹配监控数据时再转换为 HASH */
static struct zSv__ *zpHead_ = NULL;
static struct zSv__ *zpTail_ = NULL;

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
    char *pBuf = NULL;\
    if (12 == zNativeUtils_.read_hunk(CntBuf, 12, pFile)) {\
        CntBuf[11] = '\0';\
        _i Len = strtol(CntBuf, NULL, 10);\
\
        pBuf = zalloc(1 + Len);\
\
        zNativeUtils_.read_hunk(pBuf, Len, pFile);\
        pBuf[Len] = '\0';\
    } else {\
        zPRINT_ERR_EASY("err!");\
    }\
\
    pclose(pFile);\
    pBuf;\
})

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
            memset(zpSv_->ecsSv_, 0, 60 * sizeof(struct zSvData__));

            /* 同时复制末尾的 '\0'，共计 23 bytes；剩余空间清零 */
            memcpy(zpSv_->id, zpJTmp->valuestring, 23);
            memset(zpSv_->id + 23, '\0', zINSTANCE_ID_BUF_LEN - 23);

            /* insert new node */
            pthread_mutex_lock(&zNodeInsertLock);
            if (NULL == zpHead_) {
                zpHead_ = zpSv_;
                zpTail_ = zpHead_;
            } else {
                zpTail_->p_next = zpSv_;
                zpTail_ = zpTail_->p_next;
            }
            pthread_mutex_unlock(&zNodeInsertLock);
        } else {
            zPRINT_ERR_EASY("InstanceId invalid ?");
            zErrNo = -1;
        }
    }

zEndMark:
    cJSON_Delete(zpJRoot);
    return zErrNo;
}

static void *
zget_meta_one_page(void *zp) {
    znode_parse_and_insert(NULL, zGET_CONTENT(zp));
    return NULL;
}

static void
zget_meta_one_region(struct zRegion__ *zpRegion_) {
    char *zpContent = NULL;
    char zCmdBuf[512];

    _i zOffSet = 0;

    _i zPageTotal = 0,
       zPageNum = 0;

    cJSON *zpJRoot = NULL,
          *zpJ = NULL;

    char *zp = NULL;

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
            zpRegion_->p_name,
            zpAliyunID,
            zpAliyunKey,
            zMETA_PAGE_SIZ);

    /* call outer cmd: aliyun_cmdb */
    zpContent = zGET_CONTENT(zCmdBuf);

    zpJRoot = cJSON_Parse(zpContent);
    if (NULL == zpJRoot) {
        zPRINT_ERR_EASY("");
        return;
    }

    zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "TotalCount");
    if (cJSON_IsNumber(zpJ)) {
        zpRegion_->ecsCnt = zpJ->valueint;

        /* total pages */
        if (0 == zpRegion_->ecsCnt  % zMETA_PAGE_SIZ) {
            zPageTotal = zpJ->valueint / zMETA_PAGE_SIZ;
        } else {
            zPageTotal = 1 + zpRegion_->ecsCnt / zMETA_PAGE_SIZ;
        }
    } else {
        zPRINT_ERR_EASY("TotalCount invalid ?");
        return;
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

            zCHECK_PT_ERR(pthread_create(zTid + zPageNum - 2, NULL, zget_meta_one_page, zp));
        }

        for (zPageNum = 2; zPageNum <= zPageTotal; zPageNum++) {
            pthread_join(zTid[zPageNum - 2], NULL);
        }
    }
}

/*
 * 通用的 ECS 监控数据处理函数
 */
static void *
zget_sv_ops(void *zp) {
    _i zOffSet;
    char *zpCmdBuf = NULL,
         *zpContent = NULL,
         *zpCursor = NULL;

    cJSON *zpJRoot = NULL,
          *zpJTmp = NULL,
          *zpJ = NULL;

    _i zTimeStamp = 0,
       i;

    _f zData = 0;

    struct zSvParam__ *zpSvParam_ = zp;

    struct zSv__ *zpSv_ = NULL;

    union zInstanceId__ zInstanceId_;
    memset(zInstanceId_.id + 23, '\0', zINSTANCE_ID_BUF_LEN - 23);

    zpCmdBuf = zalloc(1024 + strlen((char *)zp + sizeof(void *)));

    /* 固定不变的参数，第二次查询开始，将在末尾追加/更新游标 */
    zOffSet = snprintf(zpCmdBuf, 2048,
            "%s "
            "-region %s "
            "-userId %s "
            "-userKey %s "
            "-domain metrics.aliyuncs.com "
            "-apiName QueryMetricList "
            "-apiVersion 2017-03-01 "
            "Period 15 "
            "Length 1000 "
            "Action QueryMetricList "
            "Project acs_ecs_dashboard "
            "Metric %s "
            "StartTime %lld"
            "EndTime %lld"
            "Dimensions %s ",
            zpUtilPath,
            zpSvParam_->p_paramSolid->p_region,
            zpAliyunID,
            zpAliyunKey,
            zpSvParam_->p_metic,
            zPrevStamp,
            zPrevStamp + 15 * 60 * 1000 - 1,
            zpSvParam_->p_paramSolid->p_dimensions);

    do {
        if (NULL != zpJRoot) {
            snprintf(zpCmdBuf + zOffSet, 2048 - zOffSet, "Cursor %s", zpCursor);

            /* 前一次的 cJSON 资源已使用完毕，清除之 */
            cJSON_Delete(zpJRoot);
            zpJRoot = NULL;
        }

        zpContent = zGET_CONTENT(zpCmdBuf);

        zpJRoot = cJSON_Parse(zpContent);
        if (NULL == zpJRoot) {
            zPRINT_ERR_EASY("");
            return (void *) -1;
        }

        zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJRoot, "Datapoints");
        if (NULL == zpJTmp) {
            cJSON_Delete(zpJRoot);

            zPRINT_ERR_EASY("Datapoints ?");
            return NULL;
        }

        for (zpJ = zpJTmp->child; NULL != zpJ; zpJ = zpJ->next) {
            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "timestamp");
            if (cJSON_IsNumber(zpJTmp)) {
                /* 修正时间戳，按 15s 对齐 */
                zTimeStamp = ((_ll)zpJTmp->valuedouble) / (15 * 1000) ;
            } else {
                zPRINT_ERR_EASY("timestamp ?");
                continue;
            }

            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "instanceId");
            if (cJSON_IsString(zpJTmp)) {
                strncpy(zInstanceId_.id, zpJTmp->valuestring, 23);
            } else {
                zPRINT_ERR_EASY("instanceId ?");
                continue;
            }

            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "Average");
            if (cJSON_IsNumber(zpJTmp)) {
                zData = zpJTmp->valuedouble;
            } else {
                zPRINT_ERR_EASY("Average ?");
                continue;
            }

            for (zpSv_ = zpSvHash_[zInstanceId_.hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ];
                    NULL != zpSv_;
                    zpSv_ = zpSv_->p_next) {
                for (i = 0; i < (zHASH_KEY_SIZ - 1); i++) {
                    if (zpSv_->hashKey[i] != zInstanceId_.hashKey[i]) {
                        goto zNextMark;
                    }
                }

                zpSv_->ecsSv_[zTimeStamp % 60].timeStamp = zTimeStamp;
                zpSvParam_->cb(& ((_i *)(& zpSv_->ecsSv_[zTimeStamp % 60]))[zpSvParam_->targetID], zData);
                break;
zNextMark:;
            }
        }

        zpJ = cJSON_GetObjectItemCaseSensitive(zpJRoot, "Cursor");
        if (cJSON_IsString(zpJ)) {
            zpCursor = zpJ->valuestring;
        } else {
            zpCursor = NULL;
        }
    } while(NULL != zpCursor);

    /* 清除最后一次的 cJSON 资源 */
    cJSON_Delete(zpJRoot);

    return NULL;
}

/*
 * 用于计算不同类别监控数据结果的 callback
 */
static void 
zsv_cb_cpu_mem(_i *zpBase, _f zNew) {
    *zpBase = zNew * 10;
}

static void 
zsv_cb_load(_i *zpBase, _f zNew) {
    *zpBase = zNew * 1000;
}

static void 
zsv_cb_default(_i *zpBase, _f zNew) {
    *zpBase = zNew;
}

static void 
zsv_cb_disk_total_spent(_i *zpBase, _f zNew) {
    (*zpBase) += zNew / 1024 / 1024;  // 单位：M
}

static void 
zsv_cb_diskiokb(_i *zpBase, _f zNew) {
    (*zpBase) += zNew / 1024;
}

static void 
zsv_cb_netiokb(_i *zpBase, _f zNew) {
    (*zpBase) += zNew / 8 / 1024;
}

/* 监控信息按主机数量分组并发查询*/
#define zSPLIT_UNIT 200
#define zSPLIT_SIZE_BASE ((sizeof("'[]'") - 1) + 200 * (sizeof("{\"instanceId\":\"i-instanceIdinstanceId\"}") - 1) + 199 * (sizeof(",") - 1))
#define zSPLIT_SIZE_TCP_STATE(state) (zSPLIT_SIZE_BASE + 200 * (sizeof(",\"state\":\"\"") - 1 + strlen(state)))
static void
zget_sv_one_region(struct zRegion__ *zpRegion_) {
    static struct zSvParamSolid__ *zpBaseSolid;  /* 指向分组后的各区间数据(拼接好的字符串) */
    //static **zppSplitDisk;  /* 分组同上，但每个字段添加磁盘 device 过滤条件 */
    //static **zppSplitNetIf;  /* 分组同上，但每个字段添加网卡 device 过滤条件 */
    static struct zSvParamSolid__ *zpTcpStateSolid[11];  /* 分组同上，分别查询 TCP 的 11 种状态关联的连接数量 */

    _i zSplitCnt,
       zOffSet,
       i,
       j,
       k;

    /* 顺序不能变！ */
    char *zpTcpState[] = { "LISTEN", "SYN_SENT", "ESTABLISHED",
        "SYN_RECV", "FIN_WAIT1", "CLOSE_WAIT", "FIN_WAIT2",
        "LAST_ACK", "TIME_WAIT", "CLOSING", "CLOSED"};

    if (0 == zpRegion_->ecsCnt % zSPLIT_UNIT) {
        zSplitCnt = zpRegion_->ecsCnt / zSPLIT_UNIT;
    } else {
        zSplitCnt = 1 + zpRegion_->ecsCnt / zSPLIT_UNIT;
    }

    zpBaseSolid = zalloc(zSplitCnt * sizeof(struct zSvParamSolid__));
    zpTail_ = zpHead_;
    for (j = 0; j < zSplitCnt; j++) {
        zpBaseSolid[j].p_region = zpRegion_->p_name;
        zpBaseSolid[j].p_dimensions = zalloc(zSPLIT_SIZE_BASE);

        zOffSet = sprintf(zpBaseSolid[j].p_dimensions, "'");

        for (k = 0; k < zSPLIT_UNIT && NULL != zpTail_;
                k++, zpTail_ = zpTail_->p_next) {
            zOffSet += sprintf(zpBaseSolid[j].p_dimensions + zOffSet,
                    ",{\"instanceId\":\"%s\"}",
                    zpTail_->id);
        }

        sprintf(zpBaseSolid[j].p_dimensions + zOffSet, "]'");
        zpBaseSolid[j].p_dimensions[1] = '[';
    }

    for (i = 0; i < 11; i++) {
        zpTcpStateSolid[i] = zalloc(zSplitCnt * sizeof(struct zSvParamSolid__));
        zpTail_ = zpHead_;
        for (j = 0; j < zSplitCnt; j++) {
            zpTcpStateSolid[i][j].p_region = zpRegion_->p_name;
            zpTcpStateSolid[i][j].p_dimensions = zalloc(zSPLIT_SIZE_TCP_STATE(zpTcpState[j]));

            zOffSet = sprintf(zpTcpStateSolid[i][j].p_dimensions, "'");

            for (k = 0; k < zSPLIT_UNIT && NULL != zpTail_; k++, zpTail_ = zpTail_->p_next) {
                zOffSet += sprintf(zpTcpStateSolid[i][j].p_dimensions + zOffSet,
                        ",{\"instanceId\":\"%s\",\"state\":\"%s\"}",
                        zpTail_->id,
                        zpTcpState[i]);
            }

            sprintf(zpTcpStateSolid[i][j].p_dimensions + zOffSet, "]'");
            zpTcpStateSolid[i][j].p_dimensions[1] = '[';
        }
    }

    /* 将线性链表转换为 HASH 结构 */
    struct zSv__ *zpTmp_;
    for (zpTail_ = zpHead_; NULL != zpTail_; zpTail_ = zpTail_->p_next) {
        if (NULL == zpSvHash_[zpTail_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ]) {
            zpSvHash_[zpTail_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ] = zpTail_;
        } else {
            zpTmp_ = zpSvHash_[zpTail_->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ];
            while (NULL != zpTmp_->p_next) {
                zpTmp_ = zpTmp_->p_next;
            }

            zpTmp_->p_next = zpTail_;
            zpTail_->p_next = NULL;  // must!
        }
    }

    /* 定义动态栈空间存放 tid */
    pthread_t zTid[zSplitCnt][26];

    /* 注册参数 */
    struct zSvParam__ *zpSvParam_ = zalloc(26 * sizeof(struct zSvParam__));

    for (i = 0; i < 26; i++) {
        zpSvParam_[i].targetID = i;
    }

    for (i = 0; i < 11; i++) {
        zpSvParam_[i].p_metic = "net_tcpconnection";
        zpSvParam_[i].p_paramSolid = zpTcpStateSolid[i];
        zpSvParam_[i].cb = zsv_cb_default;
    }

    for (i = 11; i < 26; i++) {
        zpSvParam_[i].p_paramSolid = zpBaseSolid;
    }

    zpSvParam_[11].p_metic = "cpu_total";
    zpSvParam_[11].cb = zsv_cb_cpu_mem;

    zpSvParam_[12].p_metic = "memory_usedutilization";
    zpSvParam_[12].cb = zsv_cb_cpu_mem;

    zpSvParam_[13].p_metic = "load_1m";
    zpSvParam_[13].cb = zsv_cb_load;

    zpSvParam_[14].p_metic = "load_5m";
    zpSvParam_[14].cb = zsv_cb_load;

    zpSvParam_[15].p_metic = "load_15m";
    zpSvParam_[15].cb = zsv_cb_load;

    zpSvParam_[16].p_metic = "diskusage_total";
    zpSvParam_[16].cb = zsv_cb_disk_total_spent;

    zpSvParam_[17].p_metic = "diskusage_used";
    zpSvParam_[17].cb = zsv_cb_disk_total_spent;

    zpSvParam_[18].p_metic = "disk_readbytes";
    zpSvParam_[18].cb = zsv_cb_diskiokb;

    zpSvParam_[19].p_metic = "disk_writebytes";
    zpSvParam_[19].cb = zsv_cb_diskiokb;

    zpSvParam_[20].p_metic = "disk_readiops";
    zpSvParam_[20].cb = zsv_cb_default;

    zpSvParam_[21].p_metic = "disk_writeiops";
    zpSvParam_[21].cb = zsv_cb_default;

    zpSvParam_[22].p_metic = "networkin_rate";
    zpSvParam_[22].cb = zsv_cb_netiokb;

    zpSvParam_[23].p_metic = "networkout_rate";
    zpSvParam_[23].cb = zsv_cb_netiokb;

    zpSvParam_[24].p_metic = "networkin_packages";
    zpSvParam_[24].cb = zsv_cb_default;

    zpSvParam_[25].p_metic = "networkout_packages";
    zpSvParam_[25].cb = zsv_cb_default;

    for (i = 0; i < zSplitCnt; i++) {
        for (j = 0; j < 26; j++) {
            zCHECK_PT_ERR(pthread_create((zTid + i)[j], NULL, zget_sv_ops, & zpSvParam_[i]));
        }
    }

    for (i = 0; i < zSplitCnt; i++) {
        for (j = 0; j < 26; j++) {
            pthread_join(zTid[i][j], NULL);
        }
    }
}

static void *
zsync_one_region(void *zp) {
    struct zRegion__ *zpRegion_ = zp;

    /* 实例、磁盘、网卡的基本信息 */
    zget_meta_one_region(zpRegion_);

    // TODO 多磁盘、网卡关联

    /* 与基本信息对应的监控信息 */
    zget_sv_one_region(zpRegion_);

    return NULL;
}

static void
zwrite_db(void) {
    // TODO
}

static void
zdata_sync(void) {
    // TODO 从 DB 中取最新的 zPrevStamp
    // 若为空，则赋值为 time(NULL) / (15 * 60) * 15 * 60 * 1000 - 15 * 60 * 1000

    while (time(NULL) * 1000 > (zPrevStamp + 15 * 60 * 1000 - 1)) {
        zmem_pool_init();

        pthread_t zTid[sizeof(zRegion_) / sizeof(struct zRegion__)];
        _i zThreadCnt = 0,
           i;

        /* 分区域并发执行 */
        for (i = 0; i < (_i) (sizeof(zRegion_) / sizeof(struct zRegion__)); i++) {
            /* 过滤掉数据为空的区域 */
            if (0 == zRegion_[i].ecsCnt) {
                continue;
            } else {
                zCHECK_PT_ERR(pthread_create(zTid + zThreadCnt, NULL, zsync_one_region, & zRegion_[i]));
                zThreadCnt++;
            }
        }

        for (i = 0; i < zThreadCnt; i++) {
            pthread_join(zTid[i], NULL);
        }

        /* 将最终结果写入 DB */
        zwrite_db();

        zmem_pool_destroy();

        zPrevStamp += 15 * 60 * 1000;
    }
}

#undef zINSTANCE_ID_BUF_LEN
#undef zHASH_KEY_SIZ
#undef zSV_MEM_POOL_SIZ
