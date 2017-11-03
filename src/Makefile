all: git_shadow

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

INC_DIR = ../inc
SSH_INC_DIR = ../lib/libssh2/include
GIT_INC_DIR = ../lib/libgit2/include

SSH_LIB_DIR = ../lib/libssh2/lib64
GIT_LIB_DIR = ../lib/libgit2/lib

SUB_SRC_DIR = ./run
INSTALL_DIR = ../bin

git_shadow: zmain.o zLibUtils.a
	$(CC) -o git_shadow\
		-L$(SSH_LIB_DIR) -lssh2\
		-L$(GIT_LIB_DIR) -lgit2\
		-l pthread\
		zmain.o $(SUB_SRC_DIR)/zLibUtils.a

zmain.o:
	$(CC) -c -I$(INC_DIR) -I$(SSH_INC_DIR) -I$(GIT_INC_DIR) $(CCFLAGS) zmain.c

zLibUtils.a:
	(cd $(SUB_SRC_DIR); $(MAKE))

install: git_shadow
	@mv git_shadow $(INSTALL_DIR)
