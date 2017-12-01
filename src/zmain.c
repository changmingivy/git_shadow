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

extern struct zRun__ zRun_;

_i
main(_i zArgc, char **zppArgv) {
    _i zOpt = 0;
    while (-1 != (zOpt = getopt(zArgc, zppArgv, "u:h:p:H:P:U:F:D:"))) {
        switch (zOpt) {
            case 'u':
                zRun_.p_loginName = optarg; break;
            case 'h':
                zRun_.netSrv_.p_ipAddr = optarg; break;
            case 'p':
                zRun_.netSrv_.p_port = optarg; break;
            case 'H':
                zRun_.pgLogin_.p_host = optarg; break;
            case 'A':
                zRun_.pgLogin_.p_addr = optarg; break;
            case 'P':
                zRun_.pgLogin_.p_port = optarg; break;
            case 'U':
                zRun_.pgLogin_.p_userName = optarg; break;
            case 'F':
                zRun_.pgLogin_.p_passFilePath = optarg; break;
            case 'D':
                zRun_.pgLogin_.p_dbName = optarg; break;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPrint_Time();
                fprintf(stderr,
                        "\n\033[31;01m==== Invalid option: [-%c] ====\033[00m\n"
                        "Usage:\n"
                        "%s\n"
                        "[-u login_name]  /* username on server */\n"
                        "[-h host]  /* host name or domain name or host IPv4 address */\n"
                        "[-p tcp_port]  /* tcp serv port */\n"
                        "[-H postgreSQL_host]  /* PQdb host name or domain name, default 'localhost' */\n"
                        "[-A postgreSQL_addr ]  /* PQdb host IPv4 addr, if exist, '-H' will be ignored */\n"
                        "[-P postgreSQL_port]  /* PQdb host serv port, default '5432' */\n"
                        "[-U postgreSQL_username]  /* PQdb login name, default 'git' */\n"
                        "[-F postgreSQL_passfile]  /* PQdb pass file, default '$HOME/.pgpass' */\n"
                        "[-D postgreSQL_DBname]  /* which PQdb database to login, default 'dpDB' */\n",
                        optopt,
                        zppArgv[0]);
                exit(1);
           }
    }

    /* 启动主服务 */
    zRun_.run();
}
