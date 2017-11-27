#include "zPosixReg.h"

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void
zreg_init(zRegInit__ *zpRegInit_Out, const char *zpRegPattern);

static void
zreg_match(zRegRes__ *zpRegRes_Out, regex_t *zpRegInit_, const char *zpRegSubject);

static void
zreg_free_res(zRegRes__ *zpRes_);

static void
zreg_free_meta(zRegInit__ *zpInit_);

/* 对外公开的接口 */
struct zPosixReg__ zPosixReg_ = {
    .init = zreg_init,
    .match = zreg_match,
    .free_meta = zreg_free_meta,
    .free_res = zreg_free_res
};

/* 使用 posix 扩展正则 */
static void
zreg_init(zRegInit__ *zpRegInit_Out, const char *zpRegPattern) {
    _i zErrNo;
    char zErrBuf[256];
    if (0 != (zErrNo = regcomp(zpRegInit_Out, zpRegPattern, REG_EXTENDED))) {
        zPrint_Time();
        regerror(zErrNo, zpRegInit_Out, zErrBuf, zBytes(256));
        zPrint_Err(0, NULL, zErrBuf);
        regfree(zpRegInit_Out);
        exit(1);
    }
}

static void
zreg_match(zRegRes__ *zpRegRes_Out, regex_t *zpRegInit_, const char *zpRegSubject) {
    _i zErrNo, zDynSubjectlen, zResStrLen;
    _ui zOffSet = 0;
    char zErrBuf[256];
    regmatch_t zMatchRes_;

    zpRegRes_Out->cnt = 0;
    zDynSubjectlen = strlen(zpRegSubject);

    /* 将足够大的内存一次性分配，后续成员通过指针位移的方式获取内存 */
    if (NULL == zpRegRes_Out->alloc_fn) {
        zMem_Alloc(zpRegRes_Out->p_resLen, _ui, 2 * zBytes(zDynSubjectlen));
        zpRegRes_Out->pp_rets = (char **)(zpRegRes_Out->p_resLen + zDynSubjectlen);
        zMem_Alloc(zpRegRes_Out->pp_rets[0], char, 2 * zBytes(zDynSubjectlen));
    } else {
        zpRegRes_Out->p_resLen = zpRegRes_Out->alloc_fn(zpRegRes_Out->repoId, sizeof(_ui) * 2 * zBytes(zDynSubjectlen));
        zpRegRes_Out->pp_rets = (char **)(zpRegRes_Out->p_resLen + zDynSubjectlen);
        zpRegRes_Out->pp_rets[0] = zpRegRes_Out->alloc_fn(zpRegRes_Out->repoId, zBytes(2 * zDynSubjectlen));
    }

    for (_i zCnter = 0; zDynSubjectlen > 0; zCnter++) {
        if (0 != (zErrNo = regexec(zpRegInit_, zpRegSubject, 1, &zMatchRes_, 0))) {
            if (REG_NOMATCH == zErrNo) {
                break;
            } else {
                zPrint_Time();
                regerror(zErrNo, zpRegInit_, zErrBuf, zBytes(256));
                zPrint_Err(0, NULL, zErrBuf);
                regfree(zpRegInit_);
                exit(1);
            }
        }

        zResStrLen = zMatchRes_.rm_eo - zMatchRes_.rm_so;
        if (0 == zResStrLen) {
            break;
        }

        zpRegRes_Out->p_resLen[zpRegRes_Out->cnt] = zResStrLen;
        zpRegRes_Out->cnt++;

        zpRegRes_Out->pp_rets[zCnter] = zpRegRes_Out->pp_rets[0] + zOffSet;
        strncpy(zpRegRes_Out->pp_rets[zCnter], zpRegSubject + zMatchRes_.rm_so, zResStrLen);
        zpRegRes_Out->pp_rets[zCnter][zResStrLen] = '\0';

        zOffSet += zMatchRes_.rm_eo + 1;  // '+ 1' for '\0'
        zpRegSubject += zMatchRes_.rm_eo + 1;
        zDynSubjectlen -= zMatchRes_.rm_eo + 1;
    }
}


/* 内存是全量分配给成员 [0] 的，只需释放一次 */
static void
zreg_free_res(zRegRes__ *zpRes_) {
    if (NULL == zpRes_->alloc_fn) {
        free((zpRes_)->p_resLen);
        free((zpRes_)->pp_rets[0]);
    };
}


static void
zreg_free_meta(zRegInit__ *zpInit_) {
    regfree((zpInit_));
}
