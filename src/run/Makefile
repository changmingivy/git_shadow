LIB_NAME = zLibUtils.a

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

INC_DIR = ../../inc
SSH_INC_DIR =
GIT_INC_DIR =

$(LIB_NAME):\
	$(LIB_NAME)(zRun.o)\
	$(LIB_NAME)(zDpOps.o)\
	$(LIB_NAME)(zNativeOps.o)\
	$(LIB_NAME)(zLibSsh.o)\
	$(LIB_NAME)(zLibGit.o)\
	$(LIB_NAME)(zNativeUtils.o)\
	$(LIB_NAME)(zNetUtils.o)\
	$(LIB_NAME)(zPosixReg.o)\
	$(LIB_NAME)(zThreadPool.o)

.c.o:
	$(CC) -c -I$(INC_DIR) -I$(SSH_INC_DIR) -I$(GIT_INC_DIR) $(CCFLAGS) $<


