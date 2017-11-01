
#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 8  //must define this before pcre2.h
#include "pcre2.h"  //compile with '-lpcre2-8'

#include "zCommon.h"

#define zMatchMax 1024

struct zPcreResInfo{
    char *p_rets[zMatchMax];  //matched results
    _ui ResLen[zMatchMax];  // results' strlen
    _ui cnt;         //total num of matched substrings

    void * (* alloc_fn) (_i, size_t);
    _i RepoId;
};
typedef struct zPcreResInfo zPcreResInfo;

struct zPcreInitInfo {
    pcre2_code *p_pd;
    pcre2_match_data *p_MatchData;
};
typedef struct zPcreInitInfo zPcreInitInfo;

struct zPcreReg__ {
    void (* init) (zPcreInitInfo *, const char *);
    void (* match) (zPcreResInfo *, const zPcreInitInfo *, const char *, _ui);

    void (* free_meta) (zPcreInitInfo *);
    void (* free_res) (zPcreResInfo *);
};


extern struct zPcreReg__ zPcreReg_;
