#include "zsv_cloud.h"

#include <sys/types.h>
#include <unistd.h>
#include <string.h>

extern zRepo__ *zpRepo_;

extern struct zSvAliyunEcs__ zSvAliyunEcs_;
extern struct zPgSQL__ zPgSQL_;
extern struct zRun__ zRun_;

/* 确保同一时间只有一个同步事务在运行 */
pthread_mutex_t zAtomSyncLock = PTHREAD_MUTEX_INITIALIZER;

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
                "(last_timestamp    int NOT NULL);",
                NULL)) {

        zPRINT_ERR_EASY("");
        exit(1);
    }

    zSvAliyunEcs_.init();
    // other ...
}

void
zdata_sync (void) {
    pthread_mutex_lock(&zAtomSyncLock);

    zSvAliyunEcs_.data_sync();
    // other ...

    pthread_mutex_unlock(&zAtomSyncLock);
}

void
ztb_mgmt (void) {
    zSvAliyunEcs_.tb_mgmt();
    // other ...
}
