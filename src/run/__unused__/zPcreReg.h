#define ZPCREREG_H
#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 8  //must define this before pcre2.h

#include "pcre2.h"  //compile with '-lpcre2-8'

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#define zMatchMax 1024
typedef struct {
    char *p_rets[zMatchMax];  //matched results
    _ui ResLen[zMatchMax];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, size_t);
    _i RepoId;
} zPcreRes__;

typedef struct {
    pcre2_code *p_pd;
    pcre2_match_data *p_MatchData;
} zPcreInit__ ;

struct zPcreReg__ {
    void (* init) (zPcreInit__ *, const char *);
    void (* match) (zPcreRes__ *, const zPcreInit__ *, const char *, _ui);

    void (* free_meta) (zPcreInit__ *);
    void (* free_res) (zPcreRes__ *);
};


extern struct zPcreReg__ zPcreReg_;
