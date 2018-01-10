#include "zPcreReg.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static void
zpcre_init(zPcreInit__ *zpPcreInit_Out, const char *zpPcrePattern);

static void
zpcre_match(zPcreRes__ *zpPcreRes_Out, const zPcreInit__ *zpPcreInit_, const char *zpPcreSubject, _ui zMatchLimit);

static void
zpcre_free_meta(zPcreInit__ *zpInit_);

static void
zpcre_free_res(zPcreRes__ *zpRes_);

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
    zPRINT_ERR(errno, NULL, (char *)zErrBuf);
}

static void
zpcre_init(zPcreInit__ *zpPcreInit_Out, const char *zpPcrePattern) {
    _i zErrNo;
    PCRE2_SIZE zErrOffset;

    zpPcreInit_Out->p_pd = pcre2_compile((PCRE2_SPTR)zpPcrePattern, PCRE2_ZERO_TERMINATED, 0, &zErrNo, &zErrOffset, NULL);
    if (NULL == zpPcreInit_Out->p_pd) {
        zpcre_get_err(zErrNo);
    }

    zpPcreInit_Out->p_matchData = pcre2_match_data_create_from_pattern(zpPcreInit_Out->p_pd, NULL);
}

static void
zpcre_match(zPcreRes__ *zpPcreRes_Out, const zPcreInit__ *zpPcreInit_, const char *zpPcreSubject, _ui zMatchLimit) {
    PCRE2_SPTR zpSubject = (PCRE2_SPTR)zpPcreSubject;
    size_t zDynSubjectLen = strlen(zpPcreSubject);

    /* 将足够大的内存一次性分配给成员 [0]，后续成员通过指针位移的方式获取内存 */
    if (NULL != zpPcreRes_Out->alloc_fn) {
        zMEM_ALLOC(zpPcreRes_Out->p_rets[0], char, 2 * zDynSubjectLen);
    } else {
        zpPcreRes_Out->p_rets[0] = zpPcreRes_Out->alloc_fn(zpPcreRes_Out->repoID, zBYTES(2 * zDynSubjectLen));
    }

    PCRE2_SIZE *zpResVector = NULL;
    _i zErrNo = 0;
    size_t zOffSet = 0;
    zpPcreRes_Out->cnt = 0;
    if (zMatchMax < zMatchLimit) {
        zMatchLimit = zMatchMax;
    }
    while (zpPcreRes_Out->cnt < zMatchLimit) {
        //zErrNo == 0 means space is not enough, you need check it
        //when use pcre2_match_data_create instead of the one whih suffix '_pattern'
        if (0 > (zErrNo = pcre2_match(zpPcreInit_->p_pd, zpSubject, zDynSubjectLen, 0, 0, zpPcreInit_->p_matchData, NULL))) {
            if (zErrNo == PCRE2_ERROR_NOMATCH) {
                break;
            } else {
                zpcre_get_err(zErrNo);
                exit(1);
            }
        }

        zpResVector = pcre2_get_ovector_pointer(zpPcreInit_->p_matchData);
        //if (zpResVector[0] >= zpResVector[1]) {
        //    break;
        //}

        zpPcreRes_Out->resLen[zpPcreRes_Out->cnt] = zpResVector[1] - zpResVector[0];

        zpPcreRes_Out->p_rets[zpPcreRes_Out->cnt] = zpPcreRes_Out->p_rets[0] + zOffSet;
        strncpy(zpPcreRes_Out->p_rets[zpPcreRes_Out->cnt], (char *) zpSubject + zpResVector[0], zpPcreRes_Out->resLen[zpPcreRes_Out->cnt]);
        zpPcreRes_Out->p_rets[zpPcreRes_Out->cnt][zpPcreRes_Out->resLen[zpPcreRes_Out->cnt]] = '\0';  // 最终结果以字符串类型返回

        zOffSet += zpPcreRes_Out->resLen[zpPcreRes_Out->cnt] + 1;
        zpSubject += zpResVector[1] + 1;
        zDynSubjectLen -= zpResVector[1] + 1;
        zpPcreRes_Out->cnt++;
    }

    zpPcreRes_Out->cnt--;  // 结果一定是多加了一次的，要减掉
}

static void
zpcre_free_meta(zPcreInit__ *zpInit_) {
    pcre2_match_data_free(zpInit_->p_matchData);
    pcre2_code_free(zpInit_->p_pd);
}

/* 内存是全量分配给成员 [0] 的，只需释放一次 */
static void
zpcre_free_res(zPcreRes__ *zpRes_) {
    free((zpRes_)->p_rets[0]);
}

// zPcreRes__ * zpcre_substitude(...) { /* TO DO */ }
