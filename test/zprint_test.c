#ifndef _Z
#include "../zmain.c"
#endif

	for (_i i = 0; i < zpGlobRepoIf[0].SortedCommitVecWrapIf.VecSiz; i++) {
		fprintf(stderr, "vec len: %zd\n", zpGlobRepoIf[0].SortedCommitVecWrapIf.p_VecIf[i].iov_len);
		fprintf(stderr, "vec data len: %zd\n", ((struct zSendInfo *)(zpGlobRepoIf[0].SortedCommitVecWrapIf.p_VecIf[i].iov_base))->DataLen);
		fprintf(stderr, "vec self id: %zd\n", ((struct zSendInfo *)(zpGlobRepoIf[0].SortedCommitVecWrapIf.p_VecIf[i].iov_base))->SelfId);
		fprintf(stderr, "vec data: %zd %zd\n", ((struct zSendInfo *)(zpGlobRepoIf[0].SortedCommitVecWrapIf.p_VecIf[i].iov_base))->data[0], ((struct zSendInfo *)(zpGlobRepoIf[0].SortedCommitVecWrapIf.p_VecIf[i].iov_base))->data[1]);

		fprintf(stderr, "vec len: %s\n", zpGlobRepoIf[0].CommitVecWrapIf.p_RefDataIf[i].p_data);
	}

// 此函数仅用作测试
void
ztest_print(void) {
        fprintf(stderr,"RepoNum: %d\n", zRepoNum);
        for (_i i = 0; i < zRepoNum; i++) {
        fprintf(stderr,"RepoPath: %s\n", zpRepoGlobIf[i].RepoPath);
        }

        fprintf(stderr,"zInotifyFD: %d\n", zInotifyFD);
        for (_i i = 0; i < zWatchHashSiz; i++) {
        if (NULL != zpObjHash[i]) {
                fprintf(stderr,"OBJPath: %s\n", zpObjHash[i]->path);
                fprintf(stderr,"zpRegexPattern: %s\n", zpObjHash[i]->zpRegexPattern);
                fprintf(stderr,"UpperWid: %d\n", zpObjHash[i]->UpperWid);
                fprintf(stderr,"RecursiveMark: %d\n", zpObjHash[i]->RecursiveMark);
                fprintf(stderr,"CallBack: %p\n", zpObjHash[i]->CallBack);
        }
        }

        for (_i i = 0; i < zRepoNum; i++) {
        if (NULL == zppCURRENTsig[i]) {
                fprintf(stderr,"NULL sig\n");
                goto zMark;
        }
        fprintf(stderr,"zppCURRENTsig: ");
        for (_i z = 0; z < 40; z++) {
                fprintf(stderr,"%c", zppCURRENTsig[i][z]);
        }
        fprintf(stderr,"\n");

zMark:
//        fprintf(stderr,"CacheVecSiz: %d\n", zpCacheVecSiz[i]);
//        for (_i j = 0; j < zpCacheVecSiz[i]; j++) {
//                fprintf(stderr,"CacheVersion: %ld, CacheDiffFilePath: %s, CacheDiffFilePathLen: %zd\n", ((zFileDiffInfo *)zppCacheVecIf[i][j].iov_base)->CacheVersion, ((zFileDiffInfo *)zppCacheVecIf[i][j].iov_base)->path, zppCacheVecIf[i][j].iov_len);
//        }

        fprintf(stderr,"zLogCacheSiz: %d\n", zpLogCacheVecSiz[i]);
        for (_i j = 0; j < zpLogCacheVecSiz[i]; j++) {
                if (NULL != zppLogCacheVecIf[i][j].iov_base) {
                fprintf(stderr,"PreloadLog: %s, PreloadLogLen: %zd\n", ((zDeployLogInfo *)zppLogCacheVecIf[i][j].iov_base)->path, zppLogCacheVecIf[i][j].iov_len);
                for (_ul k = 0; k < (zppLogCacheVecIf[i][j].iov_len - sizeof(zDeployLogInfo)); k++) {
                        fprintf(stderr,"%c", (((zDeployLogInfo *)zppLogCacheVecIf[i][j].iov_base)->path)[k]);
                }
                fprintf(stderr, "\n");
                }
        }
    }

        for (_i i = 0; i < zRepoNum; i++) {
        fprintf(stderr,"Totalhost: %d\n", zpRepoGlobIf[i].TotalHost);
        fprintf(stderr,"zpReplyCnt: %d\n", zpRepoGlobIf[i].ReplyCnt);
        for (_i j = 0; j < zpRepoGlobIf[i].TotalHost; j++) {
                fprintf(stderr,"ClientAddr: %d\n", zpRepoGlobIf[i].p_DpResList[j].ClientAddr);
                fprintf(stderr,"RepoId: %d\n", zpRepoGlobIf[i].p_DpResList[j].RepoId);
                fprintf(stderr,"DeployState: %d\n", zpRepoGlobIf[i].p_DpResList[j].DeployState);
                fprintf(stderr,"next: %p\n", zpRepoGlobIf[i].p_DpResList[j].p_next);
        }

        zDeployResInfo *zpTmp;
        for (_i k = 0; k < zDeployHashSiz; k++) {
                if (NULL == zpRepoGlobIf[i].p_DpResHash[k]) {
                continue;
                } else {
                zpTmp = zpRepoGlobIf[i].p_DpResHash[k];
                do {
                        fprintf(stderr, "HASH %%: %u===========\n", k);
                        fprintf(stderr,"HashClientAddr: %d\n", zpTmp->ClientAddr);
                        fprintf(stderr,"hashRepoId: %d\n", zpTmp->RepoId);
                        fprintf(stderr,"hashDeployState: %d\n", zpTmp->DeployState);
                        fprintf(stderr,"hashnext: %p\n", zpTmp->p_next);

                        zpTmp = zpTmp->p_next;
                } while(NULL != zpTmp);
                }
        }
        }
}
