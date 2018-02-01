#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include "zsv_cloud.h"

extern zRepo__ *zpRepo_;

extern struct zSvAliyunEcs__ zSvAliyunEcs_;
extern struct zPgSQL__ zPgSQL_;
extern struct zRun__ zRun_;

void zinit(void);
void zdata_sync (void);
void ztb_mgmt (void);

struct zSvCloud__ zSvCloud_ = {
    .init = zinit,
    .data_sync = zdata_sync,
    .tb_mgmt = ztb_mgmt,
};

void
zinit(void) {
    if (0 != zPgSQL_.exec_once(
                zRun_.p_sysInfo_->pgConnInfo,
                "CREATE TABLE IF NOT EXISTS sv_sync_meta "
                "(last_timestamp    bigint NOT NULL);",
                NULL)) {

        zPRINT_ERR_EASY("");
        exit(1);
    }

    zSvAliyunEcs_.init();
    // other ...
}

void
zdata_sync (void) {
    zSvAliyunEcs_.data_sync();
    // other ...
}

void
ztb_mgmt (void) {
    zSvAliyunEcs_.tb_mgmt();
    // other ...
}
