#!/bin/sh
    export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
    export GIT_DIR="/home/git/miaopai/.git"

    cd /home/git/miaopai &&
    git pull --force ./.git server:master
