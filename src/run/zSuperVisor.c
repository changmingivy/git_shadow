#include "zSuperVisor.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

extern struct zRun__ zRun_;
extern struct zThreadPool__ zThreadPool_;
extern struct zPgSQL__ zPgSQL_;

extern zRepo__ *zpRepo_;

static void * zsupervisor_prepare(void *zp);
static _i zwrite_db_supersivor(void *zp, _i zSd, struct sockaddr *zpPeerAddr, socklen_t zPeerAddrLen);
static void * zwrite_db_thread_wraper(void *zp);

/* 监控数据以 8M 为单位，批量写入 */
#define zDB_POOL_BUF_SIZ 8192 * 1024
static pthread_mutex_t zDBPoolBufLock = PTHREAD_MUTEX_INITIALIZER;
static char *zpOptiBuf = NULL;
static _i zOptiDataLen = 0;

/* 对外接口 */
struct zSuperVisor__ zSuperVisor_ = {
    .init = zsupervisor_prepare,
    .write_db = zwrite_db_supersivor,
};

/* 缓存区满后，启用新线程写入磁盘并释放内存 */
static void *
zwrite_db_thread_wraper(void *zp) {
    zPgSQL_.write_db(zp, 0, NULL, 0);

    free(zp);

    return NULL;
}

/*
 * 缓冲的数据量达到 zDB_POOL_BUF_SIZ - 510 时，执行批量写入
 * udp 接收的缓冲区大小 510，
 */
static _i
zwrite_db_supersivor(void *zp,
        _i zSd __attribute__ ((__unused__)),
        struct sockaddr *zpPeerAddr __attribute__ ((__unused__)),
        socklen_t zPeerAddrLen __attribute__ ((__unused__))) {

    pthread_mutex_lock(& zDBPoolBufLock);

    if ((zDB_POOL_BUF_SIZ - 510) < zOptiDataLen) {
        /* 去除最后一个逗号 */
        zpOptiBuf[zOptiDataLen - 1] = '\0';

        /* 缓存区满后，启用新线程写入磁盘 */
        zThreadPool_.add(zwrite_db_thread_wraper, zpOptiBuf);

        zMEM_ALLOC(zpOptiBuf, char, zDB_POOL_BUF_SIZ);

        strcpy(zpOptiBuf, "INSERT INTO supervisor_log VALUES ");
        zOptiDataLen = sizeof("INSERT INTO supervisor_log VALUES ") - 1;
    }

    zOptiDataLen += sprintf(zpOptiBuf + zOptiDataLen, "%s", zp);

    pthread_mutex_unlock(& zDBPoolBufLock);

    return 0;
}


/* 监控模块 DB 预建 */
static void *
zsupervisor_prepare(void *zp __attribute__ ((__unused__))) {
    /* +2 的意义: 防止恰好在临界时间添加记录导致异常 */
    _i zBaseID = time(NULL) / 3600 + 2;

    char zSQLBuf[512];

    if (0 != zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo,
            "CREATE TABLE IF NOT EXISTS supervisor_log"
            "("
            "ip              inet NOT NULL,"
            "time_stamp      bigint NOT NULL,"
            "cpu_t           bigint NOT NULL,"  /* cpu total */
            "cpu_s           bigint NOT NULL,"  /* cpu spent */
            "mem_t           int NOT NULL,"  /* mem total */
            "mem_s           int NOT NULL,"  /* mem spent */
            "disk_io_s       bigint NOT NULL,"  /* io spent */
            "net_io_s        bigint NOT NULL,"  /* net spent */
            "disk_mu         bigint NOT NULL,"  /* disk max usage: 每次只提取磁盘使用率最高的一个磁盘或分区的使用率，整数格式 0-100，代表 0% - 100% */
            "loadavg5        int NOT NULL"  /* system load average recent 5 mins */
            ") PARTITION BY RANGE (time_stamp);",
            NULL)) {

        zPRINT_ERR_EASY("");
        exit(1);
    }

    /* 每次启动时尝试创建必要的表，按小时分区（1天 == 3600秒） */
    sprintf(zSQLBuf,
            "CREATE TABLE IF NOT EXISTS supervisor_log_%d "
            "PARTITION OF supervisor_log FOR VALUES FROM (MINVALUE) TO (%d);",
            zBaseID, 3600 * zBaseID);

    zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);

    /* 预建 10 天的分区表 */
    for (_i zID = 0; zID < 10 * 24; zID++) {
        sprintf(zSQLBuf,
                "CREATE TABLE IF NOT EXISTS supervisor_log_%d "
                "PARTITION OF supervisor_log FOR VALUES FROM (%d) TO (%d);",
                zBaseID + zID + 1,
                3600 * (zBaseID + zID), 3600 * (zBaseID + zID + 1));

        zPgSQL_.write_db(zSQLBuf, 0, NULL, 0);
    }

    /* DB 缓存区初始化 */
    zMEM_ALLOC(zpOptiBuf, char, zDB_POOL_BUF_SIZ);
    strcpy(zpOptiBuf, "INSERT INTO supervisor_log VALUES ");
    zOptiDataLen = sizeof("INSERT INTO supervisor_log VALUES ") - 1;

    return NULL;
}
