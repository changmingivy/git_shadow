#!/usr/bin/env bash
zHostNatIf="enp2s0"
zHostIP="172.30.50.97"
zBridgeIf=br0

modprobe tun
modprobe vhost
modprobe vhost_net

ip link add $zBridgeIf type bridge 2>/dev/null
ip link set $zBridgeIf up
ip addr add "172.16.254.254/16" dev $zBridgeIf

echo 1 > /proc/sys/net/ipv4/ip_forward

iptables -t nat -F POSTROUTING
iptables -t nat -A POSTROUTING -s 172.16.0.0/16 -o $zHostNatIf -j SNAT --to-source $zHostIP

qemu-system-x86_64 \
-enable-kvm \
-machine q35,accel=kvm -device intel-iommu \
-cpu host -smp 4,sockets=4,cores=1,threads=1 \
-m 4096 \
-netdev tap,ifname=BASE,script=tap.sh,downscript=no,vhost=on,id=vmNic_BASE -device virtio-net-pci,mac=00:e0:4c:49:99:99,netdev=vmNic_BASE \
-drive file=/home/fh/Qemu/IMGs/CentOS_base.img,if=none,cache=writeback,media=disk,id=vmDisk_BASE -device virtio-blk-pci,drive=vmDisk_BASE \
-boot order=cd \
-name vmBASE \
-vnc :99 \
-daemonize

#-drive file=/home/fh/Qemu/ISOs/CentOS_6.iso,readonly=on,media=cdrom \
