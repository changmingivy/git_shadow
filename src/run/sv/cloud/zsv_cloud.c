#include "zsv_cloud.h"

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

}

void
zdata_sync (void) {

}

void
ztb_mgmt (void) {

}
