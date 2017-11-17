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

#include "zCommon.h"
#include "zRun.h"

#define UDP 0
#define TCP 1

extern struct zRun__ zRun_;

zNetSrv__ zNetSrv_ = { NULL, NULL, 0 };

_i
main(_i zArgc, char **zppArgv) {
    zPgLogin__ zPgLogin_ = { NULL, NULL, NULL, NULL, NULL, NULL };
    zNetSrv_.servType = TCP;
    _i zOpt = 0;

    while (-1 != (zOpt = getopt(zArgc, zppArgv, "uh:p:H:P:U:F:D:"))) {
        switch (zOpt) {
            case 'u':
                zNetSrv_.servType = UDP; break;
            case 'h':
                zNetSrv_.p_ipAddr = optarg; break;
            case 'p':
                zNetSrv_.p_port = optarg; break;
            case 'H':
                zPgLogin_.p_host = optarg; break;
            case 'A':
                zPgLogin_.p_addr = optarg; break;
            case 'P':
                zPgLogin_.p_port = optarg; break;
            case 'U':
                zPgLogin_.p_userName = optarg; break;
            case 'F':
                zPgLogin_.p_passFilePath = optarg; break;
            case 'D':
                zPgLogin_.p_dbName = optarg; break;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPrint_Time();
                fprintf(stderr,
                        "\n\033[31;01m==== Invalid option: [-%c] ====\033[00m\n"
                        "Usage:\n"
                        "%s\n"
                        "[-u]  /* UDP or TCP */\n"
                        "[-h host]  /* host name or domain name or host IPv4 address */\n"
                        "[-p tcp_port]  /* tcp serv port */\n"
                        "[-H postgreSQL_host]  /* PQdb host name or domain name, default 'localhost' */\n"
                        "[-A postgreSQL_addr ]  /* PQdb host IPv4 addr, if exist, '-H' will be ignored */\n"
                        "[-P postgreSQL_port]  /* PQdb host serv port, default '5432' */\n"
                        "[-U postgreSQL_username]  /* PQdb login name, default 'git' */\n"
                        "[-F postgreSQL_passfile]  /* PQdb pass file, default '$HOME/.pgpass' */\n"
                        "[-D postgreSQL_DBname]  /* which PQdb database to login, default 'dpDB' */\n",
                        optopt, zppArgv[0]);
                exit(1);
           }
    }


    /* 启动服务 */
    zRun_.run(&zNetSrv_, &zPgLogin_);
}
