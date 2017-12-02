#include "zPosixReg.h"

#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void
zreg_init(zRegInit__ *zpRegInitOUT, const char *zpRegPattern);

static void
zreg_match(zRegRes__ *zpRegResOUT, regex_t *zpRegInit_, const char *zpRegSubject);

static void
zstr_split(zRegRes__ *zpResOUT, char *zpOrigStr, char *zpDelim);

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
zreg_init(zRegInit__ *zpRegInitOUT, const char *zpRegPattern) {
    _i zErrNo;
    char zErrBuf[256];
    if (0 != (zErrNo = regcomp(zpRegInitOUT, zpRegPattern, REG_EXTENDED))) {
        zPrint_Time();
        regerror(zErrNo, zpRegInitOUT, zErrBuf, zBytes(256));
        zPrint_Err(0, NULL, zErrBuf);
        regfree(zpRegInitOUT);
        exit(1);
    }
}

static void
zreg_match(zRegRes__ *zpRegResOUT, regex_t *zpRegInit_, const char *zpRegSubject) {
    _i zErrNo, zDynSubjectlen, zResStrLen;
    _ui zOffSet = 0;
    char zErrBuf[256];
    regmatch_t zMatchRes_;

    zpRegResOUT->cnt = 0;
    zDynSubjectlen = strlen(zpRegSubject);

    /* 将足够大的内存一次性分配，后续成员通过指针位移的方式获取内存 */
    if (NULL == zpRegResOUT->alloc_fn) {
        zMem_Alloc(zpRegResOUT->p_resLen, char, sizeof(_i) * zDynSubjectlen + sizeof(void *) * zDynSubjectlen + 2 * zDynSubjectlen);
    } else {
        zpRegResOUT->p_resLen = zpRegResOUT->alloc_fn(zpRegResOUT->repoId, sizeof(_i) * zDynSubjectlen + sizeof(void *) * zDynSubjectlen + 2 * zBytes(zDynSubjectlen));
    }
    zpRegResOUT->pp_rets = (char **)(zpRegResOUT->p_resLen + zDynSubjectlen);
    zpRegResOUT->pp_rets[0] = (char *)(zpRegResOUT->pp_rets + zDynSubjectlen);

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

        zpRegResOUT->p_resLen[zpRegResOUT->cnt] = zResStrLen;
        zpRegResOUT->cnt++;

        zpRegResOUT->pp_rets[zCnter] = zpRegResOUT->pp_rets[0] + zOffSet;
        strncpy(zpRegResOUT->pp_rets[zCnter], zpRegSubject + zMatchRes_.rm_so, zResStrLen);
        zpRegResOUT->pp_rets[zCnter][zResStrLen] = '\0';

        zOffSet += zMatchRes_.rm_eo + 1;  // '+ 1' for '\0'
        zpRegSubject += zMatchRes_.rm_eo + 1;
        zDynSubjectlen -= zMatchRes_.rm_eo + 1;
    }
}


/*
 * 用途：
 *   将字符串按指定分割符分割，返回分割后的结果，以 zRegRes__ 结构的形式返回
 * 出错返回 -1
 * 可以指定连续多个分割符
 * 写出结构体成员若未使用项目内存池，则需要释放
 */
static void
zstr_split(zRegRes__ *zpResOUT, char *zpOrigStr, char *zpDelim) {
    char *zpStr = NULL;
    _i zFullLen =  0;

    if (!(zpResOUT && zpOrigStr && zpDelim)) {
        zPrint_Err(0, NULL, "param invalid");
        exit(1);
    }

    zFullLen = 1 + strlen(zpOrigStr);

    /* 此函数不计算 reslen，调用者需要自行计算 */
    zpResOUT->p_resLen = NULL;

    /* 将足够大的内存一次性分配 */
    if (NULL == zpResOUT->alloc_fn) {
        zMem_Alloc(zpResOUT->pp_rets, char, zBytes(zFullLen) / 2 * sizeof(void *) + zBytes(zFullLen));
    } else {
        zpResOUT->pp_rets = zpResOUT->alloc_fn(zpResOUT->repoId, zBytes(zFullLen) / 2 * sizeof(void *) + zBytes(zFullLen));
    }

    zpStr = (char *) (zpResOUT->pp_rets) + zBytes(zFullLen);
    strcpy(zpStr, zpOrigStr);

    zpResOUT->cnt = 0;
    while (NULL != (zpResOUT->pp_rets[zpResOUT->cnt] = strsep(&zpStr, zpDelim))) {
        zpResOUT->cnt++;
        while ('\0' != zpStr[0] && NULL != strchr(zpDelim, zpStr[0])) {
            zpStr++;
        }
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
