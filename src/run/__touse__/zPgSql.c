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
        "passfile=/home/'.a\' file'"
        "dbname=testdb"
        "sslmode=allow"
        "connect_timeout=10";

    zpConn = PQconnectdb(zpConnInfo);

    if (CONNECTION_OK != PQstatus(zpConn)) {
        PQfinish(zpConn);
    }

    zpRes = PQexec(zpConn, "select * from a_table");

    if (PGRES_COMMAND_OK != PQresultStatus(zpRes)) {
        PQclear(zpRes);
        PQfinish(zpConn);
    }

    zFieldCnt = PQnfields(zpRes);
    for (zCnter[0] = 0; zCnter[0] < zFieldCnt; zCnter[0]++) {
        printf("field name: %-15s, field index: %d\n", PQfname(zpRes, zCnter[0]), PQfnumber(zpRes, PQfname(zpRes, zCnter[0])));  // zCnter == PQfnumber(zpRes, PQfname(zpRes, zCnter[0]))
    }

    for (zCnter[0] = 0; zCnter[0] < PQntuples(zpRes); zCnter[0]++) {
        for (zCnter[1] = 0; zCnter[1] < zFieldCnt; zCnter[1]++) {
            printf("%-15s\n", PQgetvalue(zpRes, zCnter[1], zCnter[0]));
        }
    }

    PQclear(zpRes);
    PQfinish(zpConn);
}
