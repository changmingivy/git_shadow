#ifndef ZSVCLOUD_H
#define ZSVCLOUD_H

#include "zcommon.h"

#include "zrun.h"
#include "zpostgres.h"

struct zSVCloud__ {
    void (* init) (void);
    void (* data_sync) (void);
};

#endif  // #ifndef ZSVCLOUD_H
