#include <libpq-fe.h>

#include "zCommon.h"

/*
 * 编译：-lpq
 */

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


// 对于需要重复执行析 SQL 语句，可以生成一个预处理过的对象，而后重复使用，提升效率，类似于正则表达式的编译环节
// 注：服务器端生成的此种对象无法通过 libpq 中的函数直接销毁！
// PGresult *PQprepare(PGconn *conn,
//                     const char *stmtName,
//                     const char *query,  // SQL 语句，可以以 $1 $2 ... $N 的形式调用外部提供的参数
//                     int nParams,  // 参数数量
//                     const Oid *paramTypes);  // 参数类型，由于指定 OID 太麻烦，也可以直接在 SQL 语句中以  $1::bigint 的整式强制指定或不指定，由服务器推导类型，若使用这种风格，则此处可以置为 NULL
//
// 执行预处理过的 SQL 对象，指定对象名称，若为 NULL，则代表运行全局唯一的匿名对象
// PGresult *PQexecPrepared(PGconn *conn,
//                          const char *stmtName,
//                          int nParams,  // 实际指定的参数数量
//                          const char * const *paramValues,  // 每次运行时的实际参数值，每个参数的类型在预生成SQL对象时已经指定
//                          const int *paramLengths,  // 文本字段该字段将被忽略，可以为任意值，二进制字段需要分别指定各自的长度
//                          const int *paramFormats,  // 文本字段置 0，二进制置1，若指针置为 NULL，则以全部为文本字段处理
//                          int resultFormat);  // 置 0 表示要求返回的结果是文本格式，置 1 则表示要求二进制格式的结果


// 以命令行惯用的格式显示 SQL 命令返回的内容
// void PQprint(FILE *fout,      /* 输出流 */
//              const PGresult *res,
//              const PQprintOpt *po);
// typedef struct
// {
//     pqbool  header;      /* 打印输出域标题和行计数 */
//     pqbool  align;       /* 填充对齐域 */
//     pqbool  standard;    /* 旧的格式 */
//     pqbool  html3;       /* 输出 HTML 表格 */
//     pqbool  expanded;    /* 扩展表格 */
//     pqbool  pager;       /* 如果必要为输出使用页 */
//     char    *fieldSep;   /* 域分隔符 */
//     char    *tableOpt;   /* 用于 HTML 表格元素的属性 */
//     char    *caption;    /* HTML 表格标题 */
//     char    **fieldName; /* 替换域名称的空终止数组 */
// } PQprintOpt;

    PQclear(zpRes);
    PQfinish(zpConn);
}
