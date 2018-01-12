#include "zPgSQL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "zRun.h"

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

static zPgConnHd__ * zpg_conn(const char *zpConnInfo);
static void zpg_conn_reset(zPgConnHd__ *zpPgConnHd_);
static zPgResHd__ * zpg_exec(zPgConnHd__ *zpPgConnHd_, const char *zpSQL, zbool_t zNeedRet);
static zPgResHd__ * zpg_exec_with_param(zPgConnHd__ *zpPgConnHd_, const char *zpCmd, _i zParamCnt, const char * const *zppParamValues, zbool_t zNeedRet);
static zPgResHd__ * zpg_prepare(zPgConnHd__ *zpPgConnHd_, const char *zpSQL, const char *zpPreObjName, _i zParamCnt);
static zPgResHd__ * zpg_prepare_exec(zPgConnHd__ *zpPgConnHd_, const char *zpPreObjName, _i zParamCnt, const char * const *zppParamValues, zbool_t zNeedRet);
static zPgRes__ * zpg_parse_res(zPgResHd__ *zpPgResHd_);

static void zpg_res_clear(zPgResHd__ *zpPgResHd_, zPgRes__ *zpPgRes_);
static void zpg_conn_clear(zPgConnHd__ *zpPgConnHd_);
static zbool_t zpg_conn_check(const char *zpConnInfo);
static zbool_t zpg_thread_safe_check();

static _i zpg_exec_once(char *zpConnInfo, char *zpCmd, zPgRes__ **zppPgRes_);
static _i zpg_exec_with_param_once(char *zpConnInfo, char *zpCmd, _i zParamCnt, const char **zppParam, zPgRes__ **zppPgRes_);

static void zpg_conn_pool_init(void);

static _i zwrite_db(void *zp, _i zSd __attribute__ ((__unused__)),
        struct sockaddr *zpPeerAddr __attribute__ ((__unused__)),
        socklen_t zPeerAddrLen __attribute__ ((__unused__)));

/* postgreSQL 连接池 */
#define zDB_POOL_SIZ 256
static pthread_mutex_t zDBPoolLock = PTHREAD_MUTEX_INITIALIZER;
static zPgConnHd__ *zpDBPool_[zDB_POOL_SIZ] = { NULL };
static _s zDBPoolStack[zDB_POOL_SIZ];
static _s zDBPoolStackHeader;

/* 数据以 8k 为单位，批量写入 */
static pthread_mutex_t zDBPoolBufLock = PTHREAD_MUTEX_INITIALIZER;
static char zOptiBuf[8192];
static _s zOptiDataLen = 0;


/*
 * Public 接口
 */
struct zPgSQL__ zPgSQL_ = {
    .conn = zpg_conn,
    .conn_reset = zpg_conn_reset,

    .exec = zpg_exec,
    .exec_with_param = zpg_exec_with_param,
    .prepare = zpg_prepare,
    .prepare_exec = zpg_prepare_exec,

    .parse_res = zpg_parse_res,

    .res_clear = zpg_res_clear,
    .conn_clear = zpg_conn_clear,

    .thread_safe_check = zpg_thread_safe_check,
    .conn_check = zpg_conn_check,

    .exec_once = zpg_exec_once,
    .exec_with_param_once = zpg_exec_with_param_once,

    .conn_pool_init = zpg_conn_pool_init,
    .write_db = zwrite_db,
};

/* DB 连接池初始化 */
static void
zpg_conn_pool_init(void) {
    zDBPoolStackHeader = zDB_POOL_SIZ - 1;

    for (_i i = 0; i < zDB_POOL_SIZ; i++) {
        zDBPoolStack[i] = i;

        zCHECK_NULL_EXIT(
                zpDBPool_[i] = zPgSQL_.conn(zRun_.p_sysInfo_->pgConnInfo)
                );
    }
}

/*
 * 连接 pgSQL server
 */
static zPgConnHd__ *
zpg_conn(const char *zpConnInfo) {
    zPgConnHd__ *zpPgConnHd_ = PQconnectdb(zpConnInfo);
    if (CONNECTION_OK == PQstatus(zpPgConnHd_)) {
        return zpPgConnHd_;
    } else {
        zPRINT_ERR_EASY(PQerrorMessage(zpPgConnHd_));
        PQfinish(zpPgConnHd_);
        return NULL;
    }
}


/*
 * 断线重连
 */
static void
zpg_conn_reset(zPgConnHd__ *zpPgConnHd_) {
    PQreset(zpPgConnHd_);
}


/*
 * 执行 SQL cmd
 * zNeedRet 为 zTrue 时，表时此 SQL 属于查询类，有结果需要返回
 * */
static zPgResHd__ *
zpg_exec(zPgConnHd__ *zpPgConnHd_, const char *zpSQL, zbool_t zNeedRet) {
    zPgResHd__ *zpPgResHd_ = PQexec(zpPgConnHd_, zpSQL);
    if ((zTrue == zNeedRet ? PGRES_TUPLES_OK : PGRES_COMMAND_OK) == PQresultStatus(zpPgResHd_)) {
        return zpPgResHd_;
    } else {
        zPRINT_ERR_EASY(PQresultErrorMessage(zpPgResHd_));
        PQclear(zpPgResHd_);
        return NULL;
    }
}


/*
 * 使用带外部参数的方式执行 SQL cmd
 */
static zPgResHd__ *
zpg_exec_with_param(zPgConnHd__ *zpPgConnHd_, const char *zpCmd, _i zParamCnt, const char * const *zppParamValues, zbool_t zNeedRet) {
    zPgResHd__ *zpPgResHd_ = PQexecParams(zpPgConnHd_, zpCmd, zParamCnt, NULL, zppParamValues, NULL, NULL, 0);
    if ((zTrue == zNeedRet ? PGRES_TUPLES_OK : PGRES_COMMAND_OK) == PQresultStatus(zpPgResHd_)) {
        return zpPgResHd_;
    } else {
        zPRINT_ERR_EASY(PQresultErrorMessage(zpPgResHd_));
        PQclear(zpPgResHd_);
        return NULL;
    }
}


/*
 * 预编译重复执行的 SQL cmd，加快执行速度
 */
static zPgResHd__ *
zpg_prepare(zPgConnHd__ *zpPgConnHd_, const char *zpSQL, const char *zpPreObjName, _i zParamCnt) {
    zPgResHd__ *zpPgResHd_ = PQprepare(zpPgConnHd_, zpPreObjName, zpSQL, zParamCnt, NULL);
    if (PGRES_COMMAND_OK == PQresultStatus(zpPgResHd_)) {
        return zpPgResHd_;
    } else {
        zPRINT_ERR_EASY(PQresultErrorMessage(zpPgResHd_));
        PQclear(zpPgResHd_);
        return NULL;
    }
}


/*
 * 使用预编译的 SQL 对象快速执行 SQL cmd
 */
static zPgResHd__ *
zpg_prepare_exec(zPgConnHd__ *zpPgConnHd_, const char *zpPreObjName, _i zParamCnt, const char * const *zppParamValues, zbool_t zNeedRet) {
    zPgResHd__ *zpPgResHd_ = PQexecPrepared(zpPgConnHd_, zpPreObjName, zParamCnt, zppParamValues, NULL, NULL, 0);
    if ((zTrue == zNeedRet ? PGRES_TUPLES_OK : PGRES_COMMAND_OK) == PQresultStatus(zpPgResHd_)) {
        return zpPgResHd_;
    } else {
        zPRINT_ERR_EASY(PQresultErrorMessage(zpPgResHd_));
        PQclear(zpPgResHd_);
        return NULL;
    }
}


/*
 * 解析并输出 SQL 查询类命令返回的结果
 * 返回的数据中，第一组是字段名称
 * 返回 NULL 表示查询结果为空
 */
static zPgRes__ *
zpg_parse_res(zPgResHd__ *zpPgResHd_) {
    _i zTupleCnt = 0,
       zFieldCnt = 0,
       i = 0,
       j = 0;
    zPgRes__ *zpPgRes_ = NULL;

    if (0 == (zTupleCnt = PQntuples(zpPgResHd_))) {
        return NULL;
    }
    zFieldCnt = PQnfields(zpPgResHd_);

    zMEM_ALLOC(zpPgRes_, char, sizeof(zPgRes__) + zTupleCnt * sizeof(zPgResTuple__)
            + zFieldCnt * sizeof(void *)  /* for zpPgRes_->fieldNames_.pp_fields */
            + zTupleCnt * (zFieldCnt * sizeof(void *)));  /* for zpPgRes_->tupleRes_[0].pp_fields */

    zpPgRes_->fieldNames_.pp_fields = (char **) (
            (char *) (zpPgRes_ + 1) + zTupleCnt * sizeof(zPgResTuple__)
            );

    zpPgRes_->tupleRes_[0].pp_fields = zpPgRes_->fieldNames_.pp_fields + zFieldCnt;

    zpPgRes_->tupleCnt = zTupleCnt;
    zpPgRes_->fieldCnt = zFieldCnt;

    /* 提取字段名称 */
    for (i = 0; i < zFieldCnt; i++) {
        zpPgRes_->fieldNames_.pp_fields[i] = PQfname(zpPgResHd_, i);
    }

    /* 提取每一行各字段的值 */
    for (j = 0; j < zTupleCnt; j++) {
        zpPgRes_->tupleRes_[j].pp_fields = zpPgRes_->tupleRes_[0].pp_fields + j * zFieldCnt;
        for (i = 0; i < zFieldCnt; i++) {
            zpPgRes_->tupleRes_[j].pp_fields[i] = PQgetvalue(zpPgResHd_, j, i);
        }
    }

    return zpPgRes_;
}


/*
 * 清理 SQL 查询结果相关资源
 * !!! zpPgRes_ 中引用了 zpPgResHd_ 中的数据，
 * 故两者必须同时清理，不能在清理前者后，继续使用后者 !!!
 */
static void
zpg_res_clear(zPgResHd__ *zpPgResHd_, zPgRes__ *zpPgRes_) {
    /* 顺序不可变 */
    if (NULL != zpPgResHd_) {
        PQclear(zpPgResHd_);
    };

    if (NULL != zpPgRes_) {
        free(zpPgRes_);
    }
}


/*
 * 清理 pgSQL 连接句柄
 */
static void
zpg_conn_clear(zPgConnHd__ *zpPgConnHd_) {
    if (NULL != zpPgConnHd_) {
        PQfinish(zpPgConnHd_);
    }
}


/*
 * 检查所在环境是否是线程安全的
 */
static zbool_t
zpg_thread_safe_check() {
    return 1 == PQisthreadsafe() ? zTrue : zFalse;
}


/*
 * 测试 pgSQL 服务器是否正常连接
 */
static zbool_t
zpg_conn_check(const char *zpConnInfo) {
    return PQPING_OK == PQping(zpConnInfo) ? zTrue : zFalse;
}


/*
 * 执行一次性 SQL cmd 的封装
 */
static _i
zpg_exec_once(char *zpConnInfo, char *zpCmd, zPgRes__ **zppPgRes_) {
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zpConnInfo))) {
        return -90;
    } else {
        if (NULL == (zpPgResHd_ = zPgSQL_.exec(
                        zpPgConnHd_,
                        zpCmd,
                        NULL == zppPgRes_ ? zFalse : zTrue))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            return -91;
        }

        if (NULL == zppPgRes_) {
            zpg_res_clear(zpPgResHd_, NULL);
        } else if (NULL == (*zppPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zpg_res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpPgConnHd_);
            return -92;
        } else {
            /* 必须在 parse_res 之后 */
            (*zppPgRes_)->p_pgResHd_ = zpPgResHd_;
        }

        zPgSQL_.conn_clear(zpPgConnHd_);
    }

    return 0;
}


static _i
zpg_exec_with_param_once(char *zpConnInfo, char *zpCmd, _i zParamCnt, const char **zppParam, zPgRes__ **zppPgRes_) {
    zPgConnHd__ *zpPgConnHd_ = NULL;
    zPgResHd__ *zpPgResHd_ = NULL;

    if (NULL == (zpPgConnHd_ = zPgSQL_.conn(zpConnInfo))) {
        return -90;
    } else {
        if (NULL == (zpPgResHd_ = zPgSQL_.exec_with_param(
                        zpPgConnHd_,
                        zpCmd,
                        zParamCnt,
                        zppParam,
                        NULL == zppPgRes_ ? zFalse : zTrue))) {
            zPgSQL_.conn_clear(zpPgConnHd_);
            return -91;
        }

        if (NULL == zppPgRes_) {
            zpg_res_clear(zpPgResHd_, NULL);
        } else if (NULL == (*zppPgRes_ = zPgSQL_.parse_res(zpPgResHd_))) {
            zpg_res_clear(zpPgResHd_, NULL);
            zPgSQL_.conn_clear(zpPgConnHd_);
            return -92;
        } else {
            /* 必须在 parse_res 之后 */
            (*zppPgRes_)->p_pgResHd_ = zpPgResHd_;
        }

        zPgSQL_.conn_clear(zpPgConnHd_);
    }

    return 0;
}


/*
 * 通用的只写动作，无返回内容
 * 原生写入，没有 prepared 优化
 */
static void
zwrite_db_real(char *zpCmd) {
    _i zKeepID;

    /* 出栈 */
    pthread_mutex_lock(& zDBPoolLock);

    while (0 > zDBPoolStackHeader) {
        pthread_mutex_unlock(& zDBPoolLock);
        sleep(1);
        pthread_mutex_lock(& zDBPoolLock);
    }

    zKeepID = zDBPoolStack[zDBPoolStackHeader];
    zDBPoolStackHeader--;

    pthread_mutex_unlock(& zDBPoolLock);

    /* 若执行失败，记录日志 */
    if (NULL == zPgSQL_.exec(zpDBPool_[zKeepID], zpCmd, zFalse)) {
        zRun_.write_log(zpCmd, 0, NULL, 0);
    }

    /* 入栈 */
    pthread_mutex_lock(& zDBPoolLock);
    zDBPoolStack[++zDBPoolStackHeader] = zKeepID;
    pthread_mutex_unlock(& zDBPoolLock);
}


/*
 * 首字符为空格的请求，执行缓存后批量写入
 * 否则即时写入
 */
static _i
zwrite_db(void *zp,
        _i zSd __attribute__ ((__unused__)),
        struct sockaddr *zpPeerAddr __attribute__ ((__unused__)),
        socklen_t zPeerAddrLen __attribute__ ((__unused__))) {

    char *zpCmd = zp;

    if (' ' == zpCmd[0]) {
        if (0 == pthread_mutex_trylock(& zDBPoolBufLock)) {
            /*
             * 缓冲的数据量达到 8192 - 509 时，执行批量写入
             * udp 接收的缓冲区大小 510，
             * 除去开头的索引字符，最多只会传递 509bytes
             */
            if ((8192 - 509) < zOptiDataLen) {
                zwrite_db_real(zOptiBuf);
                zOptiDataLen = 0;
            }

            zOptiDataLen += sprintf(zOptiBuf + zOptiDataLen, "%s", zpCmd + 1);

            pthread_mutex_unlock(& zDBPoolBufLock);
        } else {
            zwrite_db_real(zpCmd + 1);
        }
    } else {
        zwrite_db_real(zpCmd);
    }

    return 0;
}


// /*
//  * EXAMPLE!
//  * 编译：-lpq
//  */
// _i
// main(void) {
//     PGconn *zpConn;
//     PGresult *zpRes;
//     _i zFieldCnt,
//        zCnter[2];
//
//     /* hostaddr=192.168.1.2 password=xx */
//     const char *zpConnInfo =
//         "host=db.yixia.com"
//         "port=9527"
//         "user=admin"
//         "passfile=/$HOME/.pgpass"  // 文件权限必须设置为0600，每行的格式：hostname:port:database:username:password，井号#代表注释行
//         "dbname=dpDB"
//         "sslmode=allow"
//         "connect_timeout=10";
//
//     zpConn = PQconnectdb(zpConnInfo);
//
//     if (CONNECTION_OK != PQstatus(zpConn)) {
//         PQfinish(zpConn);
//     }
//
//     zpRes = PQexec(zpConn, "select * from a_table");  // 可以以分号分割同时执行多条SQL，但只会返回最后一条的输出内容；中间的任何指令执行出错，后续的指令将不被执行
//
//     if (PGRES_TUPLES_OK != PQresultStatus(zpRes)) {
//         // 代表命令功执行，并且其返回内容已全部接收完毕；无返回值的命令使用：PGRES_COMMAND_OK
//         // Note that a SELECT command that happens to retrieve zero rows still shows PGRES_TUPLES_OK. PGRES_COMMAND_OK is for commands that can never return rows (INSERT or UPDATE without a RETURNING clause, etc.)
//         PQclear(zpRes);
//         PQfinish(zpConn);
//     }
//
//     zFieldCnt = PQnfields(zpRes);
//     for (zCnter[0] = 0; zCnter[0] < zFieldCnt; zCnter[0]++) {
//         printf("field name: %-15s, field index: %d\n", PQfname(zpRes, zCnter[0]), PQfnumber(zpRes, PQfname(zpRes, zCnter[0])));  // zCnter == PQfnumber(zpRes, PQfname(zpRes, zCnter[0]))
//     }
//
//     for (zCnter[0] = 0; zCnter[0] < PQntuples(zpRes); zCnter[0]++) {
//         for (zCnter[1] = 0; zCnter[1] < zFieldCnt; zCnter[1]++) {
//             printf("%-15s\n", PQgetvalue(zpRes, zCnter[1], zCnter[0]));  // PQgetvalue will return an empty string, not a null pointer, for a null field
//         }
//     }
//
//     PQreset(zpConn);  // 尝试关闭连接后重新连接，用于连接中断重连场景
//
// // PGPing PQping(const char *conninfo);
// //
// // typedef enum
// // {
// //     PQPING_OK,                    /* server is accepting connections */
// //     PQPING_REJECT,                /* server is alive but rejecting connections */
// //     PQPING_NO_RESPONSE,            /* could not establish connection */
// //     PQPING_NO_ATTEMPT            /* connection not attempted (bad params) */
// // } PGPing;
//
// // ConnStatusType PQstatus(const PGconn *conn);
// // typedef enum
// // {
// //     CONNECTION_OK,
// //     CONNECTION_BAD,
// // } ConnStatusType;
//
// // char *PQresultErrorMessage(const PGresult *res);
// // char *PQerrorMessage(const PGconn *conn);  // 获取最近一次失败操作的错误信息
//
// // int PQgetlength(const PGresult *res, int row_number, int column_number);  // 根据行号和列号返回字段的长度，文本内容格式与 strlen() 返回的一样
// //
// // int PQisthreadsafe();  // 返回 1，表示当前运行环境中的PQ是线程安全的
// //  In particular, you cannot issue concurrent commands from different threads through the same connection object. (If you need to run concurrent commands, use multiple connections.)
//
//
// // 对于需要重复执行析 SQL 语句，可以生成一个预处理过的对象，而后重复使用，提升效率，类似于正则表达式的编译环节
// // 注：服务器端生成的此种对象无法通过 libpq 中的函数直接销毁！
// // PGresult *PQprepare(PGconn *conn,
// //                     const char *stmtName,
// //                     const char *query,  // SQL 语句，可以以 $1 $2 ... $N 的形式调用外部提供的参数
// //                     int nParams,  // 参数数量
// //                     const Oid *paramTypes);  // 参数类型，由于指定 OID 太麻烦，也可以直接在 SQL 语句中以  $1::bigint 的整式强制指定或不指定，由服务器推导类型，若使用这种风格，则此处可以置为 NULL
// //
// // 执行预处理过的 SQL 对象，指定对象名称，若为 NULL，则代表运行全局唯一的匿名对象
// // PGresult *PQexecPrepared(PGconn *conn,
// //                          const char *stmtName,
// //                          int nParams,  // 实际指定的参数数量
// //                          const char * const *paramValues,  // 每次运行时的实际参数值，每个参数的类型在预生成SQL对象时已经指定
// //                          const int *paramLengths,  // 文本字段该字段将被忽略，可以为任意值，二进制字段需要分别指定各自的长度
// //                          const int *paramFormats,  // 文本字段置 0，二进制置1，若指针置为 NULL，则以全部为文本字段处理
// //                          int resultFormat);  // 置 0 表示要求返回的结果是文本格式，置 1 则表示要求二进制格式的结果
//
//
// // 以命令行惯用的格式显示 SQL 命令返回的内容
// // void PQprint(FILE *fout,      /* 输出流 */
// //              const PGresult *res,
// //              const PQprintOpt *po);
// // typedef struct
// // {
// //     pqbool  header;      /* 打印输出域标题和行计数 */
// //     pqbool  align;       /* 填充对齐域 */
// //     pqbool  standard;    /* 旧的格式 */
// //     pqbool  html3;       /* 输出 HTML 表格 */
// //     pqbool  expanded;    /* 扩展表格 */
// //     pqbool  pager;       /* 如果必要为输出使用页 */
// //     char    *fieldSep;   /* 域分隔符 */
// //     char    *tableOpt;   /* 用于 HTML 表格元素的属性 */
// //     char    *caption;    /* HTML 表格标题 */
// //     char    **fieldName; /* 替换域名称的空终止数组 */
// // } PQprintOpt;
//
//     PQclear(zpRes);
//     PQfinish(zpConn);
// }
