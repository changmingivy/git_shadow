#ifndef ZSV_CLOUD_H
#define ZSV_CLOUD_H

#include "zcommon.h"
#include "zaliyun_ecs.h"

struct zSvCloud__ {
    void (* init) (void);
    void (* data_sync) (void);
    void (* tb_mgmt) (void);
};

#endif  // #ifndef ZALIYUN_ECS_H
