#ifndef _Z
    #include "../../zmain.c"
#endif
 
#define zThreadPollSiz 129  // 允许同时处于空闲状态的线程数量，即常备线程数量
#define zThreadPollSizMark (zThreadPollSiz - 1)

typedef void * (* zThreadPoolOps) (void *);  // 线程池回调函数

struct zThreadPoolInfo {
    pthread_t SelfTid;
    pthread_cond_t CondVar;

    zThreadPoolOps func;
    void *p_param;
};
typedef struct zThreadPoolInfo zThreadPoolInfo;

/* 线程池栈结构 */
zThreadPoolInfo *zpPoolStackIf[zThreadPollSiz];

_i ____zStackHeader = -1;
pthread_mutex_t ____zStackHeaderLock = PTHREAD_MUTEX_INITIALIZER;
pthread_t ____zThreadPoolTidTrash;

void *
zthread_pool_meta_func(void *zpIf) {
    zThreadPoolInfo *zpSelfTask;
    zMem_C_Alloc(zpSelfTask, zThreadPoolInfo, 1);  // 分配已清零的空间
    zCheck_Pthread_Func_Exit( pthread_cond_init(&(zpSelfTask->CondVar), NULL) );

    zpSelfTask->SelfTid = pthread_self();
    pthread_detach(zpSelfTask->SelfTid);

    /* 线程可被cancel，且cancel属性设置为立即退出 */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);

    void **zppMetaIf = &zpIf;

zMark:
    pthread_mutex_lock(&____zStackHeaderLock);
    if (____zStackHeader < zThreadPollSizMark) {  
        zpPoolStackIf[++____zStackHeader] = zpSelfTask;
        while (NULL == zpSelfTask->func) {
            pthread_cond_wait( &(zpSelfTask->CondVar), &____zStackHeaderLock );
        }
        pthread_mutex_unlock(&____zStackHeaderLock);
    
        /* 每个使用线程池的函数入参，必须在最前面预留一个 sizeof(void *) 大小的空间，用于存放线程池传入的元数据 */
        if (NULL == zpSelfTask->p_param) {
            zpSelfTask->func(zpSelfTask->p_param);
        } else {
            zppMetaIf = zpSelfTask->p_param;
            zppMetaIf[0] = zpSelfTask;  // 用于为 pthread_kill 及回收资源动作提供参数
            zpSelfTask->func(zpSelfTask->p_param);
            zppMetaIf[0] = NULL;
        }
    
        zpSelfTask->func = NULL;
        goto zMark;
    } else {  // 太多空闲线程时，回收资源
        pthread_mutex_unlock(&____zStackHeaderLock);
        pthread_cond_destroy(&(zpSelfTask->CondVar));
        free(zpSelfTask);
        return (void *) -1;
    }
}

void
zthread_poll_init(void) {
    for (_i zCnter = 0; zCnter < zThreadPollSiz; zCnter++) {
        zCheck_Pthread_Func_Exit( pthread_create(&____zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL) );
    }
}

/* 
 * !!!!    注：此模型尚没有考虑线程总量超过操作系统限制的情况    !!!!
 *
 * 线程池容量不足时，自动扩容
 * 空闲线程过多时，会自动缩容
 */
#define zAdd_To_Thread_Pool(zFunc, zParam) do {\
    pthread_mutex_lock(&____zStackHeaderLock);\
    while (0 > ____zStackHeader) {\
        pthread_mutex_unlock(&____zStackHeaderLock);\
        pthread_create(&____zThreadPoolTidTrash, NULL, zthread_pool_meta_func, NULL);\
        pthread_mutex_lock(&____zStackHeaderLock);\
    }\
    _i ____zKeepStackHeader= ____zStackHeader;\
    zpPoolStackIf[____zStackHeader]->func = zFunc;\
    zpPoolStackIf[____zStackHeader]->p_param = zParam;\
    ____zStackHeader--;\
    pthread_mutex_unlock(&____zStackHeaderLock);\
    pthread_cond_signal(&(zpPoolStackIf[____zKeepStackHeader]->CondVar));\
    }\
} while(0)
