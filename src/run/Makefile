LIB_NAME = utils.a

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =

$(LIB_NAME): $(LIB_NAME)(*.o)\
	$(LIB_NAME)(common.a)\
	$(LIB_NAME)(net.a)\
	$(LIB_NAME)(db.a)\
	$(LIB_NAME)(regex.a)\
	$(LIB_NAME)(json.a)

.c.o:
	$(CC) -c $(CCFLAGS) $(INC_ARGS) $<
	(cd common; make INC_ARGS="$(INC_ARGS)")
	(cd net; make INC_ARGS="$(INC_ARGS)")
	(cd db; make INC_ARGS="$(INC_ARGS)")
	(cd regex; make INC_ARGS="$(INC_ARGS)")
	(cd json; make INC_ARGS="$(INC_ARGS)")
