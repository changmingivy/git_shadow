LIB_NAME = utils.a

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =

$(LIB_NAME):
	$(LIB_NAME)(zRun.o)\
	$(LIB_NAME)(zNativeOps.o)\
	$(LIB_NAME)(zDpOps.o)\
	$(LIB_NAME)(common/common.a)\
	$(LIB_NAME)(net/net.a)\
	$(LIB_NAME)(db/db.a)\
	$(LIB_NAME)(regex/regex.a)\
	$(LIB_NAME)(json/json.a)

.c.o:
	$(CC) -c $(CCFLAGS) $(INC_ARGS) $<

common.a:
	(cd common; make INC_ARGS="$(INC_ARGS)")

net.a:
	(cd net; make INC_ARGS="$(INC_ARGS)")

db.a:
	(cd db; make INC_ARGS="$(INC_ARGS)")

regex.a:
	(cd regex; make INC_ARGS="$(INC_ARGS)")

json.a:
	(cd json; make INC_ARGS="$(INC_ARGS)")
