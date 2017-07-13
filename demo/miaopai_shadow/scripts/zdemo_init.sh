#!/bin/sh

for x in conf inc scripts src; do
    cp -rp ../$x ../demo/miaopai_shadow
done
