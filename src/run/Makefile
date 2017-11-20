LIB_NAME = zLibUtils.a

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =

$(LIB_NAME):\
	$(LIB_NAME)(zRun.o)\
	$(LIB_NAME)(zDpOps.o)\
	$(LIB_NAME)(zNativeOps.o)\
	$(LIB_NAME)(zLibSsh.o)\
	$(LIB_NAME)(zLibGit.o)\
	$(LIB_NAME)(zNativeUtils.o)\
	$(LIB_NAME)(zNetUtils.o)\
	$(LIB_NAME)(zPosixReg.o)\
	$(LIB_NAME)(zPgSQL.o)\
	$(LIB_NAME)(zThreadPool.o)

.c.o:
	$(CC) -c $(CCFLAGS) $(INC_ARGS) $<
	cd common && $(CC) $(CCFLAGS) $(INC_ARGS) -c $< && mv *.o ../
	cd ../net && $(CC) $(CCFLAGS) $(INC_ARGS) -c $< && mv *.o ../
	cd ../db && $(CC) $(CCFLAGS) $(INC_ARGS) -c $< && mv *.o ../
	cd ../regex && $(CC) $(CCFLAGS) $(INC_ARGS) -c $< && mv *.o ../
	cd ../json && $(CC) $(CCFLAGS) $(INC_ARGS) -c $< && mv *.o ../


