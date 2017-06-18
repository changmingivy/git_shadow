#!/bin/sh

yes|yum install pcre2-devel git gcc

mkdir -p ../bin

if [[ 0 -eq `ls ../bin | grep -c git_shadow` ]]
then
	gcc -O2 \
		-std=c11 \
		-I../inc \
		-lpthread \
		-lpcre2-8 \
		-D_XOPEN_SOURCE=700 \
		-o ../bin/git_shadow \
		../src/zmain.c \
		../src/common/*
fi
