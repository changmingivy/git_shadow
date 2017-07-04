
// 此函数仅用作测试
void
ztest_print(void) {
    printf("RepoNum: %d\n", zRepoNum);
    for (_i i = 0; i < zRepoNum; i++) {
        printf("RepoPath: %s\n", zppRepoPathList[i]);
    }

    printf("zInotifyFD: %d\n", zInotifyFD);
    for (_i i = 0; i < zWatchHashSiz; i++) {
        if (NULL != zpObjHash[i]) {
            printf("OBJPath: %s\n", zpObjHash[i]->path);
            printf("zpRegexPattern: %s\n", zpObjHash[i]->zpRegexPattern);
            printf("UpperWid: %d\n", zpObjHash[i]->UpperWid);
            printf("RecursiveMark: %d\n", zpObjHash[i]->RecursiveMark);
            printf("CallBack: %p\n", zpObjHash[i]->CallBack);
        }
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("zppCurTagSig: ");
        for (_i z = 0; z < 40; z++) {
            printf("%c", zppCurTagSig[i][z]);
        }
        printf("\n");
        printf("CacheVecSiz: %d\n", zpCacheVecSiz[i]);
        printf("zPreLoadLogSiz: %d\n", zpPreLoadLogVecSiz[i]);

        for (_i j = 0; j < zpCacheVecSiz[i]; j++) {
            printf("CacheDiffFilePath: %s, CacheDiffFilePathLen: %zd\n", ((zFileDiffInfo *)zppCacheVecIf[i][j].iov_base)->path, zppCacheVecIf[i][j].iov_len);
        }

        for (_i j = 0; j < zpPreLoadLogVecSiz[i]; j++) {
            printf("PreloadLog: %s, PreloadLogLen: %zd\n", zppPreLoadLogVecIf[i][j].iov_base,zppPreLoadLogVecIf[i][j].iov_len);
        }
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("LogFd-meta: %d\n", zpLogFd[0][i]);
        printf("LogFd-data: %d\n", zpLogFd[1][i]);
        printf("LogFd-sig: %d\n", zpLogFd[2][i]);
    }

    for (_i i = 0; i < zRepoNum; i++) {
        printf("Totalhost: %d\n", zpTotalHost[i]);
        printf("zpReplyCnt: %d\n", zpReplyCnt[i]);
        for (_i j = 0; j < zpTotalHost[i]; j++) {
            printf("ClientAddr: %d\n", zppDpResList[i][j].ClientAddr);
            printf("RepoId: %d\n", zppDpResList[i][j].RepoId);
            printf("DeployState: %d\n", zppDpResList[i][j].DeployState);
            printf("next: %p\n", zppDpResList[i][j].p_next);
        }

        for (_i k = 0; k < zDeployHashSiz; k++) {
            if (NULL == zpppDpResHash[i][k]) {continue;}
            else {
                do {
                    printf("HashClientAddr: %d\n", zpppDpResHash[i][k]->ClientAddr);
                    printf("hashRepoId: %d\n", zpppDpResHash[i][k]->RepoId);
                    printf("hashDeployState: %d\n", zpppDpResHash[i][k]->DeployState);
                    printf("hashnext: %p\n", zpppDpResHash[i][k]->p_next);

                    zpppDpResHash[i][k] = zpppDpResHash[i][k]->p_next;
                } while(NULL != zpppDpResHash[i][k]);
            }
        }
    }
}

