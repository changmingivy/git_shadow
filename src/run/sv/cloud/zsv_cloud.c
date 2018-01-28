#include "zsv_cloud.h"

#include <sys/types.h>
#include <unistd.h>

#include <errno.h>

extern struct zRun__ zRun_;
extern struct zPgSQL__ zPgSQL_;

extern zRepo__ *zpRepo_;

static void zsv_cloud_prepare(void);
static void zcloud_data_sync(void);

/* 对外接口 */
struct zSVCloud__ zSVCloud_ = {
    .init = zsv_cloud_prepare,
    .data_sync = zcloud_data_sync,
};

struct zEcsDisk__ {
    char *p_name;
    struct zEcsDisk__ *p_next;
};

struct zEcsNetIf__ {
    char *p_name;
    struct zEcsNetIf__ *p_next;
};

struct zEcsSv__ {
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

// TODO 开辟专用的内存池，大小 8M，批量取完数据后，一次性释放
struct zEcsData__ {
    /*
     * 以链表形式集齐实例所有 device 的名称，
     * 用于查询对应设备的监控数据
     */
    struct zEcsDisk__ disk;
    struct zEcsNetIf__ netIf;

    /* 并发提取数据时需要的多线程游标 */
    struct zEcsDisk__  *p_disk;
    struct zEcsNetIf__ *p_netIf;

    /* HASH KEY */
    union {
#define zHASH_KEY_SIZ (1 + 23 / sizeof(_ull))  // instanceID(22 char) + '\0'
        _ull hashKey[zHASH_KEY_SIZ];
        char id[sizeof(_ull) * zHASH_KEY_SIZ];
    };

    /* 以 900 秒（15 分钟）为周期同步监控数据，单机数据条数：60 */
    struct zEcsSv__ ecsSv_[60];

    struct zEcsData__ *p_next;
};

/* 云监控模块 DB 预建 */
static void
zsv_cloud_prepare(void) {
    _i zErrNo = 0;

    zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo,
            "CREATE TABLE IF NOT EXISTS sv_ecs_aliyun"
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
             * 阿里云返回的值与 top 命令显示的格式一致，如：3.88，
             * 乘以 1000，之后除以 CPU 核心数，以整数整式保留一位小数的精度，
             * 不能乘以 10000，可以会导致 smallint 溢出。
             * = 1000 * system load average recent 1 mins/ 5 mins / 15mins
             */
            "load_1m            smallint NOT NULL,"
            "load_5m            smallint NOT NULL,"
            "load_15m           smallint NOT NULL,"

            /* 分别处于 tcp 的 11 种状态的连接计数 */
            "tcp_state_cnt          int[11] NOT NULL,"

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

            /*
             * 磁盘使用率：
             * 此项指标需要取回所有磁盘的已使用空间绝对大小与总空间大小，
             * 之后计算得出最终结果（以百分值的 1000 倍存储），不能直接取阿里云的比率
             */
            "disk_rate       smallint NOT NULL"
            ") PARTITION BY RANGE (time_stamp);",
            NULL);

    if (0 != zErrNo) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    /* 每次启动时尝试创建必要的表，按小时分区（1天 == 3600秒） */
    /* 预建 10 天的分区表，没有指明 repo_id 监控数据，一律存放于 0 号表 */
    char zSQLBuf[512];
    _i zBaseID = time(NULL) / 3600;
    for (_i zID = 0; zID < 10 * 24; zID++) {
        sprintf(zSQLBuf,
                "CREATE TABLE IF NOT EXISTS supervisor_log_%d "
                "PARTITION OF supervisor_log FOR VALUES FROM (%d) TO (%d) "
                "PARTITION BY LIST (repo_id);",
                zBaseID + zID,
                3600 * (zBaseID + zID),
                3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);
    }
}

/*
 * 定期扩展 DB 云监控分区表，
 * 并同步云上数据至本地数据库
 */
static void
zcloud_data_sync(void) {
//    _i zBaseID = 0,
//       zID = 0;
//
//    char zBuf[512];
//
//    /* 创建之后 10 * 24 小时的 sv_cloud_log 分区表 */
//    zBaseID = time(NULL) / 3600;
//    for (zID = 0; zID < 10 * 24; zID++) {
//        sprintf(zBuf,
//                "CREATE TABLE IF NOT EXISTS supervisor_log_%d "
//                "PARTITION OF supervisor_log FOR VALUES FROM (%d) TO (%d) "
//                "PARTITION BY LIST (repo_id);"
//                "CREATE TABLE supervisor_log_%d_0 "
//                "PARTITION OF supervisor_log_%d FOR VALUES IN (0);",
//                zBaseID + zID,
//                3600 * (zBaseID + zID),
//                3600 * (zBaseID + zID + 1),
//                zBaseID + zID,
//                zBaseID + zID);
//
//        zPgSQL_.write_db(zBuf, 0, NULL, 0);
//    }
//
//    /* 清除 30 * 24 小时之前连续 10 * 24 小时的 supervisor_log 分区表 */
//    for (zID = 0; zID < 10 * 24; zID++) {
//        sprintf(zBuf,
//                "DROP TABLE IF EXISTS supervisor_log_%d;",
//                (zBaseID - 30 * 24) - zID);
//
//        zPgSQL_.write_db(zBuf, 0, NULL, 0);
//    }
//
//    // TODO 拉取 aliyun，存入 DB
}
