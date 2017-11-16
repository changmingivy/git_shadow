#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
        #define _XOPEN_SOURCE 700
        #define _DEFAULT_SOURCE
        #define _BSD_SOURCE
    #endif
#endif

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifndef ZCOMMON_H
#include "zCommon.h"
#endif

#ifndef ZRUN_H
#include "zRun.h"
#endif

#define UDP 0
#define TCP 1

extern struct zRun__ zRun_;

zNetSrv__ zNetSrv_ = { NULL, NULL, 0 };

_i
main(_i zArgc, char **zppArgv) {
    char *zpConfFilePath = NULL;
    struct stat zStat_;
    zNetSrv_.zServType = TCP;

    for (_i zOpt = 0; -1 != (zOpt = getopt(zArgc, zppArgv, "Uh:p:f:"));) {
        switch (zOpt) {
            case 'h':
                zNetSrv_.p_IpAddr = optarg; break;
            case 'p':
                zNetSrv_.p_port = optarg; break;
            case 'U':
                zNetSrv_.zServType = UDP;
            case 'f':
                if (-1 == stat(optarg, &zStat_) || !S_ISREG(zStat_.st_mode)) {
                        zPrint_Time();
                        fprintf(stderr, "\033[31;01m配置文件异常!\n用法: %s -f <PATH>\033[00m\n", zppArgv[0]);
                        _exit(1);
                }
                zpConfFilePath = optarg;
                break;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPrint_Time();
                fprintf(stderr, "\033[31;01mInvalid option: %c\nUsage: %s -f <Config File Absolute Path>\033[00m\n", optopt, zppArgv[0]);
                _exit(1);
           }
    }


    /* 启动服务 */
    zRun_.run(&zNetSrv_, zpConfFilePath);
}
