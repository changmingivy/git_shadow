#ifndef ZSVCLOUD_H
#define ZSVCLOUD_H

#include "zcommon.h"

#include "zrun.h"
#include "zthread_pool.h"
#include "zpostgres.h"
#include "znative_utils.h"
#include "cJSON.h"

/* instanceID(22 char) + '\0' */
#define zHASH_KEY_SIZ (_i)(1 + 23 / sizeof(_ull))
#define zINSTANCE_ID_BUF_LEN (1 + 23 / sizeof(_ull)) * sizeof(_ull)

typedef enum {
    zLISTEN,
    zSYN_SENT,
    zESTABLISHED,
    zSYN_RECV,
    zFIN_WAIT1,
    zCLOSE_WAIT,
    zFIN_WAIT2,
    zLAST_ACK,
    zTIME_WAIT,
    zCLOSING,
    zCLOSED,
} ztcp_state_t;

struct zSvParamSolid__ {
    char *p_region;
    char *p_dimensions;
};

struct zSvParam__ {
    struct zSvParamSolid__ *p_paramSolid;

    char *p_metic;
    _i targetID;

    /* 处理数据的回调函数 */
    void (* cb) (_i *, _f);
};

struct zRegion__ {
    char *p_name;
    _i ecsCnt;
};

struct zSvData__ {
    /* 分别处于 tcp 的 11 种状态的连接计数 */
    _i tcpState[11];

    /* 可直接取到的不需要额外加工的数据项 */
    _i timeStamp;
    _i cpu;
    _i mem;
    _i load[3];

    /*
     * 取磁盘列表的时，可得到以 GB 为单位的容量，
     * 不需要从监控数据中再取一次，
     * 所有磁盘容量加和之后，换算成 M，保持与 diskSpent 的单位统一
     */
    _i diskTotal;

    /*
     * 首先将 byte 换算成 M(/1024/1024)，以 M 为单位累加计数，
     * 集齐所有磁盘的数据后，计算实例的全局磁盘总使用率
     */
    _i diskSpent;

    /*
     * 首先将 byte 换算成 KB(/1024)，以 KB 为单位累加计数，
     * 集齐所有设备的数据后，取得最终的 sum 值
     */
    _i disk_rdkb;
    _i disk_wrkb;

    _i disk_rdiops;
    _i disk_wriops;

    _i net_rdkb;
    _i net_wrkb;

    _i net_rdiops;
    _i net_wriops;
} __attribute__ ((aligned (sizeof(_i))));

//struct zDisk__ {
//    char *p_dev;  // "/dev/vda1"
//    struct zDisk__ *p_next;
//};
//
//struct zNetIf__ {
//    char *p_dev;  // "eth0"
//    struct zNetIf__ *p_next;
//};

union zInstanceId__ {
    _ull hashKey[zHASH_KEY_SIZ];
    char id[zINSTANCE_ID_BUF_LEN];
};

struct zSv__ {
    /*
     * 以链表形式集齐实例所有 device 的名称，
     * 用于查询对应设备的监控数据
     */
    //struct zDisk__ disk;
    //struct zNetIf__ netIf;

    /* HASH KEY */
    union {
        _ull hashKey[zHASH_KEY_SIZ];
        char id[zINSTANCE_ID_BUF_LEN];
    };

    /* 以 900 秒（15 分钟）为周期同步监控数据，单机数据条数：60 */
    struct zSvData__ ecsSv_[60];

    struct zSv__ *p_next;
};

struct zSVCloud__ {
    void (* init) (void);
    void (* data_sync) (void);
    void (* tb_mgmt) (void);
};

#endif  // #ifndef ZSVCLOUD_H
