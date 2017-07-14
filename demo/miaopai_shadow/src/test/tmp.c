

/*
 * 旧版路由函数，已废弃
 */
void
zdo_serv(void *zpSd) {
    _i zSd = *((_i *)zpSd);

    char zReqBuf[zBytes(4)];
    if (zBytes(4) > zrecv_nohang(zSd, zReqBuf, zBytes(4), MSG_PEEK, NULL)) { // 接收前端指令信息，读出指令但不真正取走数据
        zPrint_Err(0, NULL, "recv ERROR!");
        zsendto(zSd, "!", zBytes(2), 0, NULL);  //  若数据异常，要求前端重发报文
        return;
    }

    switch (zReqBuf[0]) {
        case 'p':  // list:列出内容有变动的所有文件路径
            zlist_diff_files(zSd);
            break;
        case 'P':  // print:打印某个文件的详细变动内容
            zprint_diff_contents(zSd);
            break;
        case 'l':  // LIST:打印最近zPreLoadLogSiz次布署日志
            zlist_log(zSd, 0);
            break;
        case 'L':  // LIST:打印所有历史布署日志
            zlist_log(zSd, 1);
            break;
        case 'd':  // deploy:布署单个文件
            zdeploy(zSd, 0);
            break;
        case 'D':  // DEPLOY:布署当前提交所有文件
            zdeploy(zSd, 1);
            break;
        case 'r':  // revoke:撤销单个文件的更改
            zrevoke(zSd, 0);
            break;
        case 'R':  // REVOKE:撤销某次提交的全部更改
            zrevoke(zSd, 1);
            break;
        case 'c':  // confirm:客户端回复的布署成功确认信息
            zconfirm_deploy_state(zSd, 0);
            break;
        case 'C':  // CONFIRM:前端回复的人工确认信息
            zconfirm_deploy_state(zSd, 1);
            break;
        case 'u':  // update major clients ipv4 addr txt file: 与中控机直接通信的master客户端列表数据需要更新
            zupdate_ipv4_db_txt(zSd, zReqBuf[1], 0);  // zReqBuf[1] 存储代码库索引号／代号
            break;
        case 'U':  // Update all clients ipv4 addr txt file: 所有客户端ipv4地址列表数据需要更新
            zupdate_ipv4_db_txt(zSd, zReqBuf[1], 1);  // zReqBuf[1] 存储代码库索引号／代号
            break;
        default:
            zPrint_Err(0, NULL, "Undefined request");
    }
}
