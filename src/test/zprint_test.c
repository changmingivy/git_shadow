
// 此函数仅用作测试
void
ztest_print(void) {
    fprintf(stderr,"RepoNum: %d\n", zRepoNum);
    for (_i i = 0; i < zRepoNum; i++) {
        fprintf(stderr,"RepoPath: %s\n", zppRepoPathList[i]);
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
        if (NULL == zppCurTagSig[i]) {
            fprintf(stderr,"NULL sig\n");
            goto zMark;
        }
        fprintf(stderr,"zppCurTagSig: ");
        for (_i z = 0; z < 40; z++) {
            fprintf(stderr,"%c", zppCurTagSig[i][z]);
        }
        fprintf(stderr,"\n");

zMark:
        fprintf(stderr,"CacheVecSiz: %d\n", zpCacheVecSiz[i]);
        fprintf(stderr,"zPreLoadLogSiz: %d\n", zpPreLoadLogVecSiz[i]);

        for (_i j = 0; j < zpCacheVecSiz[i]; j++) {
            fprintf(stderr,"CacheDiffFilePath: %s, CacheDiffFilePathLen: %zd\n", ((zFileDiffInfo *)zppCacheVecIf[i][j].iov_base)->path, zppCacheVecIf[i][j].iov_len);
        }

        for (_i j = 0; j < zpPreLoadLogVecSiz[i]; j++) {
            fprintf(stderr,"PreloadLog: %s, PreloadLogLen: %zd\n", zppPreLoadLogVecIf[i][j].iov_base,zppPreLoadLogVecIf[i][j].iov_len);
        }
    }

    for (_i i = 0; i < zRepoNum; i++) {
        fprintf(stderr,"LogFd-meta: %d\n", zpLogFd[0][i]);
        fprintf(stderr,"LogFd-data: %d\n", zpLogFd[1][i]);
        fprintf(stderr,"LogFd-sig: %d\n", zpLogFd[2][i]);
    }

    for (_i i = 0; i < zRepoNum; i++) {
        fprintf(stderr,"Totalhost: %d\n", zpTotalHost[i]);
        fprintf(stderr,"zpReplyCnt: %d\n", zpReplyCnt[i]);
        for (_i j = 0; j < zpTotalHost[i]; j++) {
            fprintf(stderr,"ClientAddr: %d\n", zppDpResList[i][j].ClientAddr);
            fprintf(stderr,"RepoId: %d\n", zppDpResList[i][j].RepoId);
            fprintf(stderr,"DeployState: %d\n", zppDpResList[i][j].DeployState);
            fprintf(stderr,"next: %p\n", zppDpResList[i][j].p_next);
        }

        for (_i k = 0; k < zDeployHashSiz; k++) {
            if (NULL == zpppDpResHash[i][k]) {continue;}
            else {
                do {
                    fprintf(stderr,"HashClientAddr: %d\n", zpppDpResHash[i][k]->ClientAddr);
                    fprintf(stderr,"hashRepoId: %d\n", zpppDpResHash[i][k]->RepoId);
                    fprintf(stderr,"hashDeployState: %d\n", zpppDpResHash[i][k]->DeployState);
                    fprintf(stderr,"hashnext: %p\n", zpppDpResHash[i][k]->p_next);

                    zpppDpResHash[i][k] = zpppDpResHash[i][k]->p_next;
                } while(NULL != zpppDpResHash[i][k]);
            }
        }
    }
}

