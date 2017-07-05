#!/usr/bin/env bash

qemu-system-x86_64 \
-enable-kvm \
-machine q35,accel=kvm -device intel-iommu \
-cpu host -smp 2,sockets=2,cores=1,threads=1 \
-m 2048 \
-netdev tap,ifname=BASE,script=tap.sh,downscript=no,vhost=on,id=vmNic_BASE -device virtio-net-pci,mac=00:e0:4c:49:99:99,netdev=vmNic_BASE \
-drive file=../images/CentOS_base.img,if=none,cache=writeback,media=disk,id=vmDisk_BASE -device virtio-blk-pci,drive=vmDisk_BASE \
-drive file=../ISOs/CentOS.iso,readonly=on,media=cdrom \
-boot order=cd \
-name vmBASE \
-vnc :0 \
-daemonize