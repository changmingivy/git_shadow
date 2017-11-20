CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =
LIB_NAME = `basename \`pwd\``.a

$(LIB_NAME):
	$(CC) -c $(CCFLAGS) $(INC_ARGS) *.c
	ar -crv $(LIB_NAME) `find . -name "*.o"`
	mv $(LIB_NAME) ../
