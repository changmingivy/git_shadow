#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define _SELF_
#include "zPosixReg.h"
#undef _SELF_

static void
zreg_compile(zRegInitInfo *zpRegInitIfOut, const char *zpRegPattern);

static void
zreg_match(zRegResInfo *zpRegResIfOut, regex_t *zpRegInitIf, const char *zpRegSubject);

static void
zreg_free_res(zRegResInfo *zpResIf);

static void
zreg_free_meta(zRegInitInfo *zpInitIf);

/* 对外公开的接口 */
struct zPosixReg__ zPosixReg_ = {
    .compile = zreg_compile,
    .match = zreg_match,
    .free_meta = zreg_free_meta,
    .free_res = zreg_free_res
};

/* 使用 posix 扩展正则 */
static void
zreg_compile(zRegInitInfo *zpRegInitIfOut, const char *zpRegPattern) {
    _i zErrNo;
    char zErrBuf[256];
    if (0 != (zErrNo = regcomp(zpRegInitIfOut, zpRegPattern, REG_EXTENDED))) {
        zPrint_Time();
        regerror(zErrNo, zpRegInitIfOut, zErrBuf, zBytes(256));
        zPrint_Err(0, NULL, zErrBuf);
        regfree(zpRegInitIfOut);
        exit(1);
    }
}

static void
zreg_match(zRegResInfo *zpRegResIfOut, regex_t *zpRegInitIf, const char *zpRegSubject) {
    _i zErrNo, zDynSubjectlen, zResStrLen;
    _ui zOffSet = 0;
    char zErrBuf[256];
    regmatch_t zMatchResIf;

    zpRegResIfOut->cnt = 0;
    zDynSubjectlen = strlen(zpRegSubject);

    /* 将足够大的内存一次性分配给成员 [0]，后续成员通过指针位移的方式获取内存 */
    if (NULL == zpRegResIfOut->alloc_fn) {
        zMem_Alloc(zpRegResIfOut->p_rets[0], char, 2 * zDynSubjectlen);
    } else {
        zpRegResIfOut->p_rets[0] = zpRegResIfOut->alloc_fn(zpRegResIfOut->RepoId, zBytes(2 * zDynSubjectlen));
    }

    for (_i zCnter = 0; (zCnter < zMatchLimit) && (zDynSubjectlen > 0); zCnter++) {
        if (0 != (zErrNo = regexec(zpRegInitIf, zpRegSubject, 1, &zMatchResIf, 0))) {
            if (REG_NOMATCH == zErrNo) { break; }
            else {
                zPrint_Time();
                regerror(zErrNo, zpRegInitIf, zErrBuf, zBytes(256));
                zPrint_Err(0, NULL, zErrBuf);
                regfree(zpRegInitIf);
                exit(1);
            }
        }

        zResStrLen = zMatchResIf.rm_eo - zMatchResIf.rm_so;
        if (0 == zResStrLen) { break; }

        zpRegResIfOut->ResLen[zpRegResIfOut->cnt] = zResStrLen;
        zpRegResIfOut->cnt++;

        zpRegResIfOut->p_rets[zCnter] = zpRegResIfOut->p_rets[0] + zOffSet;
        strncpy(zpRegResIfOut->p_rets[zCnter], zpRegSubject + zMatchResIf.rm_so, zResStrLen);
        zpRegResIfOut->p_rets[zCnter][zResStrLen] = '\0';

        zOffSet += zMatchResIf.rm_eo + 1;  // '+ 1' for '\0'
        zpRegSubject += zMatchResIf.rm_eo + 1;
        zDynSubjectlen -= zMatchResIf.rm_eo + 1;
    }
}

/* 内存是全量分配给成员 [0] 的，只需释放一次 */
static void
zreg_free_res(zRegResInfo *zpResIf) {
    free((zpResIf)->p_rets[0]);
}

static void
zreg_free_meta(zRegInitInfo *zpInitIf) {
    regfree((zpInitIf));
}

#undef zMatchLimit
