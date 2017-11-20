#include "zRun.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include "zNetUtils.h"
#include "zThreadPool.h"

extern struct zThreadPool__ zThreadPool_;
extern struct zNetUtils__ zNetUtils_;
extern struct zNativeUtils__ zNativeUtils_;
extern struct zNativeOps__ zNativeOps_;
extern struct zDpOps__ zDpOps_;
extern struct zPgSQL__ zPgSQL_;

char *zpGlobLoginName = NULL;
char *zpGlobHomePath = NULL;
char *zpGlobSSHPort = "22";
char *zpGlobSSHPubKeyPath = NULL;
char *zpGlobSSHPrvKeyPath = NULL;
_i zGlobHomePathLen = 0;

static void zstart_server(zNetSrv__ *zpNetSrv_, zPgLogin__ *zpPgLogin_);
static void * zops_route (void *zpParam);

struct zRun__ zRun_ = {
    .run = zstart_server,
    .route = zops_route,
    .ops = { NULL }
};

char *zpErrVec[128];

void
zerr_vec_init(void) {
    zpErrVec[1] = "无法识别或未定义的操作请求";
    zpErrVec[2] = "项目不存在或正在创建过程中";
    zpErrVec[3] = "";
    zpErrVec[4] = "";
    zpErrVec[5] = "系统忙，请 2 秒后重试...";
    zpErrVec[6] = "项目被锁定，请解锁后重试";
    zpErrVec[7] = "服务端接收到的数据无法解析";
    zpErrVec[8] = "已产生新的布署记录，请刷新页面";
    zpErrVec[9] = "服务端错误：接收缓冲区为空或容量不足，无法解析数据";
    zpErrVec[10] = "请求的数据类型错误：非提交记录或布署记录";
    zpErrVec[11] = "正在布署，请稍后重试...";
    zpErrVec[12] = "布署失败";  /* useless.. */
    zpErrVec[13] = "上一次布署结果是失败，请重试该次布署或执行回滚";
    zpErrVec[14] = "系统测算的布署耗时较长，请稍后查看布署列表中的最新记录";
    zpErrVec[15] = "服务端布署前环境初始化失败";
    zpErrVec[16] = "系统当前负载太高，请稍稍后重试";
    zpErrVec[17] = "";
    zpErrVec[18] = "";
    zpErrVec[19] = "指定的目标机列表中存在重复 IP";
    zpErrVec[20] = "";
    zpErrVec[21] = "";
    zpErrVec[22] = "";
    zpErrVec[23] = "部分或全部目标机初始化失败";  /* useless.. */
    zpErrVec[24] = "没有在 ExtraData 字段中指明目标机总数";
    zpErrVec[25] = "";
    zpErrVec[26] = "目标机列表为空";
    zpErrVec[27] = "";
    zpErrVec[28] = "指定的目标机数量与实际解析出的数量不一致";
    zpErrVec[29] = "";
    zpErrVec[30] = "";
    zpErrVec[31] = "";
    zpErrVec[32] = "项目 ID 超出系统允许的范围";
    zpErrVec[33] = "无法创建项目路径";
    zpErrVec[34] = "项目信息格式错误：信息不足或存在不合法字段";
    zpErrVec[35] = "项目 ID 已存在";
    zpErrVec[36] = "项目路径已存在，且项目 ID 不同";
    zpErrVec[37] = "未指定远程源代码版本控制系统类型：git";
    zpErrVec[38] = "拉取远程代码失败";
    zpErrVec[39] = "";
    zpErrVec[40] = "";
    zpErrVec[41] = "";
    zpErrVec[42] = "";
    zpErrVec[43] = "";
    zpErrVec[44] = "";
    zpErrVec[45] = "";
    zpErrVec[46] = "";
    zpErrVec[47] = "";
    zpErrVec[48] = "";
    zpErrVec[49] = "";
    zpErrVec[50] = "";
    zpErrVec[51] = "";
    zpErrVec[52] = "";
    zpErrVec[53] = "";
    zpErrVec[54] = "";
    zpErrVec[55] = "";
    zpErrVec[56] = "";
    zpErrVec[57] = "";
    zpErrVec[58] = "";
    zpErrVec[59] = "";
    zpErrVec[60] = "";
    zpErrVec[61] = "";
    zpErrVec[62] = "";
    zpErrVec[63] = "";
    zpErrVec[64] = "";
    zpErrVec[65] = "";
    zpErrVec[66] = "";
    zpErrVec[67] = "";
    zpErrVec[68] = "";
    zpErrVec[69] = "";
    zpErrVec[70] = "服务端版本号列表缓存错误";
    zpErrVec[71] = "服务端差异文件列表缓存错误";
    zpErrVec[72] = "服务端单个文件的差异内容缓存错误";
    zpErrVec[73] = "服务端 Git 仓库异常!";
    zpErrVec[74] = "";
    zpErrVec[75] = "";
    zpErrVec[76] = "";
    zpErrVec[77] = "";
    zpErrVec[78] = "";
    zpErrVec[79] = "";
    zpErrVec[80] = "目标机请求下载的文件路径不存在或无权访问";
    zpErrVec[81] = "";
    zpErrVec[82] = "";
    zpErrVec[83] = "";
    zpErrVec[84] = "";
    zpErrVec[85] = "";
    zpErrVec[86] = "";
    zpErrVec[87] = "";
    zpErrVec[88] = "";
    zpErrVec[89] = "";
    zpErrVec[90] = "数据库连接失败";
    zpErrVec[91] = "SQL 命令执行失败";
    zpErrVec[92] = "";
    zpErrVec[93] = "";
    zpErrVec[94] = "";
    zpErrVec[95] = "";
    zpErrVec[96] = "";
    zpErrVec[97] = "";
    zpErrVec[98] = "";
    zpErrVec[99] = "";
    zpErrVec[100] = "fake success";
    zpErrVec[101] = "目标机返回的版本号与正在布署的不一致";
    zpErrVec[102] = "目标机 post-update 出错返回";
    zpErrVec[103] = "目标机返回的信息类型无法识别";
    zpErrVec[104] = "";
    zpErrVec[105] = "";
    zpErrVec[106] = "";
    zpErrVec[107] = "";
    zpErrVec[108] = "";
    zpErrVec[109] = "";
    zpErrVec[110] = "";
    zpErrVec[111] = "";
    zpErrVec[112] = "";
    zpErrVec[113] = "";
    zpErrVec[114] = "";
    zpErrVec[115] = "";
    zpErrVec[116] = "";
    zpErrVec[117] = "";
    zpErrVec[118] = "";
    zpErrVec[119] = "";
    zpErrVec[120] = "";
    zpErrVec[121] = "";
    zpErrVec[122] = "";
    zpErrVec[123] = "";
    zpErrVec[124] = "";
    zpErrVec[125] = "";
    zpErrVec[126] = "";
    zpErrVec[127] = "";
}

/************
 * 网络服务 *
 ************/
static void
zstart_server(zNetSrv__ *zpNetSrv_, zPgLogin__ *zpPgLogin_) {
    /* 检查 pgSQL 运行环境是否是线程安全的 */
    if (zFalse == zPgSQL_.thread_safe_check()) {
        zPrint_Err(0, NULL, "==== !!! FATAL !!! ====");
        exit(1);
    }

    /* 成为守护进程 */
    zNativeUtils_.daemonize("/");

    /* 初始化错误信息影射表 */
    zerr_vec_init();

    /* 提取 $USER 及 $HOME 等 */
    if (NULL == (zpGlobLoginName = getlogin())) { zpGlobLoginName = "git"; }
    if (NULL == (zpGlobHomePath = getenv("HOME"))) { zpGlobHomePath = "/home/git"; }
    zGlobHomePathLen = strlen(zpGlobHomePath);

    zMem_Alloc(zpGlobSSHPubKeyPath, char, strlen(zpGlobHomePath) + sizeof("/.ssh/id_rsa.pub"));
    sprintf(zpGlobSSHPubKeyPath, "%s/.ssh/id_rsa.pub", zpGlobHomePath);

    zMem_Alloc(zpGlobSSHPrvKeyPath, char, strlen(zpGlobHomePath) + sizeof("/.ssh/id_rsa"));
    sprintf(zpGlobSSHPrvKeyPath, "%s/.ssh/id_rsa", zpGlobHomePath);

    /* 线程池初始化 */
    zThreadPool_.init();

    /* 扫描所有项目库并初始化之 */
    zNativeOps_.proj_init_all(zpPgLogin_);

    /* 定时扩展 pgSQL 日志数据表的分区 */
    zThreadPool_.add(zNativeOps_.extend_pg_partition, NULL);

    /* 索引范围：0 至 zServHashSiz - 1 */
    zRun_.ops[0] = NULL;
    zRun_.ops[1] = zDpOps_.creat;  // 添加新代码库
    zRun_.ops[2] = zDpOps_.lock;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zRun_.ops[3] = zDpOps_.unlock;  // 恢复布署／撤销功能
    zRun_.ops[4] = NULL;
    zRun_.ops[5] = NULL;
    zRun_.ops[6] = zDpOps_.show_meta;  // 显示单个有效项目的元信息
    zRun_.ops[7] = NULL;
    zRun_.ops[8] = zDpOps_.state_confirm;  // 远程主机初始经状态、布署结果状态、错误信息
    zRun_.ops[9] = zDpOps_.print_revs;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zRun_.ops[10] = zDpOps_.print_diff_files;  // 显示差异文件路径列表
    zRun_.ops[11] = zDpOps_.print_diff_contents;  // 显示差异文件内容
    zRun_.ops[12] = zDpOps_.dp;  // 批量布署或撤销
    zRun_.ops[13] = zDpOps_.req_dp;  // 用于新加入某个项目的主机每次启动时主动请求中控机向自己承载的所有项目同目最近一次已布署版本代码
    zRun_.ops[14] = zDpOps_.req_file;  // 请求服务器传输指定的文件
    zRun_.ops[15] = NULL;

    /* 返回的 socket 已经做完 bind 和 listen */
    _i zMajorSd = zNetUtils_.gen_serv_sd(zpNetSrv_->p_ipAddr, zpNetSrv_->p_port, zpNetSrv_->servType);

    /* 会传向新线程，使用静态变量；使用数组防止负载高时造成线程参数混乱 */
    static zSockAcceptParam__ zSockAcceptParam_[64] = {{NULL, 0}};
    for (_ui zCnter = 0;; zCnter++) {
        if (-1 == (zSockAcceptParam_[zCnter % 64].connSd = accept(zMajorSd, NULL, 0))) {
            zPrint_Err(errno, "-1 == accept(...)", NULL);
        } else {
            zThreadPool_.add(zops_route, &(zSockAcceptParam_[zCnter % 64]));
        }
    }
}


/*
 * 网络服务路由函数
 */
static void *
zops_route(void *zpParam) {
    _i zSd = ((zSockAcceptParam__ *) zpParam)->connSd;

    char zDataBuf[zGlobCommonBufSiz] = {'\0'};
    char *zpDataBuf = zDataBuf;

    _i zErrNo = 0,
       zOpsId = -1,
       zDataLen = -1;

    /* 若收到大体量数据，直接一次性扩展为1024倍的缓冲区，以简化逻辑 */
    if (zGlobCommonBufSiz == (zDataLen = recv(zSd, zpDataBuf, zGlobCommonBufSiz, 0))) {
        zMem_C_Alloc(zpDataBuf, char, zGlobCommonBufSiz * 1024);  // 用清零的空间，保障正则匹配不出现混乱
        strcpy(zpDataBuf, zDataBuf);
        zDataLen += recv(zSd, zpDataBuf + zDataLen, zGlobCommonBufSiz * 1024 - zDataLen, 0);
    }

    /* 最短的json字符串：{"a":}，6 字节 */
    if (zBytes(6) > zDataLen) {
        zPrint_Err(errno, NULL, "recvd data too short(< 8bytes)");
        goto zMarkEnd;
    }

    /* 提取 value[OpsId] */
    cJSON *zpJRoot = cJSON_Parse(zpDataBuf);
    cJSON *zpOpsId = cJSON_GetObjectItemCaseSensitive(zpJRoot, "OpsId");
    if (cJSON_IsNumber(zpOpsId)) {
        zOpsId = zpOpsId->valueint;

        /* 检验 value[OpsId] 合法性 */
        if (0 > zOpsId || zServHashSiz <= zOpsId || NULL == zRun_.ops[zOpsId]) { zErrNo = -1; }
        else { zErrNo = zRun_.ops[zOpsId](zpJRoot, zSd); }
    } else {
        zErrNo = -1;
    }
    cJSON_Delete(zpJRoot);

    /* 成功状态在下层函数中回复，错误状态统一返回至上层处理 */
    if (0 > zErrNo) {
        zDataLen = snprintf(zDataBuf, zGlobCommonBufSiz, "[{\"OpsId\":%d,\"data\":\"%s\"}]", zErrNo, zpErrVec[-1 * zErrNo]);
        zNetUtils_.sendto(zSd, zDataBuf, zDataLen, 0, NULL);

        /* 错误信息，打印出一份，防止客户端已断开的场景导致错误信息丢失 */
        zPrint_Err(0, NULL, zpDataBuf);
    }

zMarkEnd:
    close(zSd);
    if (zpDataBuf != &(zDataBuf[0])) { free(zpDataBuf); }
    return NULL;
}
