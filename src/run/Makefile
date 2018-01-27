CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 依赖上层 Makefile 传入
INC_ARGS =

utils.a: 
	$(CC) -c $(CCFLAGS) $(INC_ARGS) *.c
	(cd dp; make INC_ARGS="$(INC_ARGS)")
	(cd sv; make INC_ARGS="$(INC_ARGS)")
	(cd sv/cloud; make INC_ARGS="$(INC_ARGS)")
	(cd utils/common; make INC_ARGS="$(INC_ARGS)")
	(cd utils/net; make INC_ARGS="$(INC_ARGS)")
	(cd utils/db; make INC_ARGS="$(INC_ARGS)")
	(cd utils/regex; make INC_ARGS="$(INC_ARGS)")
	(cd utils/json; make INC_ARGS="$(INC_ARGS)")
	(cd utils/algo; make INC_ARGS="$(INC_ARGS)")
	ar -crv $@ `find . -type f -name "*.o"`
