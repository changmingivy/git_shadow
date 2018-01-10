#include "zCommon.h"
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>
#include <time.h>
#include <errno.h>

#include "zRun.h"

char *zpProcName = NULL;
size_t zProcNameBufLen = 0;

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

_i
main(_i zArgc, char **zppArgv) {
    zArgvInfo__ zArgvInfo_ = { NULL, NULL, NULL, NULL, NULL, NULL };
    for (_i i = 0; i < zArgc; i++) {
        zProcNameBufLen += 1 + strlen(zppArgv[i]);
    }

    /*
     * mmap shared 的主进程及所有项目进程共享的区域 
     * 存放系统全局信息
     */
    if (MAP_FAILED ==
            (zRun_.p_sysInfo_ = mmap(NULL, sizeof(zSysInfo__), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0))) {
        zPRINT_ERR_EASY_SYS();
        exit(1);
    }

    /* 提取命令行参数 */
    _i zOpt = 0;
    while (-1 != (zOpt = getopt(zArgc, zppArgv, "x:u:h:p:H:P:U:F:D:"))) {
        switch (zOpt) {
            case 'x':
                zMEM_ALLOC(zRun_.p_sysInfo_->p_servPath, char, 1 + strlen(optarg));
                strcpy(zRun_.p_sysInfo_->p_servPath, optarg);
                break;
            case 'u':
                zMEM_ALLOC(zRun_.p_sysInfo_->p_loginName, char, 1 + strlen(optarg));
                strcpy(zRun_.p_sysInfo_->p_loginName, optarg);
                break;
            case 'h':
                zMEM_ALLOC(zRun_.p_sysInfo_->netSrv_.p_ipAddr, char, 1 + strlen(optarg));
                strcpy(zRun_.p_sysInfo_->netSrv_.p_ipAddr, optarg);

                /* git push 会用此字符串作为分支名称的一部分 */
                snprintf(zRun_.p_sysInfo_->netSrv_.specStrForGit, INET6_ADDRSTRLEN,
                        "%s",
                        zRun_.p_sysInfo_->netSrv_.p_ipAddr);

                for (_i i = 0; '\0' != zRun_.p_sysInfo_->netSrv_.specStrForGit[i]; i++) {
                    if (':' == zRun_.p_sysInfo_->netSrv_.specStrForGit[i]) {
                        zRun_.p_sysInfo_->netSrv_.specStrForGit[i] = '_';
                    }
                }

                break;
            case 'p':
                zMEM_ALLOC(zRun_.p_sysInfo_->netSrv_.p_port, char, 1 + strlen(optarg));
                strcpy(zRun_.p_sysInfo_->netSrv_.p_port, optarg);
                break;
            case 'H':
                zMEM_ALLOC(zArgvInfo_.p_pgHost, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgHost, optarg);
                break;
            case 'A':
                zMEM_ALLOC(zArgvInfo_.p_pgAddr, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgAddr, optarg);
                break;
            case 'P':
                zMEM_ALLOC(zArgvInfo_.p_pgPort, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgPort, optarg);
                break;
            case 'U':
                zMEM_ALLOC(zArgvInfo_.p_pgUserName, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgUserName, optarg);
                break;
            case 'F':
                zMEM_ALLOC(zArgvInfo_.p_pgPassFilePath, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgPassFilePath, optarg);
                break;
            case 'D':
                zMEM_ALLOC(zArgvInfo_.p_pgDBName, char, 1 + strlen(optarg));
                strcpy(zArgvInfo_.p_pgDBName, optarg);
                break;
            default: // zOpt == '?'  // 若指定了无效的选项，报错退出
                zPRINT_TIME();
                fprintf(stderr,
                        "\n\033[31;01m==== Invalid option: [-%c] ====\033[00m\n"
                        "Usage:\n"
                        "%s\n"
                        "[-x serv_path]  /* server root path on master, usually /home/git/zgit_shadow */\n"
                        "[-u login_name]  /* username on server */\n"
                        "[-h host]  /* host name or domain name or host IP address */\n"
                        "[-p tcp_port]  /* tcp serv port */\n"
                        "[-H postgreSQL_host]  /* PQdb host name or domain name, default 'localhost' */\n"
                        "[-A postgreSQL_addr ]  /* PQdb host IP addr, if exist, '-H' will be ignored */\n"
                        "[-P postgreSQL_port]  /* PQdb host serv port, default '5432' */\n"
                        "[-U postgreSQL_username]  /* PQdb login name, default 'git' */\n"
                        "[-F postgreSQL_passfile]  /* PQdb pass file, default '$HOME/.pgpass' */\n"
                        "[-D postgreSQL_DBname]  /* which PQdb database to login, default 'dpDB' */\n",
                        optopt,
                        zppArgv[0]);
                exit(1);
           }
    }

    /* 主进程名称定制 */
    memset(zppArgv[0], 0, zProcNameBufLen);
    snprintf(zppArgv[0], zProcNameBufLen,
            "git_shadow|==> ID: MASTER");

    /* 项目进程使用此指针定制自身名称 */
    zpProcName = zppArgv[0];

    /* 启动主服务 */
    zRun_.run(& zArgvInfo_);
}
