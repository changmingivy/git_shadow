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


struct zPgSql__ {
    bool (* check_thread_safe) ();

    zPgConnHd__ * (* conn) (const char *);
    void (* conn_reset) (zPgConnHd__ *);

    zPgResHd__ * (* exec) (zPgConnHd__ *, const char *, bool);
    zPgResHd__ * (* prepare) (zPgConnHd__ *, const char *, const char *, _i);
    zPgResHd__ * (* prepare_exec) (zPgConnHd__ *, const char *, _i, const char * const *, bool);

    zPgRes__ * (* parse_res) (zPgResHd__ *);

    void (* res_clear) (zPgResHd__ *, zPgRes__ *);
    void (* conn_clear) (zPgConnHd__ *);
};


// extern struct zPgSql__ zPgSql_ ;

#endif  // #ifndef ZPGSQL_H
