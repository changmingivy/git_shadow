#include <libpq-fe.h>

#include "zCommon.h"


_i
main(void) {
    PGconn *zpConn;
    PGresult *zpRes;
    _i zFieldCnt,
       zCnter[2];

    /* hostaddr=192.168.1.2 password=xx */
    const char *zpConnInfo =
        "host=db.yixia.com"
        "port=9527"
        "user=admin"
        "passfile=/home/.pgpass"  // 文件权限必须设置为0600，每行的格式：hostname:port:database:username:password，井号#代表注释行
        "dbname=testdb"
        "sslmode=allow"
        "connect_timeout=10";

    zpConn = PQconnectdb(zpConnInfo);

    if (CONNECTION_OK != PQstatus(zpConn)) {
        PQfinish(zpConn);
    }

    zpRes = PQexec(zpConn, "select * from a_table");  // 可以以分号分割同时执行多条SQL，但只会返回最后一条的输出内容；中间的任何指令执行出错，后续的指令将不被执行

    if (PGRES_TUPLES_OK != PQresultStatus(zpRes)) {
        // 代表命令功执行，并且其返回内容已全部接收完毕；无返回值的命令使用：PGRES_COMMAND_OK
        // Note that a SELECT command that happens to retrieve zero rows still shows PGRES_TUPLES_OK. PGRES_COMMAND_OK is for commands that can never return rows (INSERT or UPDATE without a RETURNING clause, etc.)
        PQclear(zpRes);
        PQfinish(zpConn);
    }

    zFieldCnt = PQnfields(zpRes);
    for (zCnter[0] = 0; zCnter[0] < zFieldCnt; zCnter[0]++) {
        printf("field name: %-15s, field index: %d\n", PQfname(zpRes, zCnter[0]), PQfnumber(zpRes, PQfname(zpRes, zCnter[0])));  // zCnter == PQfnumber(zpRes, PQfname(zpRes, zCnter[0]))
    }

    for (zCnter[0] = 0; zCnter[0] < PQntuples(zpRes); zCnter[0]++) {
        for (zCnter[1] = 0; zCnter[1] < zFieldCnt; zCnter[1]++) {
            printf("%-15s\n", PQgetvalue(zpRes, zCnter[1], zCnter[0]));  // PQgetvalue will return an empty string, not a null pointer, for a null field
        }
    }

    PQreset(zpConn);  // 尝试关闭连接后重新连接，用于连接中断重连场景

// PGPing PQping(const char *conninfo);
//
// typedef enum
// {
//     PQPING_OK,                    /* server is accepting connections */
//     PQPING_REJECT,                /* server is alive but rejecting connections */
//     PQPING_NO_RESPONSE,            /* could not establish connection */
//     PQPING_NO_ATTEMPT            /* connection not attempted (bad params) */
// } PGPing;

// ConnStatusType PQstatus(const PGconn *conn);
// typedef enum
// {
//     CONNECTION_OK,
//     CONNECTION_BAD,
// } ConnStatusType;

// char *PQresultErrorMessage(const PGresult *res);
// char *PQerrorMessage(const PGconn *conn);  // 获取最近一次失败操作的错误信息

// int PQgetlength(const PGresult *res, int row_number, int column_number);  // 根据行号和列号返回字段的长度，文本内容格式与 strlen() 返回的一样
//
// int PQisthreadsafe();  // 返回 1，表示当前运行环境中的PQ是线程安全的
//  In particular, you cannot issue concurrent commands from different threads through the same connection object. (If you need to run concurrent commands, use multiple connections.)

    PQclear(zpRes);
    PQfinish(zpConn);
}
