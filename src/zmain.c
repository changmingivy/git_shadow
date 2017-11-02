#ifndef _Z_BSD
#define _XOPEN_SOURCE 700
#endif

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "zCommon.h"
#include "run/zLocalUtils.h"
#include "run/zLocalOps.h"
#include "run/zThreadPool.h"

#define UDP 0
#define TCP 1

extern void zstart_server(void *);

typedef struct zNetSrv__ {
    char *p_IpAddr;  // 字符串形式的ip点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i zServType;  // 网络服务类型：TCP/UDP
} zNetSrv__;

struct zNetSrv__ zNetSrv_;


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

    /* 转换自身为守护进程，解除与终端的关联关系 */
    zLocalUtils_.daemonize("/");

    /* 初始化线程池：旧的线程池设计，在大压力下应用有阻死风险，暂不用之 */
    zThreadPool_.init();

    /* 扫描所有项目库并初始化之 */
    zLocalOps_.proj_init_all(zpConfFilePath);

    /* 启动网络服务 */
    zstart_server(&zNetSrv_);
}
