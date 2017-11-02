#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "zRun.h"


static void zstart_server(void *zpParam);

struct zRun__ zRun_ = { 
	.run = zstart_server,
	.ops = { NULL }
};


/************
 * 网络服务 *
 ************/
/*  执行结果状态码对应表
 *  -1：操作指令不存在（未知／未定义）
 *  -2：项目ID不存在
 *  -3：代码版本ID不存在或与其相关联的内容为空（空提交记录）
 *  -4：差异文件ID不存在
 *  -5：指定的主机 IP 不存在
 *  -6：项目布署／撤销／更新ip数据库的权限被锁定
 *  -7：后端接收到的数据无法解析，要求前端重发
 *  -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 *  -9：服务端错误：接收缓冲区为空或容量不足，无法解析数据
 *  -10：前端请求的数据类型错误
 *  -11：正在布署／撤销过程中（请稍后重试？）
 *  -12：布署失败（超时？未全部返回成功状态）
 *  -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确
 *  -14：系统测算的布署耗时超过 90 秒，通知前端不必阻塞等待，可异步于布署列表中查询布署结果
 *  -15：布署前环境初始化失败（中控机）
 *  -16：系统负载太高(CPU 或 内存占用超过 80%)，不允许布署，提示稍后重试
 *
 *  -19：更新目标机IP列表时，存在重复IP
 *  -23：更新目标机IP列表时：部分或全部目标初始化失败
 *  -24：更新目标机IP列表时，没有在 ExtraData 字段指明IP总数量
 *  -26：目标机IP列表为空
 *  -28：前端指定的IP数量与实际解析出的数量不一致
 *  -29：一台或多台目标机环境初化失败(SSH 连接至目标机建立接收项目文件的元信息——git 仓库)
 *
 *  -32：请求创建的项目ID超出系统允许的最大或最小值（创建或载入项目代码库时出错）
 *  -33：无法创建请求的项目路径
 *  -34：请求创建的新项目信息格式错误（合法字段数量少于 5 个或大于 6 个，第6个字段用于标记是被动拉取代码还是主动推送代码）
 *  -35：请求创建的项目ID已存在（创建或载入项目代码库时出错）
 *  -36：请求创建的项目路径已存在，且项目ID不同
 *  -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *  -38：拉取远程代码库失败（git clone 失败）
 *  -39：项目元数据创建失败，如：无法打开或创建布署日志文件meta等原因
 *
 *  -70：服务器版本号列表缓存存在错误
 *  -71：服务器差异文件列表缓存存在错误
 *  -72：服务器单个文件的差异内容缓存存在错误
 *
 *  -80：目标机请求的文件路径不存在或无权访问
 *
 *  -101：目标机返回的版本号与正在布署的不一致
 *  -102：目标机返回的错误信息
 *  -103：目标机返回的状态信息Type无法识别
 *
 *  -10000: fake success
 */

static void
zstart_server(void *zpParam) {
    zRun_.ops[0] = NULL;  // ztest_func;  // 留作功能测试接口
    zRun_.ops[1] = zDpOps_.creat;  // 添加新代码库
    zRun_.ops[2] = zDpOps_.lock;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zRun_.ops[3] = zDpOps_.lock;  // 恢复布署／撤销功能
    zRun_.ops[4] = NULL;  // 已解决 CentOS-6 平台上 sendmsg 的问题，不再需要 zupdate_ip_db_proxy()
    zRun_.ops[5] = zDpOps_.show_meta_all;  // 显示所有有效项目的元信息
    zRun_.ops[6] = zDpOps_.show_meta;  // 显示单个有效项目的元信息
    zRun_.ops[7] = NULL;
    zRun_.ops[8] = zDpOps_.state_confirm;  // 远程主机初始经状态、布署结果状态、错误信息
    zRun_.ops[9] = zDpOps_.print_revs;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zRun_.ops[10] = zDpOps_.print_diff_files;  // 显示差异文件路径列表
    zRun_.ops[11] = zDpOps_.print_diff_contents;  // 显示差异文件内容
    zRun_.ops[12] = zDpOps_.dp;  // 布署或撤销
    zRun_.ops[13] = zDpOps_.req_dp;  // 用于新加入某个项目的主机每次启动时主动请求中控机向自己承载的所有项目同目最近一次已布署版本代码
    zRun_.ops[14] = zDpOps_.req_file;  // 请求服务器传输指定的文件
    zRun_.ops[15] = NULL;

    /* 如下部分配置网络服务 */
    _i zMajorSd;
    zNetSrv__ *zpNetSrv_ = (zNetSrv__ *)zpParam;
    zMajorSd = zNetUtils_.gen_serv_sd(zpNetSrv_->p_IpAddr, zpNetSrv_->p_port, zpNetSrv_->zServType);  // 返回的 socket 已经做完 bind 和 listen

    /* 会传向新线程，使用静态变量；使用数组防止负载高时造成线程参数混乱 */
    static zSockAcceptParam__ zSockAcceptParam_[64] = {{NULL, 0}};
    for (_ui zCnter = 0;; zCnter++) {
        if (-1 == (zSockAcceptParam_[zCnter % 64].ConnSd = accept(zMajorSd, NULL, 0))) {
            zPrint_Err(errno, "-1 == accept(...)", NULL);
        } else {
            zThreadPool_.add(zDpOps_.route, &(zSockAcceptParam_[zCnter % 64]));
        }
    }
}
