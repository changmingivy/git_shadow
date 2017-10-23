#ifndef _Z
    #include "../../../inc/zutils.h"
    #include "../../zmain.c"
#endif

#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 8  //must define this before pcre2.h
#include "pcre2.h"  //compile with '-lpcre2-8'

struct zPcreResInfo{
    char *p_rets[zMatchLimit];  //matched results
    _ui ResLen[zMatchLimit];  // results' strlen
    _ui cnt;         //total num of matched substrings
    _i RepoId;  // 负值表示使用系统alloc函数分配内存；非负值表示使用自定的 zalloc_cache() 函数分配内存，从而不需释放内存
};
typedef struct zPcreResInfo zPcreResInfo;

struct zPcreInitInfo {
    pcre2_code *p_pd;
    pcre2_match_data *p_MatchData;
};
typedef struct zPcreInitInfo zPcreInitInfo;

void
zpcre_get_err(const _i zErrNo) {
    PCRE2_UCHAR zErrBuf[256];
    pcre2_get_error_message(zErrNo, zErrBuf, sizeof(zErrBuf));
    zPrint_Err(errno, NULL, (char *)zBuffer);
}

void
zpcre_init(zPcreInitInfo *zpPcreInitIfOut, const char *zpPcrePattern) {
    _i zErrNo;
    PCRE2_SIZE zErrOffset;

    zpPcreInitIfOut->p_pd = pcre2_compile((Pcre2_SPTR)zpPcrePattern, Pcre2_ZERO_TERMINATED, 0, &zErrNo, &zErrOffset, NULL);
    if (NULL == zpPcreInitIfOut->p_pd) { zpcre_get_err(zErrNo); }

    zpPcreInitIfOut->p_MatchData = pcre2_match_data_create_from_pattern(zpPcreInitIfOut->p_pd, NULL);
}

void
zpcre_match(zPcreResInfo *zpPcreResIfOut, const zPcreInitInfo *zpPcreInitIf, const char *zpPcreSubject, const _i zMatchLimit) {
    PCRE2_SPTR zpSubject = (Pcre2_SPTR)zpPcreSubject;
    size_t zDynSubjectLen = strlen(zpPcreSubject);

    /* 将足够大的内存一次性分配给成员 [0]，后续成员通过指针位移的方式获取内存 */
    if (0 > zpPcreResIfOut->RepoId) {
        zMem_Alloc(zpPcreResIfOut->p_rets[0], char, 2 * zDynSubjectLen);
    } else {
        zpPcreResIfOut->p_rets[0] = zalloc_cache(zpPcreResIfOut->RepoId, zBytes(2 * zDynSubjectLen));
    }

    PCRE2_SIZE *zpResVector = NULL;
    _i zErrNo = 0;
    size_t zOffSet = 0;
    zpPcreResIfOut->cnt = 0;
    while (zpPcreResIfOut->cnt < zMatchLimit) {
        //zErrNo == 0 means space is not enough, you need check it
        //when use pcre2_match_data_create instead of the one whih suffix '_pattern'
        if (0 > (zErrNo = pcre2_match(zpPcreInitIf->p_pd, zpSubject, zDynSubjectLen, 0, 0, zpPcreInitIf->p_MatchData, NULL))) {
            if (zErrNo == Pcre2_ERROR_NOMATCH) { break; }
            else { zpcre_get_err(zErrNo); exit(1); }
        }

        zpResVector = pcre2_get_ovector_pointer(zpPcreInitIf->p_MatchData);
        //if (zpResVector[0] >= zpResVector[1]) { break; }

        zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt] = zpResVector[1] - zpResVector[0];

        zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt] = zpPcreResIfOut->p_rets[0] + zOffSet;
        strncpy(zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt], zpSubject + zpResVector[0], zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt]);
        zpPcreResIfOut->p_rets[zpPcreResIfOut->cnt][zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt]] = '\0';  // 最终结果以字符串类型返回

        zOffSet += zpPcreResIfOut->ResLen[zpPcreResIfOut->cnt] + 1;
        zpSubject += zpResVector[1] + 1;
        zDynSubjectLen -= zpResVector[1] + 1;
        zpPcreResIfOut->cnt++;
    }

    zpPcreResIfOut->cnt--;  // 结果一定是多加了一次的，要减掉
}

#define zPcre_Free_Metasource(zpRegInitIf) do {\
    pcre2_match_data_free(zpInitIf->p_MatchData);\
    pcre2_code_free(zpInitIf->p_pd);\
} while(0)

/* 内存是全量分配给成员 [0] 的，只需释放一次 */
#define zPcre_Free_Tmpsource(zpRes) do {\
    free((zpRes)->p_rets[0]);\
} while(0)

// zPcreResInfo * zpcre_substitude(...) { /* TO DO */ }
