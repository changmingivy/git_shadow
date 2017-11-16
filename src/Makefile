all: git_shadow

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

ROOT_DIR = ${HOME}/zgit_shadow2

# 默认值，会被外部传参覆盖：third party libs, absolute path
SSH_LIB_DIR = $(ROOT_DIR)/lib/libssh2/lib
GIT_LIB_DIR = $(ROOT_DIR)/lib/libgit2/lib
PG_LIB_DIR = ${HOME}/.____PostgreSQL/lib

# include files
INC_DIR = $(ROOT_DIR)/inc
SSH_INC_DIR = `dirname $(SSH_LIB_DIR)`/include
GIT_INC_DIR = `dirname $(GIT_LIB_DIR)`/include
PG_INC_DIR = `dirname $(PG_LIB_DIR)`/include

# all .c files except zmain.c
SUB_SRC_DIR = $(ROOT_DIR)/src/run

# where to install
INSTALL_DIR = $(ROOT_DIR)/bin

# build final binary
git_shadow: zmain.o zLibUtils.a
	$(CC) -o git_shadow\
		-L$(SSH_LIB_DIR) -lssh2\
		-L$(GIT_LIB_DIR) -lgit2\
		-L$(PG_LIB_DIR) -lpq\
		-lpthread\
		zmain.o $(SUB_SRC_DIR)/zLibUtils.a

# main() func...
zmain.o:
	$(CC) -c -I$(INC_DIR) -I$(SSH_INC_DIR) -I$(GIT_INC_DIR) -I$(PG_INC_DIR) $(CCFLAGS) zmain.c

# archive all submodules to a[n] .a file
# need a sub make in $(SUB_SRC_DIR)
zLibUtils.a:
	(cd $(SUB_SRC_DIR); make INC_DIR=$(INC_DIR) SSH_INC_DIR=$(SSH_INC_DIR) GIT_INC_DIR=$(GIT_INC_DIR) PG_INC_DIR=$(PG_INC_DIR))

# install
install: git_shadow
	-@strip git_shadow
	mv git_shadow $(INSTALL_DIR)

#clean
clean:
	-rm -f zmain.o $(SUB_SRC_DIR)/zLibUtils.a



