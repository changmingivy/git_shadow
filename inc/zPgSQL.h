#ifndef ZPGSQL_H
#define ZPGSQL_H

#include <libpq-fe.h>
#include "zCommon.h"


typedef struct __zPgLogin__ {
    char * p_host;
    char * p_addr;
    char * p_port;
    char * p_userName;
    char * p_passFilePath;
    char * p_dbName;
} zPgLogin__;


typedef PGconn zPgConnHd__;
typedef PGresult zPgResHd__;


typedef struct __zPgResTuple__ {
    void *p_gc;  // thread garbage collection

    _i *p_taskCnt;

    char **pp_fields;
} zPgResTuple__;


typedef struct __zPgRes__ {
    _i tupleCnt;
    _i fieldCnt;

    _i taskCnt;

    zPgResTuple__ fieldNames_;
    zPgResTuple__ tupleRes_[];
} zPgRes__;


struct zPgSQL__ {
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


// extern struct zPgSQL__ zPgSQL_ ;

#endif  // #ifndef ZPGSQL_H
