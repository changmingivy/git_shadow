#include "zaliyun_ecs.h"

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

static void zenv_init(void);
static void zdb_mgmt(void);
static void zdata_sync(void);

extern struct zRun__ zRun_;
extern struct zPgSQL__ zPgSQL_;
extern struct zThreadPool__ zThreadPool_;
extern struct zNativeUtils__ zNativeUtils_;

extern zRepo__ *zpRepo_;

/* public interface */
struct zSvAliyunEcs__ zSvAliyunEcs_ = {
    .init = zenv_init,
    .data_sync = zdata_sync,
    .tb_mgmt = zdb_mgmt,
};

static const char * const zpUtilPath = "/tmp/aliyun_cmdb";
static const char * const zpAliyunID = "LTAIHYRtkSXC1uTl";
static const char * const zpAliyunKey = "l1eLkvNkVRoPZwV9jwRpmq1xPOefGV";

static struct zRegion__ zRegion_[17] = {
    {"cn-qingdao", 0, 0},
    {"cn-beijing", 0, 1},
    {"cn-zhangjiakou", 0, 2},
    {"cn-huhehaote", 0, 3},
    {"cn-hangzhou", 0, 4},
    {"cn-shanghai", 0, 5},
    {"cn-shenzhen", 0, 6},
    {"cn-hongkong", 0, 7},

    {"ap-southeast-1", 0, 8},
    {"ap-southeast-2", 0, 9},
    {"ap-southeast-3", 0, 10},
    {"ap-south-1", 0, 11},
    {"ap-northeast-1", 0, 12},

    {"us-west-1", 0, 13},
    {"us-east-1", 0, 14},

    {"eu-central-1", 0, 15},

    {"me-east-1", 0, 16},
};

/*
 * 上一次同步数据的毫秒时间戳（UNIX timestamp * 1000 之后的值）上限 + 1，
 * aliyun 查询时的时间区间是前后均闭合的，故使用时将上限设置为 15s 边界数减 1ms，
 * 以确保区间查询统一为前闭后开的形式，避免数据重复
 */
static _ll zPrevStamp;  /* 15000 毫秒的整数倍 */

/*
 * 定制专用的内存池：开头留一个指针位置，
 * 用于当内存池容量不足时，指向下一块新开辟的内存区
 */
static struct zMemPool__ *zpMemPool_;
static size_t zMemPoolOffSet;
static pthread_mutex_t zMemPoolLock = PTHREAD_MUTEX_INITIALIZER;

/* 各 region 范围内并发插入实例节点时所用 */
static pthread_mutex_t zNodeInsertLock = PTHREAD_MUTEX_INITIALIZER;

/* 生成元数据时使用线性线结构，匹配监控数据时再转换为 HASH */
static struct zSvEcs__ *zpHead_[sizeof(zRegion_) / sizeof(struct zRegion__)] = {NULL};
static struct zSvEcs__ *zpTail_[sizeof(zRegion_) / sizeof(struct zRegion__)] = {NULL};

/* HASH */
#define zHASH_SIZ 511
static struct zSvEcs__ *zpSvHash_[sizeof(zRegion_) / sizeof(struct zRegion__)][zHASH_SIZ] = {{NULL}};


static void
zmem_pool_init(void) {
    zCHECK_PT_ERR(pthread_mutex_init(&zMemPoolLock, NULL));

    zpMemPool_ = malloc(sizeof(struct zMemPool__) + zSV_MEM_POOL_SIZ);
    if (NULL == zpMemPool_) {
        zPRINT_ERR_EASY_SYS();
        exit(1);
    }

    zpMemPool_->p_prev = NULL;
    zMemPoolOffSet = 0;
}

static void
zmem_pool_destroy(void) {
    pthread_mutex_lock(&zMemPoolLock);

    struct zMemPool__ *zpTmp_;
    while(NULL != zpMemPool_) {
        zpTmp_ = zpMemPool_;
        zpMemPool_ = zpMemPool_->p_prev;
        free(zpTmp_);
    }

    zpMemPool_ = NULL;
    zMemPoolOffSet = 0;

    pthread_mutex_unlock(&zMemPoolLock);
    pthread_mutex_destroy(&zMemPoolLock);
}

static void *
zalloc(size_t zSiz) {
    pthread_mutex_lock(& zMemPoolLock);

    /* 检测当前内存池片区剩余空间是否充裕 */
    if ((zSiz + zMemPoolOffSet) > zSV_MEM_POOL_SIZ) {
        /* 请求的内存不能超过单个片区最大容量 */
        if (zSiz > zSV_MEM_POOL_SIZ) {
            pthread_mutex_unlock(&zMemPoolLock);
            zPRINT_ERR_EASY("req memory too large!");
            exit(1);
        }

        /* 新增一片内存，加入内存池 */
        struct zMemPool__ *zpNew_ =
            malloc(sizeof(struct zMemPool__) + zSV_MEM_POOL_SIZ);
        if (NULL == zpNew_) {
            zPRINT_ERR_EASY_SYS();
            exit(1);
        }

        zpNew_->p_prev = zpMemPool_;

        /* 内存池元信息更新 */
        zpMemPool_ = zpNew_;
        zMemPoolOffSet = 0;
    }

    /* 分配内存 */
    void *zpX = zpMemPool_->pool + zMemPoolOffSet;
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
            "CREATE TABLE IF NOT EXISTS sv_aliyun_ecs "
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
            "cpu_rate        int NOT NULL,"
            "mem_rate        int NOT NULL,"

            /*
             * 磁盘使用率：
             * 此项指标需要取回所有磁盘的已使用空间绝对大小与总空间大小，
             * 之后计算得出最终结果（以百分值的 1000 倍存储），不能直接取阿里云的比率
             */
            "disk_rate       int NOT NULL,"

            /*
             * 阿里云返回的值与 top 命令显示的格式一致，如：3.88，
             * 乘以 1000，之后除以 CPU 核心数，以整数整式保留一位小数的精度，
             * = 1000 * system load average recent 1 mins/ 5 mins / 15mins
             */
            "load_1m            int NOT NULL,"
            "load_5m            int NOT NULL,"
            "load_15m           int NOT NULL,"

            /*
             * 同一主机可能拥有多个网卡、磁盘，
             * 仅保留所有设备之和，不存储名细
             */
            "disk_rdB        int NOT NULL,"  /* io_read bytes */
            "disk_wrB        int NOT NULL,"  /* io_write bytes */
            "disk_rdiops     int NOT NULL,"  /* io_read tps */
            "disk_wriops     int NOT NULL,"  /* io_write tps */
            "net_rdB         int NOT NULL,"  /* 原始值除以 8，io_read bytes/B */
            "net_wrB         int NOT NULL,"  /* 原始值除以 8，io_write bytes/B */
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
                "CREATE TABLE IF NOT EXISTS sv_aliyun_ecs_%d "
                "PARTITION OF sv_aliyun_ecs FOR VALUES FROM (%d) TO (%d);",
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
                "CREATE TABLE IF NOT EXISTS sv_aliyun_ecs_%d "
                "PARTITION OF sv_aliyun_ecs FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zBuf, 0, NULL, 0);
    }

    /* 清除 30 * 24 小时之前连续 10 * 24 小时分区表 */
    zBaseID -= 30 * 24;
    for (zID = 0; zID < 10 * 24; zID++) {
        sprintf(zBuf,
                "DROP TABLE IF EXISTS sv_aliyun_ecs_%d;",
                zBaseID - zID);

        zPgSQL_.write_db(zBuf, 0, NULL, 0);
    }
}

/******************
 * 如下：提取数据 *
 ******************/
/* pBuf 相当于此宏的 “返回值” */
#define zGET_CONTENT(pCmd) ({\
    FILE *pFile = popen((pCmd), "r");\
    if (NULL == pFile) {\
        zPRINT_ERR_EASY_SYS();\
        exit(1);\
    }\
\
    /* 最前面的 12 个字符，是以 golang %-12d 格式打印的接收到的字节数 */\
    char CntBuf[12];\
    char *pBuf = NULL;\
    _i Cnter = 0;\
    /* 若 http body 数据异常，至多重试 10 次 */\
    for (_i i = 0; i < 10 && 12 != (Cnter = zNativeUtils_.read_hunk(CntBuf, 12, pFile)); i++) {\
        pclose(pFile);\
        pFile = popen((pCmd), "r");\
        if (NULL == pFile) {\
            zPRINT_ERR_EASY_SYS();\
            exit(1);\
        }\
    }\
    if (12 == Cnter) {\
        CntBuf[11] = '\0';\
        _i Len = strtol(CntBuf, NULL, 10);\
\
        pBuf = zalloc(1 + Len);\
\
        zNativeUtils_.read_hunk(pBuf, Len, pFile);\
        pBuf[Len] = '\0';\
    } else {\
        zPRINT_ERR_EASY("aliyun http req err!");\
    }\
\
    pclose(pFile);\
    pBuf;\
})

static _i
znode_parse_and_insert(void *zpJTransRoot, char *zpContent, _i zRegionID) {
    cJSON *zpJRoot = NULL,
          *zpJTmp = NULL,
          *zpJ = NULL;

    _i zErrNo = 0;
    struct zSvEcs__ *zpSv_ = NULL;

    if (NULL == zpContent) {
        zPRINT_ERR_EASY("");
        return -1;
    }

    if (NULL == zpJTransRoot) {
        zpJRoot = cJSON_Parse(zpContent);
        if (NULL == zpJRoot) {
            zPRINT_ERR_EASY(zpContent);
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
            zpSv_ = zalloc(sizeof(struct zSvEcs__));
            zpSv_->p_next = NULL;
            memset(zpSv_->svData_, 0, 60 * sizeof(struct zSvData__));

            /* 同时复制末尾的 '\0'，共计 23 bytes；剩余空间清零 */
            memcpy(zpSv_->id, zpJTmp->valuestring, 23);
            memset(zpSv_->id + 23, '\0', zINSTANCE_ID_BUF_LEN - 23);

            /* 提取 cpu 核心数 */
            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "Cpu");
            if (cJSON_IsNumber(zpJTmp)) {
                zpSv_->cpuNum = zpJTmp->valueint;
            } else {
                zpSv_->cpuNum = 1;  // 默认值 1
            }

            /* insert new node */
            pthread_mutex_lock(& zNodeInsertLock);
            if (NULL == zpHead_[zRegionID]) {
                zpHead_[zRegionID] = zpSv_;
                zpTail_[zRegionID] = zpHead_[zRegionID];
            } else {
                zpTail_[zRegionID]->p_next = zpSv_;
                zpTail_[zRegionID] = zpTail_[zRegionID]->p_next;
            }
            pthread_mutex_unlock(& zNodeInsertLock);
        } else {
            zPRINT_ERR_EASY("InstanceId ?");
            zErrNo = -1;
        }
    }

zEndMark:
    cJSON_Delete(zpJRoot);
    return zErrNo;
}

static void *
zget_meta_one_page(void *zp) {
    znode_parse_and_insert(NULL, zGET_CONTENT((char *)zp + sizeof(_i)), ((_i *) zp)[0]);
    return NULL;
}

static void *
zget_meta_one_region(void *zp) {
    struct zRegion__ *zpRegion_  = zp;
    char *zpContent = NULL;
    char zCmdBuf[512];

    _i zOffSet = 0;

    _i zPageTotal = 0,
       zPageNum = 0;

    cJSON *zpJRoot = NULL,
          *zpJ = NULL;

    char *zpThreadBuf = NULL;

    ((_i *)zCmdBuf)[0] = zpRegion_->id;

    /* 固定不变的参数 */
    zOffSet = sizeof(_i);
    zOffSet += snprintf(zCmdBuf + sizeof(_i), 512 - sizeof(_i),
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
    zpContent = zGET_CONTENT(zCmdBuf + sizeof(_i));

    zpJRoot = cJSON_Parse(zpContent);
    if (NULL == zpJRoot) {
        zPRINT_ERR_EASY("");
        return (void *) -1;
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
        return (void *) -1;
    }

    /* first page */
    znode_parse_and_insert(zpJRoot, zpContent, zpRegion_->id);

    /* multi pages */
    if (1 < zPageTotal) {
        zpThreadBuf = zalloc((zPageTotal - 1) * (zOffSet + 24));
        pthread_t zTid[zPageTotal - 1];

        for (zPageNum = 2; zPageNum <= zPageTotal; ++zPageNum) {
            memcpy(zpThreadBuf, zCmdBuf, zOffSet);
            snprintf(zpThreadBuf + zOffSet, 24, "PageNumber %d", zPageNum);

            zCHECK_PT_ERR(pthread_create(zTid + zPageNum - 2, NULL, zget_meta_one_page, zpThreadBuf));

            zpThreadBuf += (zOffSet + 24);  // must here!
        }

        for (zPageNum = 2; zPageNum <= zPageTotal; zPageNum++) {
            pthread_join(zTid[zPageNum - 2], NULL);
        }
    }

    return NULL;
}

/*
 * 通用的 ECS 监控数据处理函数
 */
static void *
zget_sv_ops(void *zp) {
    _i zOffSet;
    char *zpBuf = NULL,
         *zpContent = NULL,
         *zpCursor = NULL;

    cJSON *zpJRoot = NULL,
          *zpJTmp = NULL,
          *zpJ = NULL;

    _i zTimeStamp = 0,
       zBufSiz = 0,
       i;

    _d zData = 0;

    struct zSvParam__ *zpSvParam_ = zp;

    struct zSvEcs__ *zpSv_ = NULL;

    union zInstanceId__ zInstanceId_;

    zBufSiz = 1024 + strlen(zpSvParam_->p_paramSolid->p_dimensions);
    zpBuf = zalloc(zBufSiz);

    /* 固定不变的参数，第二次查询开始，将在末尾追加/更新游标 */
    zOffSet = snprintf(zpBuf, zBufSiz,
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
            "StartTime %lld "
            "EndTime %lld "
            "Dimensions %s ",
            zpUtilPath,
            zRegion_[zpSvParam_->p_paramSolid->regionID].p_name,
            zpAliyunID,
            zpAliyunKey,
            zpSvParam_->p_metic,
            zPrevStamp,
            zPrevStamp + 15 * 60 * 1000 - 1,
            zpSvParam_->p_paramSolid->p_dimensions);

    zpJRoot = NULL;
    do {
        if (NULL != zpJRoot) {
            snprintf(zpBuf + zOffSet, zBufSiz - zOffSet,
                    "Cursor %s", zpCursor);

            /* 前一次的 cJSON 资源已使用完毕，清除之 */
            cJSON_Delete(zpJRoot);
            zpJRoot = NULL;
        }

        zpContent = zGET_CONTENT(zpBuf);

        zpJRoot = cJSON_Parse(zpContent);
        if (NULL == zpJRoot) {
            zPRINT_ERR_EASY("");
            return (void *) -1;
        }

        zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJRoot, "Datapoints");
        if (NULL == zpJTmp) {
            cJSON_Delete(zpJRoot);

            zPRINT_ERR_EASY("Datapoints ?");
            return (void *) -1;
        }

        for (zpJ = zpJTmp->child; NULL != zpJ; zpJ = zpJ->next) {
            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "timestamp");
            if (cJSON_IsNumber(zpJTmp)) {
                /* 修正时间戳，按 15s 对齐 */
                zTimeStamp = zpJTmp->valuedouble / (15 * 1000);
            } else {
                zPRINT_ERR_EASY("timestamp ?");
                continue;
            }

            zpJTmp = cJSON_GetObjectItemCaseSensitive(zpJ, "instanceId");
            if (cJSON_IsString(zpJTmp)) {
                memcpy(zInstanceId_.id, zpJTmp->valuestring, 23);
                memset(zInstanceId_.id + 23, '\0', zINSTANCE_ID_BUF_LEN - 23);
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

            for (zpSv_ = zpSvHash_[zpSvParam_->p_paramSolid->regionID][zInstanceId_.hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ];
                    NULL != zpSv_;
                    zpSv_ = zpSv_->p_next) {
                for (i = zHASH_KEY_SIZ - 1; i >= 0; i--) {
                    if (zpSv_->hashKey[i] != zInstanceId_.hashKey[i]) {
                        goto zNextMark;
                    }
                }

                zpSv_->svData_[zTimeStamp % 60].timeStamp = 15 * zTimeStamp;
                zpSvParam_->cb(& ((_i *)(& zpSv_->svData_[zTimeStamp % 60]))[zpSvParam_->targetID], zData);

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
zsv_cb_netio_bytes(_i *zpBase, _f zNew) {
    (*zpBase) += zNew / 8;
}

/* 监控信息按主机数量分组并发查询*/
#define zSPLIT_UNIT 50
#define zSPLIT_SIZE_BASE (1/*+\0*/ + (sizeof("'[]'") - 1/*-\0*/) + zSPLIT_UNIT * (sizeof(",{\"instanceId\":\"i-instanceIdinstanceId\"}") - 1/*-\0*/) - 1/*-,*/)
#define zSPLIT_SIZE_TCP_STATE(state) (zSPLIT_SIZE_BASE + zSPLIT_UNIT * (sizeof(",\"state\":\"\"") - 1/*-\0*/ + strlen(state)))
static void *
zget_sv_one_region(void *zp) {
    struct zRegion__ *zpRegion_ = zp;
    struct zSvParamSolid__ *zpBaseSolid;  /* 指向分组后的各区间数据(拼接好的字符串) */
    //static **zppSplitDisk;  /* 分组同上，但每个字段添加磁盘 device 过滤条件 */
    //static **zppSplitNetIf;  /* 分组同上，但每个字段添加网卡 device 过滤条件 */
    struct zSvParamSolid__ *zpTcpStateSolid[11];  /* 分组同上，分别查询 TCP 的 11 种状态关联的连接数量 */

    struct zSvEcs__ *zpTmp_[2] = {NULL};

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
    zpTmp_[0] = zpHead_[zpRegion_->id];
    for (j = 0; j < zSplitCnt; j++) {
        zpBaseSolid[j].regionID = zpRegion_->id;
        zpBaseSolid[j].p_dimensions = zalloc(zSPLIT_SIZE_BASE);

        zOffSet = sprintf(zpBaseSolid[j].p_dimensions, "'");

        for (k = 0; k < zSPLIT_UNIT && NULL != zpTmp_[0];
                k++, zpTmp_[0] = zpTmp_[0]->p_next) {
            zOffSet += sprintf(zpBaseSolid[j].p_dimensions + zOffSet,
                    ",{\"instanceId\":\"%s\"}",
                    zpTmp_[0]->id);
        }

        sprintf(zpBaseSolid[j].p_dimensions + zOffSet, "]'");
        zpBaseSolid[j].p_dimensions[1] = '[';
    }

    for (i = 0; i < 11; i++) {
        zpTcpStateSolid[i] = zalloc(zSplitCnt * sizeof(struct zSvParamSolid__));
        zpTmp_[0] = zpHead_[zpRegion_->id];
        for (j = 0; j < zSplitCnt; j++) {
            zpTcpStateSolid[i][j].regionID = zpRegion_->id;
            zpTcpStateSolid[i][j].p_dimensions = zalloc(zSPLIT_SIZE_TCP_STATE(zpTcpState[i]));

            zOffSet = sprintf(zpTcpStateSolid[i][j].p_dimensions, "'");

            for (k = 0; k < zSPLIT_UNIT && NULL != zpTmp_[0];
                    k++, zpTmp_[0] = zpTmp_[0]->p_next) {
                zOffSet += sprintf(zpTcpStateSolid[i][j].p_dimensions + zOffSet,
                        ",{\"instanceId\":\"%s\",\"state\":\"%s\"}",
                        zpTmp_[0]->id,
                        zpTcpState[i]);
            }

            sprintf(zpTcpStateSolid[i][j].p_dimensions + zOffSet, "]'");
            zpTcpStateSolid[i][j].p_dimensions[1] = '[';
        }
    }

    /* 将线性链表转换为 HASH 结构 */
    zpTmp_[0] = zpHead_[zpRegion_->id];
    while (NULL != zpTmp_[0]) {
        if (NULL == zpSvHash_[zpRegion_->id][zpTmp_[0]->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ]) {
            zpSvHash_[zpRegion_->id][zpTmp_[0]->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ] = zpTmp_[0];
        } else {
            zpTmp_[1] = zpSvHash_[zpRegion_->id][zpTmp_[0]->hashKey[zHASH_KEY_SIZ - 1] % zHASH_SIZ];
            while (NULL != zpTmp_[1]->p_next) {
                zpTmp_[1] = zpTmp_[1]->p_next;
            }

            zpTmp_[1]->p_next = zpTmp_[0];
        }

        zpTmp_[1] = zpTmp_[0];
        zpTmp_[0]= zpTmp_[0]->p_next;
        zpTmp_[1]->p_next = NULL;  // must! avoid loop!
    }

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
    zpSvParam_[18].cb = zsv_cb_default;

    zpSvParam_[19].p_metic = "disk_writebytes";
    zpSvParam_[19].cb = zsv_cb_default;

    zpSvParam_[20].p_metic = "disk_readiops";
    zpSvParam_[20].cb = zsv_cb_default;

    zpSvParam_[21].p_metic = "disk_writeiops";
    zpSvParam_[21].cb = zsv_cb_default;

    zpSvParam_[22].p_metic = "networkin_rate";
    zpSvParam_[22].cb = zsv_cb_netio_bytes;

    zpSvParam_[23].p_metic = "networkout_rate";
    zpSvParam_[23].cb = zsv_cb_netio_bytes;

    zpSvParam_[24].p_metic = "networkin_packages";
    zpSvParam_[24].cb = zsv_cb_default;

    zpSvParam_[25].p_metic = "networkout_packages";
    zpSvParam_[25].cb = zsv_cb_default;

    /* 定义动态栈空间存放 tid */
    pthread_t zTid[zSplitCnt][26];

    for (i = 0; i < zSplitCnt; i++) {
        for (j = 0; j < 26; j++) {
            zCHECK_PT_ERR(pthread_create(& zTid[i][j], NULL, zget_sv_ops, & zpSvParam_[j]));
        }
    }

    for (i = 0; i < zSplitCnt; i++) {
        for (j = 0; j < 26; j++) {
            pthread_join(zTid[i][j], NULL);
        }
    }

    return NULL;
}
#undef zSPLIT_UNIT
#undef zSPLIT_SIZE_BASE
#undef zSPLIT_SIZE_TCP_STATE

static void *
zwrite_db_worker(void *zp) {
    zPgSQL_.write_db(zp, 0, NULL, 0);

    free(zp);

    return NULL;
}

/* 提取并写入监控数据 */
static _i
zwrite_db(void) {
    struct zSvEcs__ *zpSv_;
    size_t zBufSiz = 64 * 1024 * 1024,
           zOffSet = 0,
           zBaseSiz = 0;
    char *zpBuf = NULL;

    _i i, j, k;

    if (NULL == (zpBuf = malloc(zBufSiz))) {
        zPRINT_ERR_EASY_SYS();
        exit(1);
    }

    zBaseSiz = zOffSet = sprintf(zpBuf,
            "INSERT INTO sv_aliyun_ecs "
            "(time_stamp,instance_id,"
            "cpu_rate,mem_rate,disk_rate,load_1m,load_5m,load_15m,"
            "disk_rdB,disk_wrB,disk_rdiops,disk_wriops,"
            "net_rdB,net_wrB,net_rdiops,net_wriops,"
            "tcp_state_cnt) "
            "VALUES ");

    for (i = 0; i < (_i)(sizeof(zRegion_) / sizeof(struct zRegion__)); i++) {
        for (j = 0; j < zHASH_SIZ; j++) {
            if (NULL == zpSvHash_[i][j]) {
                continue;
            } else {
                for (zpSv_ = zpSvHash_[i][j]; NULL != zpSv_; zpSv_ = zpSv_->p_next) {
                    for (k = 0; k < 60; k++) {
                        if (510 > (zBufSiz - zOffSet)) {
                            zpBuf[zOffSet - 1] = '\0';
                            zThreadPool_.add(zwrite_db_worker, zpBuf);

                            if (NULL == (zpBuf = malloc(zBufSiz))) {
                                zPRINT_ERR_EASY_SYS();
                                exit(1);
                            }

                            zOffSet = sprintf(zpBuf,
                                    "INSERT INTO sv_aliyun_ecs "
                                    "(time_stamp,instance_id,"
                                    "cpu_rate,mem_rate,disk_rate,load_1m,load_5m,load_15m,"
                                    "disk_rdkb,disk_wrkb,disk_rdiops,disk_wriops,"
                                    "net_rdkb,net_wrkb,net_rdiops,net_wriops,"
                                    "tcp_state_cnt) "
                                    "VALUES ");
                        }

                        if (0 != zpSv_->svData_[k].timeStamp) {
                            zOffSet += sprintf(zpBuf + zOffSet,
                                    "(%d,'%s',%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,'{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}'),",
                                    zpSv_->svData_[k].timeStamp,
                                    zpSv_->id,
                                    zpSv_->svData_[k].cpu,
                                    zpSv_->svData_[k].mem,
                                    0 == zpSv_->svData_[k].diskTotal ? 0 : 1000 * zpSv_->svData_[k].diskSpent / zpSv_->svData_[k].diskTotal,
                                    zpSv_->svData_[k].load[0] / zpSv_->cpuNum,
                                    zpSv_->svData_[k].load[1] / zpSv_->cpuNum,
                                    zpSv_->svData_[k].load[2] / zpSv_->cpuNum,
                                    zpSv_->svData_[k].disk_rdkb,
                                    zpSv_->svData_[k].disk_wrkb,
                                    zpSv_->svData_[k].disk_rdiops,
                                    zpSv_->svData_[k].disk_wriops,
                                    zpSv_->svData_[k].net_rdkb,
                                    zpSv_->svData_[k].net_wrkb,
                                    zpSv_->svData_[k].net_rdiops,
                                    zpSv_->svData_[k].net_wriops,
                                    zpSv_->svData_[k].tcpState[0],
                                    zpSv_->svData_[k].tcpState[1],
                                    zpSv_->svData_[k].tcpState[2],
                                    zpSv_->svData_[k].tcpState[3],
                                    zpSv_->svData_[k].tcpState[4],
                                    zpSv_->svData_[k].tcpState[5],
                                    zpSv_->svData_[k].tcpState[6],
                                    zpSv_->svData_[k].tcpState[7],
                                    zpSv_->svData_[k].tcpState[8],
                                    zpSv_->svData_[k].tcpState[9],
                                    zpSv_->svData_[k].tcpState[10]);
                        }
                    }
                }
            }
        }
    }

    /* 检测最后是否有数据需要写入 DB */
    if (zOffSet > zBaseSiz) {
        zpBuf[zOffSet - 1] = '\0';
        zThreadPool_.add(zwrite_db_worker, zpBuf);
    }

    /* update timestamp (per 15 * 60 * 1000 ms) */
    char zBuf[256];
    sprintf(zBuf,
            "UPDATE sv_sync_meta SET last_timestamp = %lld",
            (zPrevStamp / 1000 + 15 * 60) / 15);

    zPgSQL_.write_db(zBuf, 0, NULL, 0);

    return 0;
}

static void
zdata_sync(void) {
    /*
     * 从 DB 中提取最新的 zPrevStamp
     * 若为空，则赋值为执行当时之前 15 分钟的时间戳
     */
    zPgRes__ *zpPgRes_ = NULL;
    _i zErrNo = zPgSQL_.exec_once(
            zRun_.p_sysInfo_->pgConnInfo,
            "SELECT last_timestamp FROM sv_sync_meta;",
            &zpPgRes_);

    if (0 == zErrNo) {
        zPrevStamp = 15 * 1000 * strtol(zpPgRes_->tupleRes_[0].pp_fields[0], NULL, 10);
    } else if (-92 == zErrNo) {
        zPrevStamp = (time(NULL) / (15 * 60) - 1) * 15 * 60 * 1000;
    } else {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    while (time(NULL) * 1000 > (zPrevStamp + 15 * 60 * 1000 - 1)) {
        zmem_pool_init();

        pthread_t zTid[sizeof(zRegion_) / sizeof(struct zRegion__)];
        _i zThreadCnt = 0,
           i;

        /* 分区域并发执行 */
        for (i = 0; i < (_i) (sizeof(zRegion_) / sizeof(struct zRegion__)); i++) {
            zCHECK_PT_ERR(pthread_create(zTid + i, NULL, zget_meta_one_region, & zRegion_[i]));
        }

        for (i = 0; i < (_i) (sizeof(zRegion_) / sizeof(struct zRegion__)); i++) {
            pthread_join(zTid[i], NULL);
        }

        for (i = 0; i < (_i) (sizeof(zRegion_) / sizeof(struct zRegion__)); i++) {
            if (0 < zRegion_[i].ecsCnt) {
                zCHECK_PT_ERR(pthread_create(zTid + zThreadCnt, NULL, zget_sv_one_region, & zRegion_[i]));
                zThreadCnt++;
            }
        }

        for (i = 0; i < zThreadCnt; i++) {
            pthread_join(zTid[i], NULL);
        }

        zwrite_db();
        zPrevStamp += 15 * 60 * 1000;

        zmem_pool_destroy();
    }
}

#undef zINSTANCE_ID_BUF_LEN
#undef zHASH_KEY_SIZ
#undef zSV_MEM_POOL_SIZ
