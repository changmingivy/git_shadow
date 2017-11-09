#ifndef ZPGSQL_H
#define ZPGSQL_H

#include <libpq-fe.h>
#include "zCommon.h"

typedef PGconn zPgConnHd__;
typedef PGresult zPgResHd__;


typedef struct __zPgRes__ {
    _i tupleCnt;
    _i fieldCnt;

    char *p_res[];
} zPgRes__;


#endif  // #ifndef ZPGSQL_H
