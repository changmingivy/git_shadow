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

/* 云监控模块 DB 预建 */
static void
zsv_cloud_prepare(void) {
//    _i zErrNo = 0;
//
//    zErrNo = zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo,
//            "CREATE TABLE IF NOT EXISTS supervisor_log"
//            "("
//            "repo_id         smallint DEFAULT 0,"  /* 所属项目 ID */
//            "ip              inet NOT NULL,"
//            "time_stamp      int NOT NULL,"
//            "loadavg5        int NOT NULL,"  /* system load average recent 5 mins */
//            "cpu_r           smallint NOT NULL,"  /* = 10000 * cpu usage rate */
//            "mem_r           smallint NOT NULL,"  /* = 10000 * mem usage rate */
//            "disk_io_rd_s    int NOT NULL,"  /* io_read tps spent */
//            "disk_io_wr_s    int NOT NULL,"  /* io_write tps spent */
//            "net_io_rd_s     int NOT NULL,"  /* net_read tps spent */
//            "net_io_wr_s     int NOT NULL,"  /* net_wirte tps spent */
//            "disk_mu         smallint NOT NULL"  /* = 100 * disk max usage rate: 每次只提取磁盘使用率最高的一个磁盘或分区的使用率，整数格式 0-100，代表 0% - 100% */
//            ") PARTITION BY RANGE (time_stamp);",
//            NULL);
//
//    if (0 != zErrNo) {
//        zPRINT_ERR_EASY("");
//        exit(1);
//    }
//
//    /* 每次启动时尝试创建必要的表，按小时分区（1天 == 3600秒） */
//    /* 预建 10 天的分区表，没有指明 repo_id 监控数据，一律存放于 0 号表 */
//    char zSQLBuf[512];
//    _i zBaseID = time(NULL) / 3600;
//    for (_i zID = 0; zID < 10 * 24; zID++) {
//        sprintf(zSQLBuf,
//                "CREATE TABLE IF NOT EXISTS supervisor_log_%d "
//                "PARTITION OF supervisor_log FOR VALUES FROM (%d) TO (%d) "
//                "PARTITION BY LIST (repo_id);",
//                zBaseID + zID,
//                3600 * (zBaseID + zID),
//                3600 * (zBaseID + zID + 1));
//
//        zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);
//    }
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
