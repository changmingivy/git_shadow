> 篇幅较长，建议到 wiki 分页查看：https://github.com/kt10/zgit_shadow/wiki                             
# 一、概述    
&emsp;&emsp;**git_shadow** 是一套纯 C 语言实现的、基于 libgit2 的高性能布署系统，专注于最终线上产品的分发环节，支持无限量大规模布署场景。
#### 1. 系统环境需求    
&emsp;&emsp;- [linux](https://www.kernel.org) 内核版本 >= 2.6.37    
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
&emsp;&emsp;- 服务端可连接所有目标机的 SSH 服务器；        
&emsp;&emsp;- 所有目标机可连接服务端的 TCP 端口，通常是 20000。   
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
#### 8. 进程示例
>![](http://images.cnblogs.com/cnblogs_com/hadex/909098/o_process-ps.png)
# 二、服务接口   　　
## 总揽
&emsp;&emsp;git_shadow 根据用户提供的 opsId 字段，map 到将被调用的接口；标注 “-” 的项表示系统内部私有接口，不对外开放。          

|opsId|描述|
| :-: | :- |
|**0**|服务在线状态确认，接收到任意内容，均会会回复一个 "!"|
|**1**|新建项目|
|**2**|-|
|**3**|更新源库 URL 或 分支名称|
|**4**|删除项目|
|**5**|-|
|**6**|-|
|**7**|-|
|**8**|-|
|**9**|打印版本号列表|
|**10**|打印两个版本之间的差异文件列表|
|**11**|打印两个版本之间的同一个文件的内容差异|
|**12**|核心布署接口，并发执行，执行结果影响系统状态|
|**13**|-|
|**14**|-|
|**15**|查询项目元信息与最近一次布署的实时状态|
## A、项目管理类接口    
### [opsId 1] 新建项目    
>1. **功能描述**       
        
    - 创建新项目。    

>2. **json 请求示例**    
```
{
    "opsId": 1,
    "repoId": 9,
    "pathOnHost": "/home/git/zgit_shadow2",
    "sshUserName": "john",
    "sshPort": "22",
    "needPull": "Yes",
    "sourceURL": "https://github.com/kt10/zgit_shadow.git",
    "sourceBranch": "master",
    "sourceVcsType": "git"
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 将要创建的项目 ID|
|**pathOnHost**|[string] 新项目文件在目标机上的存放路径|
|**sshUserName**|[string] 服务端使用哪个用户身份登陆该项目的所有目标机|
|**sshPort**|[string] 本项目所有目标机的 SSH 服务端口|
|**needPull**|[string 可选] 是否需要服务端主动同步源库的文件更新，可使用 "Y" "yes" "N" "no" 等，只要保证首字母为 Y 或 N 即可，不区分大小写|
|**sourceURL**|[string 条件可选] 若 needPull 为 "Y"，则必须提供源库的地址|
|**sourceBranch**|[string 条件可选] 若 needPull 为 "Y"，则必须提供与源库对接的分支名称|
|**sourceVcsType**|[string 条件可选] 若 needPull 为 "Y"，则必须指定源库的 VCS 类型，可使用 "G" "git" 等，以首字母判断，不区分大小写|
>3. **json 返回示例**    
```
**成功返回**
{
    "errNo": 0
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|0 表示成功；否则表示出错|
|**content**|存放错误信息|
>4. **注意事项**       
    - 返回成功仅表示服务端所有动作执行成功，各项信息确认无误并写入数据库。       
### [opsId 4] 删除项目    
&emsp;&emsp;暂无
## B、查询类接口    
### [opsId 9] 打印版本号列表
>1. **功能描述**       
        
    - 打印最近一次布署之后新产生的版本号列表；
    - 打印已布署的版本号列表。    

>2. **json 请求示例**    
```
{
    "opsId": 9,
    "repoId": 35,
    "dataType": 0
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 项目 ID|
|**dataType**|[int] 请求查询的数据类型，0 表示新产生的版本号列表，1 表示已布署版本号列表|
>3. **json 返回示例**    
```
**成功返回**
{
  “errNo”: 0,
  "cacheId": 1555555555,
  ”data”: [
    {
      “revId”: 0,
      “revSig”: "3d93f7220b58a395ba2c56dab024b6d252c5a10e",
      “revTimeStamp”: 1555555555
    },
    {
      “revId”: 1,
      “revSig”: "3d93f7220b58a395ba2c56dab024b6d252c5a10a",
      “revTimeStamp”: 1555555550
    }
  ]
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|[int] 0 表示成功，否则表示出错|
|**data**|[obj array] 存放数据正文|
|**data/revId**|[int] 版本号 ID|
|**data/revSig**|[string] 版本号|
|**data/revTimeStamp**|[int] 版本号 UNIX 时间戳|
|**content**|[string] 存放错误信息|
>4. **注意事项**           
    - 仅会显示最新的 64 条记录，无论 dataType 是 0 还是 1。
### [opsId 10] 打印两个版本之间的差异文件列表
>1. **功能描述**       
        
    - 打印全部有变动的文件的路径（基于项目顶层的相对路径）。

>2. **json 请求示例**    
```
{
    "opsId": 10,
    "repoId": 35,
    "dataType": 0,
    "revId": 3,
    "cacheId": 1555555555
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 项目 ID|
|**dataType**|[int] 请求查询的数据类型，0 表示新产生的版本号列表，1 表示已布署版本号列表|
|**revId**|[int] 查询版本号列表时，服务端返回的 revId 字段值|
|**cacheId**|[int] 查询版本号列表时，服务端返回的 cacheId 字段值|

>3. **json 返回示例**    
```
**成功返回**
{
  “errNo”: 0,
  ”data”: [
    {
      “fileId”: 0,
      “filePath”: "doc/README"
    },
    {
      “fileId”: 1,
      “filePath”: "index.php"
    }
  ]
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|[int] 0 表示成功，否则表示出错|
|**data**|[obj array] 存放数据正文|
|**data/fileId**|[int] 差异文件 ID|
|**data/filePath**|[string] 差异文件相对于项目顶层目录的路径|
|**content**|[string] 存放错误信息|
>4. **注意事项**           
    - 差异文件数量 <=24 时，     
        - 以类似 tree 命令视图的形式显示路径的层级关系，         
        - 此时 fileId 为 -1 的条目，存放各级目录的名称，       
        - fileId >= 0 的条目，存放最终的差异文件名称；             
    - 差异文件数量 > 24 时，       
        - 以类似 git diff --name-only 命令视图的形式显示原始的路径字符串。       
### [opsId 11] 打印两个版本之间的同一个文件的差异内容
>1. **功能描述**       
        
    - 打印单一文件的变动内容。

>2. **json 请求示例**    
```
{
    "opsId": 11,
    "repoId": 35,
    "dataType": 0,
    "revId": 3,
    "fileId": 8,
    "cacheId": 1555555555
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 项目 ID|
|**dataType**|[int] 请求查询的数据类型，0 表示新产生的版本号列表，1 表示已布署版本号列表|
|**revId**|[int] 查询版本号列表时，服务端返回的 revId 字段值|
|**fileId**|[int] 查询差异文件列表时，服务端返回的 fileId 字段值|
|**cacheId**|[int] 查询版本号列表时，服务端返回的 cacheId 字段值|

>3. **json 返回示例**    
```
**成功返回**
{
  “errNo”: 0,
  ”data”: [
    {
      “content”: "+abc\n-efg"
    }
  ]
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|[int] 0 表示成功，否则表示出错|
|**data**|[obj array] 存放数据正文|
|**data/content**|[int] 同一文件在不同版本之间的差异内容|
|**content**|[string] 存放错误信息|
### [opsId 15] 实时进度查询（核心接口）    
>1. **功能描述**       
        
    - 查询最近一次布署的实时进度，包含项目元信息。

>2. **json 请求示例**    
```
{
    "opsId": 15,
    "repoId": 35
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 项目 ID|

>3. **json 返回示例**    
```
**成功返回**
{
  "errNo": 0,
  "repoMeta": {
    "id": 35,
    "path": "/home/git/abc",
    "aliasPath": "/home/git/www",
    "createdTime": "2011-11-11 10:00:00"
  },
  "recentDpInfo": {
    "revSig": "123456789009876543211234567890qhwgtsgydt",
    "result": "fail",
    "timeStamp": 1555555555,
    "timeSpent": 6,
    "process": {
      "total": 100,
      "success": 90,
      "fail": {
        "cnt": 2,
        "detail": {
          "servErr": [],
          "netServToHost": ["::1|conn fail","::2|conn fail"],
          "sshAuth": [],
          "hostDisk": [],
          "hostPermission": [],
          "hostFileConflict": [],
          "hostPathNotExist": [],
          "hostDupDeploy": [],
          "hostAddrInvalid": [],
          "netHostToServ": [],
          "hostLoad": [],
          "reqFileNotExist": []
        }
      },
      "inProcess": {
        "cnt": 8,
        "stage": {
          "hostInit": [],
          "servDpOps": [],
          "hostRecvWaiting": ["1.2.3.4"],
          "hostConfirmWaiting": ["fe80::9"]
        }
      }
    }
  },
  "dpDataAnalysis": {
    "successRate": 99.87,
    "avgTimeSpent": 12,
    "errClassification": {
      "total": 5,
      "servErr": 0,
      "netServToHost": 2,
      "sshAuth": 0,
      "hostDisk": 1,
      "hostPermission": 0,
      "hostFileConflict": 1,
      "hostPathNotExist": 0,
      "hostDupDeploy": 0,
      "hostAddrInvalid": 0,
      "netHostToServ": 1,
      "hostLoad": 0,
      "reqFileNotExist": 0
    }
  },
  "hostDataAnalysis": {
    "cpu": {
      "avgLoad": 0.00,
      "loadBalance": 0.00
    },
    "mem": {
      "avgLoad": 0.00,
      "loadBalance": 0.00
    },
    "io/Net": {
      "avgLoad": 0.00,
      "loadBalance": 0.00
    },
    "io/Disk": {
      "avgLoad": 0.00,
      "loadBalance": 0.00
    },
    "diskUsage": {
      "current": 0.00,
      "avg": 0.00,
      "max": 0.00
    }
  }
}
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述| 
| :- | :- |
|**errNo**|[int] 0 表示成功，否则表示出错| 
|||
|**repoMeta**|[obj] 项目元信息数据块| 
|**repoMeta / id**|[int] 项目 ID| 
|**repoMeta / path**|[string] 目标机上的项目路径| 
|**repoMeta / aliasPath**|[string] 目标机上的项目路径别名，即软链接| 
|**repoMeta / createdTime**|[string] 项目创建时间| 
|||
|**recentDpInfo**|[obj] 最近一次布署信息数据块| 
|**recentDpInfo / revSig**|[string] 最近一次布署的版本号| 
|**recentDpInfo / result**|[string] 最近一次布署的实时结果| 
|**recentDpInfo / timeStamp**|[int] 最近一次布署的开始时间，UNIX 时间戳格式| 
|**recentDpInfo / timeSpent**|[int] 最近一次布署的实时耗时，单位：秒| 
|||
|**recentDpInfo / process**|[obj] 最近一次布署信息的详情数据块| 
|**recentDpInfo / process / total**|[int] 目标机总数| 
|**recentDpInfo / process / success**|[int] 截止查询当时，已布署成功的目标机数量| 
|||
|**recentDpInfo / process / fail**|[obj] 截止查询当时，已确定布署失败的目标机信息数据块| 
|**recentDpInfo / process / fail / cnt**|[int] 截止查询当时，已确定布署失败的目标机数量| 
|||
|**recentDpInfo / process / fail / detail**|[obj] 截止查询当时，已确定布署失败的目标机的错误详情分类汇总数据块| 
|**recentDpInfo / process / fail / detail / servErr**|[string array] 因服务端错误而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / netServToHost**|[string array] 因服务端到目标机方向网络错误而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / sshAuth**|[string array] 因 SSH 认证错误而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostDisk**|[string array] 因目标机磁盘满而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostPermission**|[string array] 因目标机上的权限问题而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostFileConflict**|[string array] 因目标机上文件冲突而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostPathNotExist**|[string array] 因目标机上必须的路径异常缺失而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostDupDeploy**|[string array] 因向同一目标机的多个IP布署，造成重复布署而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostAddrInvalid**|[string array] 因目标机 IP 地址格式错误而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / netHostToServ**|[string array] 因目标机到服务端方向的网络错误而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / hostLoad**|[string array] 因目标机负载过高而失败的目标机列表及详情| 
|**recentDpInfo / process / fail / detail / reqFileNotExist**|[string array] 因目标机向服务端请求的文件不存在而失败的目标机列表及详情| 
|||
|**recentDpInfo / process / inProcess**|[obj] 截止查询当时，尚未确定最终结果的目标机信息数据块| 
|**recentDpInfo / process / inProcess / cnt**|[int] 截止查询当时，尚未确定最终结果的目标机数据| 
|**recentDpInfo / process / inProcess / stage**|[obj] 截止查询当时，尚未确定最终结果的每个目标机所处的阶段信息数据块| 
|**recentDpInfo / process / inProcess / stage / hostInit**|[string array] 截止查询当时，正处于目标机初始化环节的目标机列表| 
|**recentDpInfo / process / inProcess / stage / servDpOps**|[string array] 截止查询当时，正处于服务端操作环节的目标机列表| 
|**recentDpInfo / process / inProcess / stage / hostRecvWaiting**|[string array] 截止查询当时，正处于确认是否收到布署内容环节的目标机列表| 
|**recentDpInfo / process / inProcess / stage / hostConfirmWaiting**|[string array] 截止查询当时，正处于确认是否确定所收到的内容完整无误环节的目标机列表| 
|||
|**dpDataAnalysis**|[obj] 最近 30 天布署历史分析信息数据块| 
|**dpDataAnalysis / successRate**|[int] 最近 30 天的布署成功率，即 布署成功的次数 / 布署请求次数| 
|**dpDataAnalysis / avgTimeSpent**|[int] 最近 30 天的布署平均耗时，即：所有布署成功的请求耗时之和 / 所有布署成功的目标机台次之和| 
|**dpDataAnalysis / errClassification**|[obj] 最近 30 天发生的错误分类统计| 
|...|...|
|...|...|
## C、布署类接口
### [opsId 3] 源库 URL 或分支名称更新    
>1. **功能描述**       
  
    - 更新源库的地址或分支名称，多用于测试场景。     

>2. **json 请求示例**    
```
{
    "opsId": 13,
    "repoId": 35,
    "sourceURL": "https://github.com/kt10/zgit_shadow.git",
    "sourceBranch": "dev-tree"
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 创建项目时指定的 ID|
|**sourceURL**|[string 可选] 新的源库地址|
|**sourceBranch**|[string 可选] 新的源库分支名称，即服务端要和哪个分支保持同步|
>3. **json 返回示例**    
```
**成功返回**
{
    "errNo": 0
}
```
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|0 表示成功；否则表示出错|
|**content**|存放错误信息|
>4. **注意事项**       
    - 更新成功之后，在下一次更新之前，会一直生效；       
    - 线上正式环境，不建议频繁更新，可能会产生无法自动处理的文件冲突 。
### [opsId 12] 常规布署（核心接口）    
>1. **功能描述**       
  
    - 批量布署。     

>2. **json 请求示例**    
```
**使用 revId 布署**
{
    "opsId": 12,
    "repoId": 9,
    "cacheId": 1555555555,
    "revId": 5,
    "dataType": 1,
    "forceDp": "Yes",
    "aliasPath": "/home/git/www",
    "ipCnt": 4,
    "ipList": "::1 , ::2 @ ::3 _ ::4",
    "delim": ", @_",
    "sshUserName": "zhangsan",
    "sshPort": "2222",
    "postDpCmd": "rm -rf /tmp/xxx/*"
}

**使用 revSig 布署**
{
    "opsId": 12,
    "repoId": 9,
    "revSig": "abcdefg12345678abcdefg12345678qwertyui"
    "dataType": 1,
    "forceDp": "N",
    "aliasPath": "/home/git/www",
    "ipCnt": 4,
    "ipList": "::1 , ::2 @ ::3 _ ::4",
    "delim": ", @_",
    "sshUserName": "zhangsan",
    "sshPort": "2222",
    "postDpCmd": "rm -rf /tmp/xxx/*"
}
```
|字段|描述|
| :- | :- |
|**repoId**|[int] 创建项目时指定的 ID|
|**cacheId**|[int 条件可选] 查询版本号列表时取得的 cacheId，未指定 revSig 时此项不能为空|
|**revId**|[int 条件可选] 查询版本号列表时取得的 revId，未指定 revSig 时此项不能为空|
|**revSig**|[string 条件可选] 版本号字符串，若指定了此项，则 cacheId 与 revId 两项将被忽略|
|**forceDp**|[string 可选] 强制布署标志，若指定为 "Y"，则目标机端的冲突文件将会被直接清除，默认为 "N"|
|**aliasPath**|[string 可选] 新建链接到项目路径|
|**ipCnt**|[int] 目标机数量|
|**ipList**|[string] 目标机IP列表，以 delim 字段指定的分割符分割，若不指定 delim，则默认使用空格|
|**delim**|[string 可选] 字段可同时指定多个分割符，如上述示例可被正确解析|
|**sshUserName**|[string 可选] 更新连接目标机所用的用户名称|
|**sshPort**|[string 可选] 更新本项目所有目标机的 SSH 服务端口|
|**postDpCmd**|[string 可选] 布署成功后需要执行的命令|
>3. **json 返回示例**    
```
**出错返回**
{
    "errNo": -1,
    "content": "..."
}
```
|字段|描述|
| :- | :- |
|**errNo**|0 表示成功；否则表示出错|
|**content**|存放错误信息|
>4. **注意事项**       
    - 只有服务端发生错误时，才会同步返回结果；          
    - 除此之外，无论成败，都不会同步返回结果；         
    - 用户可通过实时进度查询接口获取布署结果；       
    - 或等待前端系统的邮件、微信通知；
    - 布署成功，并不代表通过 postDpCmd 字段指定的布署后命令也一定成功。            
## D、系统内部接口    
**!!! ==== 仅供了解，用户无需调用 ==== !!!**      
>- ping-pang：测试服务器在线状态，向服务端发送 "?”，若正常在线会回复 “!”；  
>- sys-update：系统版本升级后，调用此接口将所有目标机置为未初始化状态，下一次布署时将执行全量初始化，从而所有目标机上的必要组件都会得到更新；
>- history-import：系统升级后，导入旧版历史数据；
>- glob_res_confirm：已确认自身布署成功的目标机，通过此接口请求服务端的全局布署结果，若全局布署结果是失败，则该目标机将回滚至原有状态；
>- state_confirm：目标机通过此接口上报布署结果及错误信息；
>- req_file：目标机通过此接口，请求服务端发送指定的文件，如 post-update 缺失时。
# 三、常见问题
>1. **无限量大规模布署实现原理？**       

    - 布署服务端，实质是由多个服务器组成的集群，由一台主服务器及多个从服务器（也称为代理机：PROXY）构成，从服务器以 NFS 方式共享主服务器的所有布署数据；        
    - 故而，无论需要布署的目标机数量是多少，只要配套足够大的布署服务端集群，即可实现高效布署；     
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
# 五、宏观架构图 [已过时，需要更新]    
>![](http://upload-images.jianshu.io/upload_images/5142096-5210e75b9bd13380.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)

