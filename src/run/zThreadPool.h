struct zThreadPool__ {
    void (* init) (void);
    void (* add) (void * (*) (void *), void *);
};

#ifndef _SELF_
extern struct zThreadPool__ zThreadPool_;
#endif
