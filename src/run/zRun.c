#include "zRun.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

extern struct zThreadPool__ zThreadPool_;
extern struct zNetUtils__ zNetUtils_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zNativeOps__ zNativeOps_;
extern struct zDpOps__ zDpOps_;
extern struct zPgSQL__ zPgSQL_;
extern struct zLibGit__ zLibGit_;

static void zstart_server(zPgLogin__ *zpPgLogin_);
static void * zops_route_tcp_master(void *zp);
static void * zops_route_tcp (void *zp);

static void * zudp_daemon(void *zp);
static void * zops_route_udp (void *zp);

static _i zhistory_import (cJSON *zpJ, _i zSd);

struct zRun__ zRun_ = {
    .run = zstart_server,

    /* mmap in 'main' func */
    .p_sysInfo_ = NULL,
};

/* 不允许并发新建项目 */
static pthread_mutex_t zRepoCreatLock = PTHREAD_MUTEX_INITIALIZER;

/*
 * 项目进程内部分配空间，
 * 主进程中不可见
 */
zRepo__ *zpRepo_;


void
zerr_vec_init(void) {
    zRun_.p_sysInfo_->p_errVec[0] = "";
    zRun_.p_sysInfo_->p_errVec[1] = "无法识别或未定义的操作请求";
    zRun_.p_sysInfo_->p_errVec[2] = "项目不存在或正在创建过程中";
    zRun_.p_sysInfo_->p_errVec[3] = "指定的版本号不存在";
    zRun_.p_sysInfo_->p_errVec[4] = "指定的文件 ID 不存在";
    zRun_.p_sysInfo_->p_errVec[5] = "";
    zRun_.p_sysInfo_->p_errVec[6] = "项目被锁定，请解锁后重试";
    zRun_.p_sysInfo_->p_errVec[7] = "服务端接收到的数据无法解析";
    zRun_.p_sysInfo_->p_errVec[8] = "已产生新的布署记录，请刷新页面";
    zRun_.p_sysInfo_->p_errVec[9] = "服务端错误：缓冲区容量不足，无法解析网络数据";
    zRun_.p_sysInfo_->p_errVec[10] = "请求的数据类型错误：非提交记录或布署记录";
    zRun_.p_sysInfo_->p_errVec[11] = "系统忙，请两秒后重试...";
    zRun_.p_sysInfo_->p_errVec[12] = "布署失败";
    zRun_.p_sysInfo_->p_errVec[13] = "正在布署过程中，或上一次布署失败，查看最近一次布署动作的实时进度";
    zRun_.p_sysInfo_->p_errVec[14] = "用户指定的布署后命令执行失败";
    zRun_.p_sysInfo_->p_errVec[15] = "服务端布署前动作出错";
    zRun_.p_sysInfo_->p_errVec[16] = "系统当前负载太高，请稍稍后重试";
    zRun_.p_sysInfo_->p_errVec[17] = "IPnum ====> IPstr 失败";
    zRun_.p_sysInfo_->p_errVec[18] = "IPstr ====> IPnum 失败";
    zRun_.p_sysInfo_->p_errVec[19] = "指定的目标机列表中存在重复 IP";
    zRun_.p_sysInfo_->p_errVec[20] = "";
    zRun_.p_sysInfo_->p_errVec[21] = "";
    zRun_.p_sysInfo_->p_errVec[22] = "";
    zRun_.p_sysInfo_->p_errVec[23] = "部分或全部目标机初始化失败";
    zRun_.p_sysInfo_->p_errVec[24] = "前端没有指明目标机总数";
    zRun_.p_sysInfo_->p_errVec[25] = "";
    zRun_.p_sysInfo_->p_errVec[26] = "";
    zRun_.p_sysInfo_->p_errVec[27] = "";
    zRun_.p_sysInfo_->p_errVec[28] = "指定的目标机总数与实际解析出的数量不一致";
    zRun_.p_sysInfo_->p_errVec[29] = "指定的项目路径不合法";
    zRun_.p_sysInfo_->p_errVec[30] = "指定项目路径不是目录，存在非目录文件与之同名";
    zRun_.p_sysInfo_->p_errVec[31] = "SSHUserName 字段太长(>255 char)";
    zRun_.p_sysInfo_->p_errVec[32] = "指定的项目 ID 不合法(1 - 1023)";
    zRun_.p_sysInfo_->p_errVec[33] = "服务端无法创建指定的项目路径";
    zRun_.p_sysInfo_->p_errVec[34] = "项目信息格式错误：信息不足或存在不合法字段";
    zRun_.p_sysInfo_->p_errVec[35] = "项目 ID 已存在";
    zRun_.p_sysInfo_->p_errVec[36] = "服务端项目路径已存在";
    zRun_.p_sysInfo_->p_errVec[37] = "未指定远程代码库的版本控制系统类型：git";
    zRun_.p_sysInfo_->p_errVec[38] = "";
    zRun_.p_sysInfo_->p_errVec[39] = "SSHPort 字段太长(>5 char)";
    zRun_.p_sysInfo_->p_errVec[40] = "服务端项目路径操作错误";
    zRun_.p_sysInfo_->p_errVec[41] = "服务端 git 库异常";
    zRun_.p_sysInfo_->p_errVec[42] = "git clone 错误";
    zRun_.p_sysInfo_->p_errVec[43] = "git config 错误";
    zRun_.p_sysInfo_->p_errVec[44] = "git branch 错误";
    zRun_.p_sysInfo_->p_errVec[45] = "git add and commit 错误";
    zRun_.p_sysInfo_->p_errVec[46] = "libgit2 初始化错误";
    zRun_.p_sysInfo_->p_errVec[47] = "git rev_walker err";
    zRun_.p_sysInfo_->p_errVec[48] = "";
    zRun_.p_sysInfo_->p_errVec[49] = "指定的源库分支无效/同步失败";
    zRun_.p_sysInfo_->p_errVec[50] = "";
    zRun_.p_sysInfo_->p_errVec[51] = "";
    zRun_.p_sysInfo_->p_errVec[52] = "";
    zRun_.p_sysInfo_->p_errVec[53] = "";
    zRun_.p_sysInfo_->p_errVec[54] = "";
    zRun_.p_sysInfo_->p_errVec[55] = "";
    zRun_.p_sysInfo_->p_errVec[56] = "";
    zRun_.p_sysInfo_->p_errVec[57] = "";
    zRun_.p_sysInfo_->p_errVec[58] = "";
    zRun_.p_sysInfo_->p_errVec[59] = "";
    zRun_.p_sysInfo_->p_errVec[60] = "";
    zRun_.p_sysInfo_->p_errVec[61] = "";
    zRun_.p_sysInfo_->p_errVec[62] = "";
    zRun_.p_sysInfo_->p_errVec[63] = "";
    zRun_.p_sysInfo_->p_errVec[64] = "";
    zRun_.p_sysInfo_->p_errVec[65] = "";
    zRun_.p_sysInfo_->p_errVec[66] = "";
    zRun_.p_sysInfo_->p_errVec[67] = "";
    zRun_.p_sysInfo_->p_errVec[68] = "";
    zRun_.p_sysInfo_->p_errVec[69] = "";
    zRun_.p_sysInfo_->p_errVec[70] = "无内容 或 服务端版本号列表缓存错误";
    zRun_.p_sysInfo_->p_errVec[71] = "无内容 或 服务端差异文件列表缓存错误";
    zRun_.p_sysInfo_->p_errVec[72] = "无内容 或 服务端单个文件的差异内容缓存错误";
    zRun_.p_sysInfo_->p_errVec[73] = "";
    zRun_.p_sysInfo_->p_errVec[74] = "";
    zRun_.p_sysInfo_->p_errVec[75] = "";
    zRun_.p_sysInfo_->p_errVec[76] = "";
    zRun_.p_sysInfo_->p_errVec[77] = "";
    zRun_.p_sysInfo_->p_errVec[78] = "";
    zRun_.p_sysInfo_->p_errVec[79] = "";
    zRun_.p_sysInfo_->p_errVec[80] = "目标机请求下载的文件路径不存在或无权访问";
    zRun_.p_sysInfo_->p_errVec[81] = "同一目标机的同一次布署动作，收到重复的状态确认";
    zRun_.p_sysInfo_->p_errVec[82] = "无法创建 <PATH>_SHADOW/____post-deploy.sh 文件";
    zRun_.p_sysInfo_->p_errVec[83] = "";
    zRun_.p_sysInfo_->p_errVec[84] = "";
    zRun_.p_sysInfo_->p_errVec[85] = "";
    zRun_.p_sysInfo_->p_errVec[86] = "";
    zRun_.p_sysInfo_->p_errVec[87] = "";
    zRun_.p_sysInfo_->p_errVec[88] = "";
    zRun_.p_sysInfo_->p_errVec[89] = "";
    zRun_.p_sysInfo_->p_errVec[90] = "数据库连接失败";
    zRun_.p_sysInfo_->p_errVec[91] = "SQL 命令执行失败";
    zRun_.p_sysInfo_->p_errVec[92] = "SQL 执行结果错误";  /* 发生通常代表存在 BUG */
    zRun_.p_sysInfo_->p_errVec[93] = "";
    zRun_.p_sysInfo_->p_errVec[94] = "";
    zRun_.p_sysInfo_->p_errVec[95] = "";
    zRun_.p_sysInfo_->p_errVec[96] = "";
    zRun_.p_sysInfo_->p_errVec[97] = "";
    zRun_.p_sysInfo_->p_errVec[98] = "";
    zRun_.p_sysInfo_->p_errVec[99] = "";
    zRun_.p_sysInfo_->p_errVec[100] = "";
    zRun_.p_sysInfo_->p_errVec[101] = "目标机返回的版本号与正在布署的不一致";
    zRun_.p_sysInfo_->p_errVec[102] = "目标机 post-update 出错返回";
    zRun_.p_sysInfo_->p_errVec[103] = "收到未知的目标机 IP";
    zRun_.p_sysInfo_->p_errVec[104] = "";
    zRun_.p_sysInfo_->p_errVec[105] = "";
    zRun_.p_sysInfo_->p_errVec[106] = "";
    zRun_.p_sysInfo_->p_errVec[107] = "";
    zRun_.p_sysInfo_->p_errVec[108] = "";
    zRun_.p_sysInfo_->p_errVec[109] = "";
    zRun_.p_sysInfo_->p_errVec[110] = "";
    zRun_.p_sysInfo_->p_errVec[111] = "";
    zRun_.p_sysInfo_->p_errVec[112] = "";
    zRun_.p_sysInfo_->p_errVec[113] = "";
    zRun_.p_sysInfo_->p_errVec[114] = "";
    zRun_.p_sysInfo_->p_errVec[115] = "";
    zRun_.p_sysInfo_->p_errVec[116] = "";
    zRun_.p_sysInfo_->p_errVec[117] = "";
    zRun_.p_sysInfo_->p_errVec[118] = "";
    zRun_.p_sysInfo_->p_errVec[119] = "";
    zRun_.p_sysInfo_->p_errVec[120] = "";
    zRun_.p_sysInfo_->p_errVec[121] = "";
    zRun_.p_sysInfo_->p_errVec[122] = "";
    zRun_.p_sysInfo_->p_errVec[123] = "";
    zRun_.p_sysInfo_->p_errVec[124] = "";
    zRun_.p_sysInfo_->p_errVec[125] = "";
    zRun_.p_sysInfo_->p_errVec[126] = "服务端操作系统错误";
    zRun_.p_sysInfo_->p_errVec[127] = "被新的布署请求打断";
}

void
zserv_vec_init(void) {
    /*
     * TCP、UDP 路由函数
     */
    zRun_.p_sysInfo_->route_tcp = zops_route_tcp,
    zRun_.p_sysInfo_->route_udp = zops_route_udp,

    /*
     * TCP serv vec
     * 索引范围：0 至 zTCP_SERV_HASH_SIZ - 1
     */
    zRun_.p_sysInfo_->ops_tcp[0] = zDpOps_.tcp_pang;  /* 目标机使用此接口测试与服务端的连通性 */
    zRun_.p_sysInfo_->ops_tcp[1] = zDpOps_.creat;  /* 创建新项目 */
    zRun_.p_sysInfo_->ops_tcp[2] = zDpOps_.sys_update;  /* 系统文件升级接口：下一次布署时需要重新初始化所有目标机 */
    zRun_.p_sysInfo_->ops_tcp[3] = zDpOps_.repo_update;  /* 源库URL或分支更改 */
    zRun_.p_sysInfo_->ops_tcp[4] = NULL;  /* 删除项目接口预留 */
    zRun_.p_sysInfo_->ops_tcp[5] = zhistory_import;  /* 临时接口，用于导入旧版系统已产生的数据 */
    zRun_.p_sysInfo_->ops_tcp[6] = NULL;
    zRun_.p_sysInfo_->ops_tcp[7] = zDpOps_.glob_res_confirm;  /* 目标机自身布署成功之后，向服务端核对全局结果，若全局结果是失败，则执行回退 */
    zRun_.p_sysInfo_->ops_tcp[8] = zDpOps_.state_confirm;  /* 远程主机初始经状态、布署结果状态、错误信息 */
    zRun_.p_sysInfo_->ops_tcp[9] = zDpOps_.print_revs;  /* 显示提交记录或布署记录 */
    zRun_.p_sysInfo_->ops_tcp[10] = zDpOps_.print_diff_files;  /* 显示差异文件路径列表 */
    zRun_.p_sysInfo_->ops_tcp[11] = zDpOps_.print_diff_contents;  /* 显示差异文件内容 */
    zRun_.p_sysInfo_->ops_tcp[12] = zDpOps_.dp;  /* 批量布署或撤销 */
    zRun_.p_sysInfo_->ops_tcp[13] = NULL;
    zRun_.p_sysInfo_->ops_tcp[14] = zDpOps_.req_file;  /* 请求服务器发送指定的文件 */
    zRun_.p_sysInfo_->ops_tcp[15] = zDpOps_.show_dp_process;  /* 查询指定项目的详细信息及最近一次的布署进度 */

    /* UDP serv vec */
    zRun_.p_sysInfo_->ops_udp[0] = zDpOps_.udp_pang;
    zRun_.p_sysInfo_->ops_udp[1] = NULL;
    zRun_.p_sysInfo_->ops_udp[2] = NULL;
    zRun_.p_sysInfo_->ops_udp[3] = NULL;
    zRun_.p_sysInfo_->ops_udp[4] = NULL;
    zRun_.p_sysInfo_->ops_udp[5] = NULL;
    zRun_.p_sysInfo_->ops_udp[6] = NULL;
    zRun_.p_sysInfo_->ops_udp[7] = NULL;
    zRun_.p_sysInfo_->ops_udp[8] = NULL;
    zRun_.p_sysInfo_->ops_udp[9] = NULL;
}

/************
 * 网络服务 *
 ************/
static void
zexit_clean(void) {
    /*
     * 进程退出时
     * 清理同一进程组的所有进程
     */
    kill(0, SIGUSR1);
}

#ifndef _Z_BSD
/*
 * 定时获取系统全局负载信息
 */
static void *
zsys_load_monitor(void *zp __attribute__ ((__unused__))) {
    FILE *zpHandler = NULL;
    _ul zTotalMem = 0,
        zAvalMem = 0;

    zCHECK_NULL_EXIT( zpHandler = fopen("/proc/meminfo", "r") );

    while(1) {
        fscanf(zpHandler, "%*s %ld %*s %*s %*ld %*s %*s %ld", &zTotalMem, &zAvalMem);
        zRun_.p_sysInfo_->memLoad = 100 * (zTotalMem - zAvalMem) / zTotalMem;
        fseek(zpHandler, 0, SEEK_SET);

        zNativeUtils_.sleep(0.1);
    }

    return NULL;
}
#endif

/*
 * 提取必要的基础信息
 * 只需在主进程执行一次，项目进程会继承之
 */
static void
zglob_data_config(zPgLogin__ *zpPgLogin_) {
    struct passwd *zpPWD = NULL;
    char zDBPassFilePath[1024];
    struct stat zS_;

    zRun_.p_sysInfo_->udp_daemon = zudp_daemon,

    zRun_.p_sysInfo_->globRepoNumLimit = zGLOB_REPO_NUM_LIMIT;

    if (NULL == zRun_.p_sysInfo_->p_loginName) {
        zRun_.p_sysInfo_->p_loginName = "git";
    }

    zCHECK_NULL_EXIT( zpPWD = getpwnam(zRun_.p_sysInfo_->p_loginName) );
    zRun_.p_sysInfo_->p_homePath = zpPWD->pw_dir;
    zRun_.p_sysInfo_->homePathLen = strlen(zRun_.p_sysInfo_->p_homePath);

    zMEM_ALLOC(zRun_.p_sysInfo_->p_sshPubKeyPath, char, zRun_.p_sysInfo_->homePathLen + sizeof("/.ssh/id_rsa.pub"));
    sprintf(zRun_.p_sysInfo_->p_sshPubKeyPath, "%s/.ssh/id_rsa.pub", zRun_.p_sysInfo_->p_homePath);

    zMEM_ALLOC(zRun_.p_sysInfo_->p_sshPrvKeyPath, char, zRun_.p_sysInfo_->homePathLen + sizeof("/.ssh/id_rsa"));
    sprintf(zRun_.p_sysInfo_->p_sshPrvKeyPath, "%s/.ssh/id_rsa", zRun_.p_sysInfo_->p_homePath);

    /* 确保 pgSQL 密钥文件存在并合法 */
    if (NULL == zpPgLogin_->p_passFilePath) {
        snprintf(zDBPassFilePath, 1024,
                "%s/.pgpass",
                zRun_.p_sysInfo_->p_homePath);

        zpPgLogin_->p_passFilePath = zDBPassFilePath;
    }

    zCHECK_NOTZERO_EXIT( stat(zpPgLogin_->p_passFilePath, &zS_) );

    if (! S_ISREG(zS_.st_mode)) {
        zPRINT_ERR_EASY("");
        exit(1);
    }

    zCHECK_NOTZERO_EXIT( chmod(zpPgLogin_->p_passFilePath, 00600) );

    /* 生成连接 pgSQL 的元信息 */
    snprintf(zRun_.p_sysInfo_->pgConnInfo, 2048,
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "%s%s "
            "sslmode=allow "
            "connect_timeout=6",
            NULL == zpPgLogin_->p_addr ? "host=" : "",
            NULL == zpPgLogin_->p_addr ? (NULL == zpPgLogin_->p_host ? zRun_.p_sysInfo_->p_servPath : zpPgLogin_->p_host) : "",
            NULL == zpPgLogin_->p_addr ? "" : "hostaddr=",
            NULL == zpPgLogin_->p_addr ? "" : zpPgLogin_->p_addr,
            (NULL == zpPgLogin_->p_addr && NULL == zpPgLogin_->p_host)? "" : (NULL == zpPgLogin_->p_port ? "" : "port="),
            (NULL == zpPgLogin_->p_addr && NULL == zpPgLogin_->p_host)? "" : (NULL == zpPgLogin_->p_port ? "" : zpPgLogin_->p_port),
            "user=",
            NULL == zpPgLogin_->p_userName ? "git" : zpPgLogin_->p_userName,
            "passfile=",
            zpPgLogin_->p_passFilePath,
            "dbname=",
            NULL == zpPgLogin_->p_dbName ? "dpDB": zpPgLogin_->p_dbName);

    /* 初始化 serv_map 与 err_map */
    zserv_vec_init();
    zerr_vec_init();
}

/*
 * 服务启动入口
 */
static void
zstart_server(zPgLogin__ *zpPgLogin_) {
    /* 必须指定服务端的根路径 */
    if (NULL == zRun_.p_sysInfo_->p_servPath) {
        zPRINT_ERR(0, NULL, "==== !!! FATAL !!! ====");
        exit(1);
    }

    /* 检查 pgSQL 运行环境是否是线程安全的 */
    if (zFalse == zPgSQL_.thread_safe_check()) {
        zPRINT_ERR(0, NULL, "==== !!! FATAL !!! ====");
        exit(1);
    }

    /* 转换为后台守护进程 */
    zNativeUtils_.daemonize(zRun_.p_sysInfo_->p_servPath);

    /* 全局共享数据注册 */
    zglob_data_config(zpPgLogin_);

    /*
     * !!! 必须在初始化项目库之前运行
     * 主进程常备线程数量：32
     * 项目进程常备线程数量：8
     * 系统全局可启动线程数上限 1024
     */
    zThreadPool_.init(32, 1024);

    /*
     * 项目库初始化
     * 每个项目对应一个独立的进程
     */
    zNativeOps_.repo_init_all();

    /*
     * 主进程退出时，清理所有项目进程
     * 必须在项目进程启动之后执行，
     * 否则任一项目进程退出，都会触发清理动作
     */
    atexit(zexit_clean);

    /*
     * 只运行于主进程
     * 用于目标机监控数据收集
     * DB 表分区管理，由各项目进程负责
     */
    zThreadPool_.add(zudp_daemon, NULL);

#ifndef _Z_BSD
    zThreadPool_.add(zsys_load_monitor, NULL);
#endif

    /*
     * 返回的 socket 已经做完 bind 和 listen
     * 若出错，其内部会 exit
     */
    _i zMajorSd = zNetUtils_.gen_serv_sd(
            zRun_.p_sysInfo_->netSrv_.p_ipAddr,
            zRun_.p_sysInfo_->netSrv_.p_port,
            NULL,
            zProtoTCP);

    /*
     * 会传向新线程，使用静态变量
     * 使用数组防止负载高时造成线程参数混乱
     */
    static _i zSd[256] = {0};
    _uc zReqId = 0;
    for (_ui i = 0;; i++) {
        zReqId = i % 256;
        if (-1 == (zSd[zReqId] = accept(zMajorSd, NULL, 0))) {
            zPRINT_ERR_EASY_SYS();
        } else {
            zThreadPool_.add(zops_route_tcp_master, & zSd[zReqId]);
        }
    }
}

/*
 * 主进程路由函数
 */
static void *
zops_route_tcp_master(void *zp) {
    _i zSd = * ((_i *) zp);

    char zDataBuf[16] = {'\0'};
    _i zRepoId = 0;

    /*
     * 必须使用 MSG_PEEK 标志
     * json repoId 字段必须是第一个字段：
     * 格式：{"repoId":1,"...":...}
     */
    recv(zSd, zDataBuf, zBYTES(16), MSG_PEEK|MSG_NOSIGNAL);

    /*
     * 若没有提取到数据，结果是 0
     * 项目 ID 范围：1 - (zRun_.p_sysInfo_->globRepoNumLimit - 1)
     * 不允许使用 0
     */
    zRepoId = strtol(zDataBuf + sizeof("{\"repoId\":") - 1, NULL, 10);
    if (0 >= zRepoId
            || 0 != strncmp("{\"repoId\":", zDataBuf, sizeof("{\"repoId\":") - 1)) {
        zNetUtils_.send(zSd,
                "{\"errNo\":-7,\"content\":\"json parse err\"}",
                sizeof("{\"errNo\":-7,\"content\":\"json parse err\"}") - 1);
        goto zMarkEnd;
    }

    if (zRun_.p_sysInfo_->globRepoNumLimit <= zRepoId) {
        zNetUtils_.send(zSd,
                "{\"errNo\":-32,\"content\":\"repoId too large (hint: 1 - 1023)\"}",
                sizeof("{\"errNo\":-32,\"content\":\"repoId too large (hint: 1 - 1023)\"}") - 1);
        goto zMarkEnd;
    }

    /*
     * 若项目存在，将业务 socket 传递给对应的项目进程
     * 若项目不存在，则收取完整的 json 信息
     */
    if (0 < zRun_.p_sysInfo_->repoFinMark[zRepoId]) {
        zNetUtils_.send_fd(zRun_.p_sysInfo_->repoUN[zRepoId], zSd);
        goto zMarkEnd;
    } else {
        char zDataBuf[8192] = {'\0'};
        _i zDataLen = 0,
        zResNo = 0;

        cJSON *zpJRoot = NULL;
        cJSON *zpOpsId = NULL;

        recv(zSd, zDataBuf, zBYTES(8192), MSG_NOSIGNAL);

        zpJRoot = cJSON_Parse(zDataBuf);
        zpOpsId = cJSON_GetObjectItemCaseSensitive(zpJRoot, "opsId");

        if (cJSON_IsNumber(zpOpsId)) {
            _i zOpsId = zpOpsId->valueint;

            /*
             * 若 opsId 指示的是新建项目，
             * 则新建，否则返回项目不存在
             * 不允许并发新建项目
             */
            if (1 == zOpsId) {
                pthread_mutex_lock(& zRepoCreatLock);

                if (0 != (zResNo = zRun_.p_sysInfo_->ops_tcp[1](zpJRoot, zSd))) {
                    zDataLen = snprintf(zDataBuf, 8192,
                            "{\"errNo\":%d,\"content\":\"[opsId: %d] %s\"}",
                            zResNo,
                            zOpsId,
                            zRun_.p_sysInfo_->p_errVec[-1 * zResNo]);
                    zNetUtils_.send(zSd, zDataBuf, zDataLen);
                }

                pthread_mutex_unlock(& zRepoCreatLock);
            } else if (0 == zOpsId) {
                /* ping-pang 接口不检查结果 */
                zRun_.p_sysInfo_->ops_tcp[1](zpJRoot, zSd);
            } else {
                zDataLen = snprintf(zDataBuf, 8192,
                        "{\"errNo\":-2,\"content\":\"%s\"}",
                        zRun_.p_sysInfo_->p_errVec[2]);

                zNetUtils_.send(zSd, zDataBuf, zDataLen);
            }
        } else {
            zDataLen = snprintf(zDataBuf, 8192,
                    "{\"errNo\":-7,\"content\":\"%s\"}",
                    zRun_.p_sysInfo_->p_errVec[7]);

            zNetUtils_.send(zSd, zDataBuf, zDataLen);
        }

        cJSON_Delete(zpJRoot);
        goto zMarkEnd;
    }

zMarkEnd:
    close(zSd);
    return NULL;
}

/*
 * 项目进程 tcp 路由函数，用于面向用户的服务
 */
static void *
zops_route_tcp(void *zp) {
    _i zSd = * ((_i *) zp);

    char zDataBuf[4096] = {'\0'};
    char *zpDataBuf = zDataBuf;

    _i zErrNo = 0,
       zOpsId = -1,
       zDataLen = -1,
       zDataBufSiz = 4096;

    /*
     * 若收到的数据量很大，
     * 直接一次性扩展为 1024 倍(4M)的缓冲区
     */
    if (zDataBufSiz == (zDataLen = recv(zSd, zpDataBuf, zDataBufSiz, MSG_NOSIGNAL))) {
        zDataBufSiz *= 1024;
        zMEM_ALLOC(zpDataBuf, char, zDataBufSiz);
        strcpy(zpDataBuf, zDataBuf);
        zDataLen += recv(zSd, zpDataBuf + zDataLen, zDataBufSiz - zDataLen, MSG_NOSIGNAL);
    }

    /*
     * 最短的 json 字符串：{"a":}
     * 长度合计 6 字节
     */
    if (zBYTES(6) > zDataLen) {
        zPRINT_ERR(errno, NULL, "recvd data too short(< 6bytes)");
        goto zMarkEnd;
    }

    /* 提取 value[OpsId] */
    cJSON *zpJRoot = cJSON_Parse(zpDataBuf);
    cJSON *zpOpsId = cJSON_GetObjectItemCaseSensitive(zpJRoot, "opsId");
    if (cJSON_IsNumber(zpOpsId)) {
        zOpsId = zpOpsId->valueint;

        /* 检验 value[OpsId] 合法性 */
        if (0 > zOpsId || zTCP_SERV_HASH_SIZ <= zOpsId || NULL == zRun_.p_sysInfo_->ops_tcp[zOpsId]) {
            zErrNo = -1;
        } else {
            zErrNo = zRun_.p_sysInfo_->ops_tcp[zOpsId](zpJRoot, zSd);
        }
    } else {
        zErrNo = -1;
    }
    cJSON_Delete(zpJRoot);

    /*
     * 成功状态及特殊的错误信息在执行函数中直接回复
     * 通用的错误状态返回至此处统一处理
     */
    if (0 > zErrNo) {
        /* 无法解析的数据，打印出其原始信息 */
        if (-1 == zErrNo) {
            // fprintf(stderr, "\342\224\224\342\224\200\342\224\200\033[31;01m[OrigMsg]:\033[00m %s\n", zpDataBuf);
            fprintf(stderr, "\n\033[31;01m[OrigMsg]:\033[00m %s\n", zpDataBuf);
        }

        if (14 != zOpsId) {
            zDataLen = snprintf(zpDataBuf, zDataBufSiz,
                    "{\"errNo\":%d,\"content\":\"[opsId: %d] %s\"}",
                    zErrNo,
                    zOpsId,
                    zRun_.p_sysInfo_->p_errVec[-1 * zErrNo]);
            zNetUtils_.send(zSd, zpDataBuf, zDataLen);
        }
    }

zMarkEnd:
    close(zSd);
    if (zpDataBuf != &(zDataBuf[0])) {
        free(zpDataBuf);
    }

    return NULL;
}

/*
 * 返回的 udp socket 已经做完 bind，若出错，其内部会 exit
 * 收到的内容会传向新线程，
 * 使用静态变量数组防止负载高时造成线程参数混乱
 */
static void *
zudp_daemon(void *zpUNPath) {
    _i zServSd = -1;
    _uc zReqId = 0;

    if (NULL == zpUNPath) {
        zServSd = zNetUtils_.gen_serv_sd(
                zRun_.p_sysInfo_->netSrv_.p_ipAddr,
                zRun_.p_sysInfo_->netSrv_.p_port,
                NULL,
                zProtoUDP);

        /*
         * 监控数据收集服务
         * 单个消息长度不能超过 510
         */
        static zUdpInfo__ zUdpInfo_[256];
        _ui i = 0;

        for (; i < 256; i++) {
            zUdpInfo_[i].sd = zServSd;
        }

        for (;; i++) {
            zReqId = i % 256;
            recvfrom(zServSd, zUdpInfo_[zReqId].data, zBYTES(510), MSG_NOSIGNAL,
                    & zUdpInfo_[zReqId].peerAddr,
                    & zUdpInfo_[zReqId].peerAddrLen);
            zThreadPool_.add(zops_route_udp, & zUdpInfo_[zReqId]);
        }
    } else {
        zServSd = zNetUtils_.gen_serv_sd(
                NULL,
                NULL,
                zpUNPath,
                zProtoUDP);

        /*
         * TCP 套接字进程间传递服务
         */
        static _i zSd[256] = {0};
        for (_ui i = 0;; i++) {
            zReqId = i % 256;
            if (0 > (zSd[zReqId] = zNetUtils_.recv_fd(zServSd))) {
                zPRINT_ERR_EASY_SYS();
            } else {
                zThreadPool_.add(zops_route_tcp, & zSd[zReqId]);
            }
        }
    }

    return NULL;
}

/*
 * udp 路由函数，用于服务器内部
 * 首字符充当路由索引
 * 0 在 ansi 表中对应值是 48
 * 故，首字符减去 48，即可得到二进制格式的 0-9
 */
static void *
zops_route_udp (void *zp) {
    zUdpInfo__ zUdpInfo_;

    /* 必须第一时间复制出来 */
    memcpy(&zUdpInfo_, zp, sizeof(zUdpInfo__));

    if (47 < zUdpInfo_.data[0]
            && 58 > zUdpInfo_.data[0]
            && NULL != zRun_.p_sysInfo_->ops_udp[zUdpInfo_.data[0] - 48]
            && 0 == zRun_.p_sysInfo_->ops_udp[zUdpInfo_.data[0] - 48](
                zUdpInfo_.data + 1,
                zUdpInfo_.sd,
                & zUdpInfo_.peerAddr,
                zUdpInfo_.peerAddrLen)) {
        return NULL;
    } else {
        return (void *) -1;
    }
}

/**************************************************************
 * 临时接口，用于导入旧版布署系统的项目信息及已产生的布署日志 *
 **************************************************************/
extern struct zPosixReg__ zPosixReg_;
static _i
zhistory_import (cJSON *zpJ __attribute__ ((__unused__)), _i zSd) {
    char *zpConfPath="/home/git/zgit_shadow2/conf/master.conf";
    char zLogPathBuf[4096];

    char zDataBuf[4096];
    char zSQLBuf[1024];

    FILE *zpH0 = fopen(zpConfPath, "r");
    FILE *zpH1 = NULL;

    zPgResTuple__ zRepoMeta_;

    zRegRes__ zR_ = {
        .alloc_fn = NULL
    };

    while (NULL != zNativeUtils_.read_line(zDataBuf, 4096, zpH0)) {
        zPosixReg_.str_split(&zR_, zDataBuf, " ");

        zRepoMeta_.pp_fields = zR_.pp_rets;
        zNativeOps_.repo_init(&zRepoMeta_, -1);

        sprintf(zLogPathBuf,
                "/home/git/home/git/.____DpSystem/%s_SHADOW/log/deploy/meta",
                zR_.pp_rets[1] + sizeof("/home/git/") -1);

        zpH1 = fopen(zLogPathBuf, "r");
        while (NULL != zNativeUtils_.read_line(zDataBuf, 4096, zpH1)) {
            zDataBuf[40] = '\0';
            sprintf(zSQLBuf,
                    "INSERT INTO dp_log (repo_id,time_stamp,rev_sig,host_ip) "
                    "VALUES (%ld,%s,'%s','%s')",
                    strtol(zR_.pp_rets[0], NULL, 10), zDataBuf + 41,
                    zDataBuf,
                    "::1");
            zPgSQL_.exec_once(zRun_.p_sysInfo_->pgConnInfo, zSQLBuf, NULL);
        }

        zPosixReg_.free_res(&zR_);
    }

    zNetUtils_.send(zSd,
            "==== Import Success ====",
            sizeof("==== Import Success ====") - 1);
    return 0;
}
