#ifndef _Z
    #include "../../../inc/zutils.h"
#endif

#include <regex.h>

#define zMatchLimit 1024

struct zRegResInfo {
    char *p_rets[zMatchLimit];  //matched results
    _ui ResLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings
    _i RepoId;  // 负值表示使用系统alloc函数分配内存；非负值表示使用自定的 zalloc_cache() 函数分配内存，从而不需释放内存
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
    _ui zOffSet = 0;
    char zErrBuf[256];
    regmatch_t zMatchResIf;

    zpRegResIf->cnt = 0;
    zDynSubjectlen = strlen(zpRegSubject);

    /* 将足够大的内存一次性分配给成员 [0]，后续成员通过指针位移的方式获取内存 */
    if (0 > zpRegResIf->RepoId) {
        zMem_Alloc(zpRegResIf->p_rets[0], char, 2 * zDynSubjectlen);
    } else {
        zpRegResIf->p_rets[0] = zalloc_cache(zpRegResIf->RepoId, zBytes(2 * zDynSubjectlen));
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

        zpRegResIf->ResLen[zpRegResIf->cnt] = zResStrLen;
        zpRegResIf->cnt++;

        zpRegResIf->p_rets[zCnter] = zpRegResIf->p_rets[0] + zOffSet;
        strncpy(zpRegResIf->p_rets[zCnter], zpRegSubject + zMatchResIf.rm_so, zResStrLen);
        zpRegResIf->p_rets[zCnter][zResStrLen] = '\0';

        zOffSet += zMatchResIf.rm_eo + 1;  // '+ 1' for '\0'
        zpRegSubject += zMatchResIf.rm_eo + 1;
        zDynSubjectlen -= zMatchResIf.rm_eo + 1;
    }
}

/* 内存是全量分配给成员 [0] 的，只需释放一次 */
#define zReg_Free_Tmpsource(zpRes) do {\
    free((zpRes)->p_rets[0]);\
} while(0)

#define zReg_Free_Metasource(zpRegInitIf) do {\
    regfree((zpRegInitIf));\
} while(0)

#undef zMatchLimit
