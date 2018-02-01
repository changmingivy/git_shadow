#include "zsv_cloud.h"

extern struct zSvAliyunEcs__ zSvAliyunEcs_;

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
