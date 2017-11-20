CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

# 如下四项，依赖上层 Makefile 传入
INC_ARGS =
OBJ_NAME = `basename ${PWD}`

$(OBJ_NAME).o:
	$(CC) *.c -c $(CCFLAGS) $(INC_ARGS) -o $(OBJ_NAME).o
	mv *.o ../
