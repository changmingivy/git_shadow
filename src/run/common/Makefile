CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =
LIB_NAME = `basename ${PWD}`.a

$(LIB_NAME):
	mv $(LIB_NAME) ../

.c.o:
	$(CC) -c $(CCFLAGS) $(INC_ARGS) $<
	ar -crv $(LIB_NAME) $@
