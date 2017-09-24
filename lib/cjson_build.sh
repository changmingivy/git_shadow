#!/bin/sh

rm -rf cjson_build
mkdir cjson_build
cd cjson_build

cmake .. -DBUILD_SHARED_LIBS=0ff -DENABLE_CJSON_TEST=Off -DCMAKE_INSTALL_PREFIX=/home/git/zgit_shadow/lib/cjson

make
