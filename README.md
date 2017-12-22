# 一、概述    
&emsp;&emsp;**git_shadow** 是一套纯 C 语言实现的、基于 git 的高性能布署系统，专注于最终线上产品的分发环节，支持无限量大规模布署场景。
1. 系统环境需求    
[linux](https://www.kernel.org) 内核版本 >= 2.6.37
[FreeBSD](https://www.freebsd.org) 系统版本 >= 10.1
2. 编译器需求    
[gcc](http://gcc.gnu.org) >= 4.4／[clang](http://llvm.org) >= 3.4
3. 数据库需求    
[postgresql](https://www.postgresql.org) = 10.1
4. 第三方库需求    
[libssh2](https://www.libssh2.org) = 1.8.0
[libgit2](https://libgit2.github.com) = 0.26 [cmake options: THREADSAFE=ON]
[cJSON](https://github.com/DaveGamble/cJSON) = 1.6.0
5. 安装、启/停方法    
&emsp;&emsp;使用源码路径下的 serv_tools/zstart.sh 脚本一键编译与启动，不提供 rpm、deb 等预编译包。
6. 命令行参数、配置文件    
&emsp;&emsp;所有信息通过命令行参数传递，其中前 4 项必须提供，后 6 项若留空，将使用默认值：    
    - [-x] serv root path on dp_master, usually /home/git/zgit_shadow
    - [-u]  username on do_master
    - [-h] serv address(IPv6/v4)
    - [-p] serv port(TCP)
    - [-H]  PostgreSQL host name, default 'localhost'
    - [-A]  PostgreSQL host IP addr, if exist, '-H' will be ignored
    - [-P]  PostgreSQL host serv port, default '5432'
    - [-U]  PostgreSQL login name, default 'git'
    - [-F]  PostgreSQL pass file, default '$HOME/.pgpass'
    - [-D]  which database to login, default 'dpDB'
# 二、服务接口    
#### 查询类接口    
1. 打印版本号列表
2. 打印两个版本之间的差异文件列表
3. 打印两个版本之间的同一个文件的差异内容
#### 布署类接口
1. 
2. 
3. 
#### 系统内部接口
1. 
2. 
3. 
# 三、常见问题
1. 无限量大规模布署实现原理与环境要求？
    - 布署中控机，实质是由多个服务器组成的集群，由一台主服务器及多个从服务器构成，从服务器以 NFS 方式共享主服务器的所有布署数据；
    - 故而，无论需要布署的目标机数量是多少，只要配套足够大的布署中控集群，即可实现高效布署；
    - 主服务器负责与前端及目标机的交互工作，从服务负责实际的数据推送工作；当不存在从服务器时，主服务器直接进行数据的推送；
    - 当布署请求到来时，主服务器会将布署任务分派至每个从服务器执行推送动作，自身则等待接收所有目标机的状态返回，并处理其间发生的所有错误；
    - 环境要求：NFS 共享环境，需要事先搭建好
2. 目标机 100% 原子性、一致性保证？    
&emsp;&emsp;保证最终一致性，但会存在时间差。
3. 用户定制的布署后动作结果，如何保证？    
&emsp;&emsp;非核心功能，目前不保证执行结果。
# 四、代码结构简介    
```
.
├── bin/
├── src/
├── inc/
├── lib/
├── tools/
├── serv_tools/
├── screen_shoot_sample/
├── qemu_kvm/
└── log/
```
- bin/
&emsp;&emsp;存放最终编译出的二进制文件，即 git_shadow 服务程序。
- **src/**
&emsp;&emsp;存放主程序核心代码。
- **inc/**
&emsp;&emsp;与 src/ 中的源文件一一对应的头文件。
- **lib/**
&emsp;&emsp;存放第三方库的源码及编译结果。
- **tools/**
&emsp;&emsp;需要传送到布署目标机上运行的 shell 脚本，如：post-update 勾子等。
- serv_tools/
&emsp;&emsp;用于方便服务端管理的 shell 脚本，非必须。
- screen_shoot_sample/
&emsp;&emsp;运行示例截图，非必须。
- qemu_kvm/
&emsp;&emsp;一组 qemu/kvm 虚拟机 shell 脚本，用于快速启动测试环境，非必须。
- log/
&emsp;&emsp;shell 重定向产生的日志信息，非必须。
# 五、宏观架构图    
![](http://upload-images.jianshu.io/upload_images/5142096-5210e75b9bd13380.png?imageMogr2/auto-orient/strip%7CimageView2/2/w/1240)