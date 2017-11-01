#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

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

static void
zpcre_init(zPcreInitInfo *zpPcreInitIfOut, const char *zpPcrePattern);

static void
zpcre_match(zPcreResInfo *zpPcreResIfOut, const zPcreInitInfo *zpPcreInitIf, const char *zpPcreSubject, _ui zMatchLimit);

static void
zpcre_free_meta(zPcreInitInfo *zpInitIf);

static void
zpcre_free_res(zPcreResInfo *zpResIf);

struct zPcreReg__ {
    void (* init) (zPcreInitInfo *, const char *);
    void (* match) (zPcreResInfo *, const zPcreInitInfo *, const char *, _ui);

    void (* free_meta) (zPcreInitInfo *);
    void (* free_res) (zPcreResInfo *);
};

/* 唯一对外公开的接口 */
struct zPcreReg__ zPcreReg_ = {
    .init = zpcre_init,
    .match = zpcre_match,
    .free_meta = zpcre_free_meta,
    .free_res = zpcre_free_res
};

static void
zpcre_get_err(const _i zErrNo) {
    PCRE2_UCHAR zErrBuf[256];
    pcre2_get_error_message(zErrNo, zErrBuf, sizeof(zErrBuf));
    zPrint_Err(errno, NULL, (char *)zErrBuf);
}

static void
zpcre_init(zPcreInitInfo *zpPcreInitIfOut, const char *zpPcrePattern) {
    _i zErrNo;
    PCRE2_SIZE zErrOffset;

    zpPcreInitIfOut->p_pd = pcre2_compile((PCRE2_SPTR)zpPcrePattern, PCRE2_ZERO_TERMINATED, 0, &zErrNo, &zErrOffset, NULL);
    if (NULL == zpPcreInitIfOut->p_pd) { zpcre_get_err(zErrNo); }

    zpPcreInitIfOut->p_MatchData = pcre2_match_data_create_from_pattern(zpPcreInitIfOut->p_pd, NULL);
}

static void
zpcre_match(zPcreResInfo *zpPcreResIfOut, const zPcreInitInfo *zpPcreInitIf, const char *zpPcreSubject, _ui zMatchLimit) {
    PCRE2_SPTR zpSubject = (PCRE2_SPTR)zpPcreSubject;
    size_t zDynSubjectLen = strlen(zpPcreSubject);

    /* 将足够大的内存一次性分配给成员 [0]，后续成员通过指针位移的方式获取内存 */
    if (NULL != zpPcreResIfOut->alloc_fn) {
        zMem_Alloc(zpPcreResIfOut->p_rets[0], char, 2 * zDynSubjectLen);
    } else {
        zpPcreResIfOut->p_rets[0] = zpPcreResIfOut->alloc_fn(zpPcreResIfOut->RepoId, zBytes(2 * zDynSubjectLen));
    }

    PCRE2_SIZE *zpResVector = NULL;
    _i zErrNo = 0;
    size_t zOffSet = 0;
    zpPcreResIfOut->cnt = 0;
    if (zMatchMax < zMatchLimit) { zMatchLimit = zMatchMax; }
    while (zpPcreResIfOut->cnt < zMatchLimit) {
        //zErrNo == 0 means space is not enough, you need check it
        //when use pcre2_match_data_create instead of the one whih suffix '_pattern'
        if (0 > (zErrNo = pcre2_match(zpPcreInitIf->p_pd, zpSubject, zDynSubjectLen, 0, 0, zpPcreInitIf->p_MatchData, NULL))) {
            if (zErrNo == PCRE2_ERROR_NOMATCH) { break; }
            else { zpcre_get_err(zErrNo); exit(1); }
        }

        zpResVector = pcre2_get_ovector_pointer(zpPcreInitIf->p_MatchData);
        //if (zpResVector[0] >= zpResVector[1]) { break; }

        zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt] = zpResVector[1] - zpResVector[0];

        zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt] = zpPcreResIfOut->p_rets[0] + zOffSet;
        strncpy(zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt], (char *) zpSubject + zpResVector[0], zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt]);
        zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt][zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt]] = '\0';  // 最终结果以字符串类型返回

        zOffSet += zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt] + 1;
        zpSubject += zpResVector[1] + 1;
        zDynSubjectLen -= zpResVector[1] + 1;
        zpPcreResIfOut->cnt++;
    }

    zpPcreResIfOut->cnt--;  // 结果一定是多加了一次的，要减掉
}

static void
zpcre_free_meta(zPcreInitInfo *zpInitIf) {
    pcre2_match_data_free(zpInitIf->p_MatchData);
    pcre2_code_free(zpInitIf->p_pd);
}

/* 内存是全量分配给成员 [0] 的，只需释放一次 */
static void
zpcre_free_res(zPcreResInfo *zpResIf) {
    free((zpResIf)->p_rets[0]);
}

// zPcreResInfo * zpcre_substitude(...) { /* TO DO */ }
