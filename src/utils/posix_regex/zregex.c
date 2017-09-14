#ifndef _Z
    #include "../../../inc/zutils.h"
#endif

#include <regex.h>

#define zMatchLimit 64

struct zRegResInfo {
    char *p_rets[zMatchLimit];  //matched results
    _i cnt;         //total num of matched substrings
};
typedef struct zRegResInfo zRegResInfo;

typedef regex_t zRegInitInfo;

/* 使用 posix 扩展正则 */
void
zreg_compile(zRegInitInfo *zpRegInitIf, const char *zpRegPattern) {
    _i zErrNo;
    char zErrBuf[256];
    if (0 != (zErrNo = regcomp(zpRegInitIf, zpRegPattern, REG_EXTENDED))) {
        zPrint_Time();
        regerror(zErrNo, zpRegInitIf, zErrBuf, zBytes(256));
        zPrint_Err(0, NULL, zErrBuf);
        regfree(zpRegInitIf);
        exit(1);
    }
}

void
zreg_match(zRegResInfo *zpRegResIf, regex_t *zpRegInitIf, const char *zpRegSubject) {
    _i zErrNo;
    char zErrBuf[256], *zpResBuf;
    regmatch_t zMatchResIf[zMatchLimit];

    if ((0 != (zErrNo = regexec(zpRegInitIf, zpRegSubject, zMatchLimit, zMatchResIf, 0))) && (REG_NOMATCH != zErrNo)) {
        zPrint_Time();
        regerror(zErrNo, zpRegInitIf, zErrBuf, zBytes(256));
        zPrint_Err(0, NULL, zErrBuf);
        regfree(zpRegInitIf);
        exit(1);
    }

    zpRegResIf->cnt = 0;
    zMem_Alloc(zpResBuf, char, 2 * strlen(zpRegSubject));

    /* 第一个成员存储的是 zpRegSubject 本身，从第二个成员开始是匹配结果 */
    for (_i zCnter = 0; (zCnter < zMatchLimit) && (-1 != zMatchResIf[zCnter].rm_so); zCnter++) {
        strncpy(zpResBuf, &(zpRegSubject[zMatchResIf[zCnter].rm_so]), zMatchResIf[zCnter].rm_eo - zMatchResIf[zCnter].rm_so);
        zpResBuf[zMatchResIf[zCnter].rm_eo] = '\0';

        zpRegResIf->p_rets[zCnter] = zpResBuf;
        //zpRegResIf->p_rets[zCnter - 1] = zpResBuf;
        zpRegResIf->cnt++;

        zpResBuf += zMatchResIf[zCnter].rm_eo + 1;
    }
}

void
zreg_free_tmpsource(zRegResInfo *zpRes) {
    free(&(zpRes->p_rets[0][0]));
}

void
zreg_free_metasource(zRegInitInfo *zpRegInitIf) {
    regfree(zpRegInitIf);
}

#undef zMatchLimit
