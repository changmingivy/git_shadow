struct zThreadPool__ {
    void (* init) (void);
    void (* add) (void * (*) (void *), void *);
};

extern struct zThreadPool__ zThreadPool_;
