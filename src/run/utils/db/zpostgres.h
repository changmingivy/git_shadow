#ifndef ZPGSQL_H
#define ZPGSQL_H

#include "zcommon.h"
#include <semaphore.h>
#include "libpq-fe.h"

typedef PGconn zPgConnHd__;
typedef PGresult zPgResHd__;

typedef struct __zPgResTuple__ {
    //_i *p_taskCnt;

    char **pp_fields;
} zPgResTuple__;

typedef struct __zPgRes__ {
    zPgResHd__ *p_pgResHd_;

    _i tupleCnt;
    _i fieldCnt;

    //_i taskCnt;

    zPgResTuple__ fieldNames_;
    zPgResTuple__ tupleRes_[];
} zPgRes__;

struct zPgSQL__ {
    zPgConnHd__ * (* conn) (const char *);
    void (* conn_reset) (zPgConnHd__ *);

    zPgResHd__ * (* exec) (zPgConnHd__ *, const char *, zbool_t);
    zPgResHd__ * (* exec_with_param) (zPgConnHd__ *, const char *, _i, const char * const *, zbool_t);
    zPgResHd__ * (* prepare) (zPgConnHd__ *, const char *, const char *, _i);
    zPgResHd__ * (* prepare_exec) (zPgConnHd__ *, const char *, _i, const char * const *, zbool_t);

    zPgRes__ * (* parse_res) (zPgResHd__ *);

    void (* res_clear) (zPgResHd__ *, zPgRes__ *);
    void (* conn_clear) (zPgConnHd__ *);

    zbool_t (* thread_safe_check) ();
    zbool_t (* conn_check) (const char *);

    _i (* exec_once) (char *, char *, zPgRes__ **);
    _i (* exec_with_param_once) (char *, char *, _i, const char **, zPgRes__ **);

    void (* conn_pool_init) (void);
    _i (* write_db) (void *, _i, struct sockaddr *, socklen_t);
};

#endif  // #ifndef ZPGSQL_H
