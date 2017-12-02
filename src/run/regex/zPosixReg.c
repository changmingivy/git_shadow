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
zstr_split_fast(zRegRes__ *zpResOUT, char *zpOrigStr, char *zpDelim);

static void
zreg_free_res(zRegRes__ *zpRes_);

static void
zreg_free_meta(zRegInit__ *zpInit_);

/* 对外公开的接口 */
struct zPosixReg__ zPosixReg_ = {
    .init = zreg_init,

    .match = zreg_match,

    .str_split = zstr_split,
    .str_split_fast = zstr_split_fast,

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
        zMem_Alloc(zpRegResOUT->pp_rets, char, (sizeof(void *) + sizeof(_i)) * zDynSubjectlen + 2 * zDynSubjectlen);
    } else {
        zpRegResOUT->pp_rets = zpRegResOUT->alloc_fn(zpRegResOUT->repoId, (sizeof(void *) + sizeof(_i)) * zDynSubjectlen + 2 * zBytes(zDynSubjectlen));
    }

    zpRegResOUT->p_resLen = (_i *)(zpRegResOUT->pp_rets + zDynSubjectlen);
    zpRegResOUT->pp_rets[0] = (char *)(zpRegResOUT->p_resLen + zDynSubjectlen);

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
 * 用途：将字符串按指定分割符分割，返回分割后的结果，以 zRegRes__ 结构的形式返回
 * 出错返回 -1
 * 写出结构体成员若未使用项目内存池，则需要释放
 *
 * 可以识别分割符连续的情况，用于跟前端交互
 */
static void
zstr_split(zRegRes__ *zpResOUT, char *zpOrigStr, char *zpDelim) {
    char *zpStr = NULL;
    _i zFullLen =  0,
       zMaxItemNum = 0;

    if (!(zpResOUT && zpOrigStr && zpDelim)) {
        zPrint_Err(0, NULL, "param invalid");
        exit(1);
    }

    zFullLen = 1 + strlen(zpOrigStr);

    /* 将足够大的内存一次性分配 */
    if (NULL == zpResOUT->alloc_fn) {
        zMem_Alloc(zpResOUT->pp_rets, char, zMaxItemNum * (sizeof(void *) + sizeof(_i)) + zBytes(zFullLen));
    } else {
        zpResOUT->pp_rets = zpResOUT->alloc_fn(zpResOUT->repoId, zMaxItemNum * (sizeof(void *) + sizeof(_i)) + zBytes(zFullLen));
    }

    zpResOUT->p_resLen = (_i *) (zpResOUT->pp_rets + zMaxItemNum);
    zpStr = (char *) (zpResOUT->p_resLen + zMaxItemNum);

    /* strsep() 会直接更改源字符串，因此需要复制一份 */
    strcpy(zpStr, zpOrigStr);

    zpResOUT->cnt = 0;
    while (NULL != (zpResOUT->pp_rets[zpResOUT->cnt] = strsep(&zpStr, zpDelim))) {
        zpResOUT->p_resLen[zpResOUT->cnt] = zpStr - zpResOUT->pp_rets[zpResOUT->cnt] - 1;
        zpResOUT->cnt++;
        while ('\0' != zpStr[0] && NULL != strchr(zpDelim, zpStr[0])) {
            zpStr++;
        }
    }
}


/*
 * 功能同上，速度更快，
 * 但不能识别分割符连续的情况，服务端内部使用
 */
static void
zstr_split_fast(zRegRes__ *zpResOUT, char *zpOrigStr, char *zpDelim) {
    char *zpStr = NULL;
    _i zFullLen =  0,
       zMaxItemNum = 0;

    if (!(zpResOUT && zpOrigStr && zpDelim)) {
        zPrint_Err(0, NULL, "param invalid");
        exit(1);
    }

    zFullLen = 1 + strlen(zpOrigStr);
    zMaxItemNum = zFullLen / 2;

    /* 将足够大的内存一次性分配 */
    if (NULL == zpResOUT->alloc_fn) {
        zMem_Alloc(zpResOUT->pp_rets, char, zMaxItemNum * (sizeof(void *) + sizeof(_i)) + zFullLen);
    } else {
        zpResOUT->pp_rets = zpResOUT->alloc_fn(zpResOUT->repoId, zMaxItemNum * (sizeof(void *) + sizeof(_i)) + zBytes(zFullLen));
    }

    zpResOUT->p_resLen = (_i *) (zpResOUT->pp_rets + zMaxItemNum);
    zpStr = (char *) (zpResOUT->p_resLen + zMaxItemNum);

    /* strsep 会直接更改源字符串，因此需要复制一份 */
    strcpy(zpStr, zpOrigStr);

    zpResOUT->cnt = 0;
    while (NULL != (zpResOUT->pp_rets[zpResOUT->cnt] = strsep(&zpStr, zpDelim))) {
        zpResOUT->p_resLen[zpResOUT->cnt] = zpStr - zpResOUT->pp_rets[zpResOUT->cnt] - 1;
        zpResOUT->cnt++;
    }
}


/* 内存是全量分配给成员 [0] 的，只需释放一次 */
static void
zreg_free_res(zRegRes__ *zpRes_) {
    if (NULL == zpRes_->alloc_fn) {
        free((zpRes_)->pp_rets);
    };
}


static void
zreg_free_meta(zRegInit__ *zpInit_) {
    regfree((zpInit_));
}
