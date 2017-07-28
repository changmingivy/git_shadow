#!/bin/sh

for x in bin conf inc scripts src; do
    cp -r $HOME/zgit_shadow/$x $HOME/zgit_shadow/demo/miaopai_shadow
done
