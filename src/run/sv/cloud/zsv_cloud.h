#ifndef ZSVCLOUD_H
#define ZSVCLOUD_H

#include "zcommon.h"

#include "zrun.h"
#include "zthread_pool.h"
#include "zpostgres.h"
#include "znative_utils.h"
#include "cJSON.h"

struct zSVCloud__ {
    void (* init) (void);
    void (* data_sync) (void);
    void (* tb_mgmt) (void);
};

#endif  // #ifndef ZSVCLOUD_H
