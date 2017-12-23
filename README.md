# 一、概述    
&emsp;&emsp;**git_shadow** 是一套纯 C 语言实现的、基于 libgit2 的高性能布署系统，专注于最终线上产品的分发环节，支持无限量大规模布署场景。
#### 1. 系统环境需求    
&emsp;&emsp;- [linux](https://www.kernel.org) 内核版本 >= 2.6.37    
&emsp;&emsp;- 或 [FreeBSD](https://www.freebsd.org) 系统版本 >= 10.1    
#### 2. 编译器需求    
&emsp;&emsp;- [gcc](http://gcc.gnu.org) >= 4.4        
&emsp;&emsp;- 或 [clang](http://llvm.org) >= 3.4      
#### 3. 数据库需求    
&emsp;&emsp;- [postgresql](https://www.postgresql.org) = 10.1    
#### 4. 第三方库需求    
&emsp;&emsp;- [libssh2](https://www.libssh2.org) = 1.8.0    
&emsp;&emsp;- [libgit2](https://libgit2.github.com) = 0.26 [cmake options: THREADSAFE=ON]    
&emsp;&emsp;- [cJSON](https://github.com/DaveGamble/cJSON) = 1.6.0    
#### 5. 布署环境需求      
&emsp;&emsp;- 布署中控机可连接所有目标机的 SSH 服务器；        
&emsp;&emsp;- 所有目标机可连接中控机的服务端口，通常是 20000。   
#### 6. 安装、启/停方法    
&emsp;&emsp;- 可使用源码路径下的 serv_tools/zstart.sh 脚本一键下载、编译、启动 git_shadow 及数据库、第三方库；    
&emsp;&emsp;- 用户无须手动配置上述 3、4 步所述需求；    
&emsp;&emsp;- 暂不提供 rpm、deb 等预编译包。       

    **如下所示，将使用默认配置启动 git_shadow，监听在本地 20000 端口**              
    git clone https://github.com/kt10/zgit_shadow.git /tmp/zgit_shadow2        
    sh /tmp/zgit_shadow2/serv_tools/zstart.sh ::1 20000          
#### 7. 命令行参数          
|选项|描述|
| :-: | :- |
|**-x**|git_shadow 的运行路径，通常与 git clone 的目标路径相同|
|**-u**|git_shadow 进程所属的用户名|
|**-h**|git_shadow 的服务地址，支持 IPv6、IPv4、主机名、域名等形式|
|**-p**|git_shadow 服务端口|
|**-H**|[可选] PostgreSQL host name, default 'localhost'|
|**-A**|[可选] PostgreSQL host IP addr, if exist, '-H' will be ignored|
|**-P**|[可选] PostgreSQL host serv port, default '5432'|
|**-U**|[可选] PostgreSQL login name, default 'git'|
|**-F**|[可选] PostgreSQL pass file, default '$HOME/.pgpass'|
|**-D**|[可选] which database to login, default 'dpDB'|    
# 二、服务接口    
## 总揽
&emsp;&emsp;git_shadow 根据用户提供的 opsId 字段，map 到将被调用的接口；标注 “-” 的项表示系统内部私有接口，不对外开放。          

|opsId|描述|
| :-: | :- |
|**0**|服务在线状态确认，接收到任意内容，均会会回复一个 "!"|
|**1**|新建项目|
|**2**|-|
|**3**|更新源库 URL 或 分支名称|
|**4**|-|
|**5**|-|
|**6**|-|
|**7**|-|
|**8**|-|
|**9**|打印版本号列表|
|**10**|打印两个版本之间的差异文件列表|
|**11**|打印两个版本之间的同一个文件的内容差异|
|**12**|核心布署接口，并发执行，执行结果影响系统状态|
|**13**|辅助布署接口，串行执行，执行结果不影响系统状态|
|**14**|-|
|**15**|查询项目元信息与最近一次布署的实时状态|
## A、项目管理类接口    
#### [opsId 1] 新建项目    
>1. **功能描述**       
        
    - 创建新项目。    

>2. **json 请求示例**    
```
{
    "opsId": 1,
    "projId": 9,
    "pathOnHost": "/home/git/zgit_shadow2",
    "sshUserName": "john",
    "sshPort": "22",
    "needPull": "Yes",
    "sourceUrl": "https://github.com/kt10/zgit_shadow.git",
    "sourceBranch": "master",
    "sourceVcsType": "git"
}
```
|字段|描述|
| :- | :- |
|**projId**|将要创建的项目 ID|
|**pathOnHost**|新项目文件在目标机上的存放路径|
|**sshUserName**|中控机使用哪个用户身份登陆该项目的所有目标机|
|**sshPort**|本项目所有目标机的 SSH 服务端口|
|**needPull**|[可选] 是否需要中控机主动同步源库的文件更新，可使用 "Y" "yes" "N" "no" 等，只要保证首字母为 Y 或 N 即可，不区分大小写|
|**sourceUrl**|[条件可选] 若 needPull 为 "Y"，则必须提供源库的地址|
|**sourceBranch**|[条件可选] 若 needPull 为 "Y"，则必须提供与源库对接的分支名称|
|**sourceVcsType**|[条件可选] 若 needPull 为 "Y"，则必须指定源库的 VCS 类型，可使用 "G" "git" 等，以首字母判断，不区分大小写|
>3. **json 返回示例**    
```
{
    "errNo": 0,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|0 表示成功；否则表示出错|
|**content**|errNo 非 0 时，存放错误信息|
>4. **注意事项**       
    - 返回成功仅表示中控机端所有动作执行成功，各项信息确认无误并写入数据库。       
#### [opsId 4] 删除项目    
&emsp;&emsp;暂无
## B、查询类接口    
#### [opsId 9] 打印版本号列表

#### [opsId 10] 打印两个版本之间的差异文件列表

#### [opsId 11] 打印两个版本之间的同一个文件的差异内容

## C、布署类接口
#### [opsId 3] 源库 URL 或分支名称更新    

#### [opsId 12] 常规布署    

#### [opsId 13] 特殊布署    
>1. **功能描述**       
  
    - 布署到单台或少量的临时目标机;    
    - 应对特殊场景的灵活布署需求，通常用于临时测试场景；
    - 或目标机重启后，主动请求同步。     

>2. **json 请求示例**    
```
{
    "opsId": 13,
    "projId": 9,
    "ipCnt": 2,
    "ipList": "::1 , ::2 @ ::3 _ ::4",
    "delim": ", @_",
    "revSig": "abcdefg12345678abcdefg12345678qwertyui"
}
```
|字段|描述|
| :- | :- |
|**projId**|创建项目时指定的 ID|
|**ipCnt**|目标机数量|
|**ipList**|目标机IP列表，以 delim 字段指定的分割符分割，若不指定 delim，则默认使用空格|
|**delim**|[可选] 字段可同时指定多个分割符，如上述示例可被正确解析|
|**revSig**|[可选] 提示目标机本地的最新版本号，若与线上版本一致，则无需布署，直接返回成功|
>3. **json 返回示例**    
```
{
    "errNo": 0,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|0 表示成功；否则表示出错|
|**content**|errNo 非 0 时，存放错误信息|
>4. **注意事项**       
    - 无法返回失败的目标机名单及对应的具体原因；         
    - 接口的调用会并发执行，但单次调用内部，无论指定了多少目标机，均是串行执行，无并发；              
    - 执行结果不包含在实时进度查询接口返回的信息中。
## D、系统内部接口    
**!!! ==== 仅供了解，用户无需调用 ==== !!!**      
>- ping-pang：测试服务器在线状态，向中控机发送 "?”，若正常在线会回复 “!”；  
>- sys-update：系统版本升级后，调用此接口将所有目标机置为未初始化状态，下一次布署时将执行全量初始化，从而所有目标机上的必要组件都会得到更新；
>- history-import：系统升级后，导入旧版历史数据；
>- glob_res_confirm：已确认自身布署成功的目标机，通过此接口请求中控机的全局布署结果，若全局布署结果是失败，则该目标机将回滚至原有状态；
>- state_confirm：目标机通过此接口上报布署结果及错误信息；
>- req_file：目标机通过此接口，请求中控机发送指定的文件，如 post-update 缺失时。
# 三、常见问题
>1. **无限量大规模布署实现原理？**       

    - 布署中控机，实质是由多个服务器组成的集群，由一台主服务器及多个从服务器（也称为代理机：PROXY）构成，从服务器以 NFS 方式共享主服务器的所有布署数据；        
    - 故而，无论需要布署的目标机数量是多少，只要配套足够大的布署中控集群，即可实现高效布署；     
    - 主服务器负责与前端及目标机的交互工作，从服务负责实际的数据推送工作；当不存在从服务器时，主服务器直接进行数据的推送；    
    - 当布署请求到来时，主服务器会将布署任务分派至每个从服务器执行推送动作，自身则等待接收所有目标机的状态返回，并处理其间发生的所有错误。        

>2. **目标机 100% 原子性、一致性保证？**    

    - 保证最终一致性，但会存在时间差。    

>3. **用户定制的布署后动作结果，如何保证？**    

    - 非核心功能，目前不保证执行结果。    
# 四、代码结构简介    
>- bin/：存放最终编译出的二进制文件，即 git_shadow 服务程序；
>- **src/**：存放主程序核心代码；
>- **inc/**：与 src/ 中的源文件一一对应的头文件；
>- **lib/**：存放第三方库的源码及编译结果；
>- **tools/**：需要传送到布署目标机上运行的 shell 脚本，如：post-update 勾子等；
>- serv_tools/：用于方便服务端管理的 shell 脚本，非必须；
>- screen_shoot_sample/：运行示例截图，非必须；
>- qemu_kvm/：一组 qemu/kvm 虚拟机 shell 脚本，用于快速启动测试环境，非必须；
>- log/：shell 重定向产生的日志信息，非必须。
# 五、宏观架构图    
>![](http://upload-images.jianshu.io/upload_images/5142096-5210e75b9bd13380.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)




