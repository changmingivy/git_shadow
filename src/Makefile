all: git_shadow

CC = clang
CCFLAGS = -g -O2 -std=c99 -Wall -Wextra

ROOT_DIR = ${HOME}/zgit_shadow2

# 默认值，会被外部传参覆盖：third party libs, absolute path
SSH_LIB_DIR = $(ROOT_DIR)/lib/libssh2/lib
GIT_LIB_DIR = $(ROOT_DIR)/lib/libgit2/lib
PG_LIB_DIR = ${HOME}/.____PostgreSQL/lib

# include files
SSH_INC_DIR = `dirname $(SSH_LIB_DIR)`/include
GIT_INC_DIR = `dirname $(GIT_LIB_DIR)`/include
PG_INC_DIR = `dirname $(PG_LIB_DIR)`/include

INC_ARGS =\
	-I$(ROOT_DIR)/inc\
	-I$(ROOT_DIR)/inc/common\
	-I$(ROOT_DIR)/inc/net\
	-I$(ROOT_DIR)/inc/db\
	-I$(ROOT_DIR)/inc/regex\
	-I$(ROOT_DIR)/inc/json\
	-I$(SSH_INC_DIR)\
	-I$(GIT_INC_DIR)\
	-I$(PG_INC_DIR)

# all .c files except zmain.c
SUB_SRC_DIR = $(ROOT_DIR)/src/run

# where to install
INSTALL_DIR = $(ROOT_DIR)/bin

# build final binary
git_shadow: zmain.o utils.a
	$(CC) -o git_shadow\
		-L$(SSH_LIB_DIR) -lssh2\
		-L$(GIT_LIB_DIR) -lgit2\
		-L$(PG_LIB_DIR) -lpq\
		-lpthread\
		zmain.o $(SUB_SRC_DIR)/utils.a

# main() func...
zmain.o:
	$(CC) -c $(CCFLAGS) $(INC_ARGS) zmain.c

# archive all submodules to a[n] .a file
# need a sub make in $(SUB_SRC_DIR)
utils.a:
	(cd $(SUB_SRC_DIR); make INC_ARGS="$(INC_ARGS)")

# install
install: git_shadow
	-@strip git_shadow
	mv git_shadow $(INSTALL_DIR)

#clean
clean:
	-find . -type f -name "*.a" -o -name "*.o" | xargs rm -f

