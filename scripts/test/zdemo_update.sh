#!/bin/sh

for x in bin conf inc scripts src; do
    cp -r /home/git/zgit_shadow/$x /home/git/zgit_shadow/demo/miaopai_shadow
done

chown -R git:git /home/git
