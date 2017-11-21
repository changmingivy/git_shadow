CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 依赖上层 Makefile 传入
INC_ARGS =

utils.a: 
	$(CC) -c $(CCFLAGS) $(INC_ARGS) *.c
	(cd common; make INC_ARGS="$(INC_ARGS)")
	(cd net; make INC_ARGS="$(INC_ARGS)")
	(cd db; make INC_ARGS="$(INC_ARGS)")
	(cd regex; make INC_ARGS="$(INC_ARGS)")
	(cd json; make INC_ARGS="$(INC_ARGS)")
	ar -crv $@ `find . -type f -name "*.o"`
