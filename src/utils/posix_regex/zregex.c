#ifndef _Z
    #include "../../../inc/zutils.h"
#endif

#include <regex.h>

#define zMatchLimit 1024

struct zRegResInfo {
    char *p_rets[zMatchLimit];  //matched results
    _i ResLen[zMatchLimit];  // results' strlen
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
    _i zErrNo, zDynSubjectlen, zResStrLen;
    char zErrBuf[256];
    regmatch_t zMatchResIf[2];

    zpRegResIf->cnt = 0;
    zDynSubjectlen = strlen(zpRegSubject);
    for (_i zCnter = 0; (zCnter < zMatchLimit) && (zDynSubjectlen > 0); zCnter++) {
        if (0 != (zErrNo = regexec(zpRegInitIf, zpRegSubject, 1, &(zMatchResIf[1]), 0))) {
            if (REG_NOMATCH == zErrNo) { break; }
            else {
                zPrint_Time();
                regerror(zErrNo, zpRegInitIf, zErrBuf, zBytes(256));
                zPrint_Err(0, NULL, zErrBuf);
                regfree(zpRegInitIf);
                exit(1);
            }
        }

        zResStrLen = zMatchResIf[1].rm_eo - zMatchResIf[1].rm_so;
        if (0 == zResStrLen) { break; }

        zpRegResIf->ResLen[zpRegResIf->cnt] = zResStrLen;
        zpRegResIf->cnt++;

        zMem_Alloc(zpRegResIf->p_rets[zCnter], char, 1 + zResStrLen);
        strncpy(zpRegResIf->p_rets[zCnter], zpRegSubject + zMatchResIf[1].rm_so, zResStrLen);
        zpRegResIf->p_rets[zCnter][zResStrLen] = '\0';

        zpRegSubject += zMatchResIf[1].rm_eo + 1;
        zDynSubjectlen -= zMatchResIf[1].rm_eo + 1;
    }
}

void
zreg_free_tmpsource(zRegResInfo *zpRes) {
    for (_i zCnter = 0; zCnter < zpRes->cnt; zCnter++) {
        free(zpRes->p_rets[zCnter]);
    }
}

void
zreg_free_metasource(zRegInitInfo *zpRegInitIf) {
    regfree(zpRegInitIf);
}

#undef zMatchLimit
