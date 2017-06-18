#define PCRE2_CODE_UNIT_WIDTH 8  //must define this before pcre2.h
#include <pcre2.h>  //compile with '-lpcre2-8'

#include "zutils.h"

#define zMatchLimit 64

struct zPCRERetInfo{
	char *p_rets[zMatchLimit];  //matched results
	_i cnt;             //total num of matched substrings
//	char *p_NewSubject;      //store the new subject after substitution
};
typedef struct zPCRERetInfo zPCRERetInfo;

struct zPCREInitInfo {
	pcre2_code *p_pd;
	pcre2_match_data *p_MatchData;
};
typedef struct zPCREInitInfo zPCREInitInfo;

zPCREInitInfo * zpcre_init(const char *);
zPCRERetInfo * zpcre_match(const zPCREInitInfo *, const char *, const _i);
void zpcre_free_tmpsource(zPCRERetInfo *zpRet);
void zpcre_free_metasource(zPCREInitInfo *zpInitIf);
