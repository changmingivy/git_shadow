#!/usr/bin/env bash
#
# 0、目录结构:conf、images、ISOs
# 1、安装镜像存放于"../ISOs"路径下，命名格式：FreeBSD.iso
# 2、配置好每个OS的基础镜像，置于"../images"路径下，命名格式:FreeBSD_base.img
#

zMaxVmNum="96"

if [[ $1 == '' ]]; then
    zVmNum=1
    printf "\033[31;01m\$VmNum is not specified, defaults to 1\n\033[00m"
elif [[ $1 =~ [0-9][0-9]? ]]; then
    zVmNum=$1
else
    printf "\033[31;01m\$1 is not a number!!!\n\033[00m"
    exit
fi

if [[ $2 == '' ]]; then
    zOS=CentOS
    printf "\033[31;01mOS is not specified, defaults to \"CentOS-6.9\"\n\033[00m"
else
    zOS=$2
fi

if [[ $1 -gt $zMaxVmNum ]]; then
    printf "\033[31;01mvmNum > ${zMaxVmNum}!!!\n\033[00m"
    exit
fi

case $zOS in
    CentOS)
        zVersion=06
        zAddrPos=01
        ;;
    *)
        printf "\033[31;01mUnknown OS name!!!\n\033[00m"
        printf "Supported OS name:FreeBSD CentOS\n"
        exit
        ;;
esac

zISO="${zOS}.iso"
zBaseImg="${zOS}_base.img"
zCpuNum="2"
zMem="512M"
zDiskSiz="20G"
zHostNatIf="eno1"
zHostIP="10.10.40.49"
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

zvm_func() {
    qemu-system-x86_64 \
    -enable-kvm \
    -machine q35,accel=kvm -device intel-iommu \
    -cpu host -smp $zCpuNum,sockets=$zCpuNum,cores=1,threads=1 \
    -m $zMem \
    -netdev tap,ifname=${zOS}_$1,script=tap.sh,downscript=no,vhost=on,id=vmNic_${zOS}_$1 -device virtio-net-pci,mac=00:e0:4c:49:$1:${zVersion},netdev=vmNic_${zOS}_$1 \
    -drive file=$zImgPath,if=none,cache=writeback,media=disk,id=vmDisk_${zOS}_$1 -device virtio-blk-pci,drive=vmDisk_${zOS}_$1 \
    -boot order=cd \
    -name vm${zOS}_$1 \
    -vnc :$2 \
    -daemonize
}

zops_func() {
    eval zX=$(($1 + $zAddrPos))

    if [[ 10 -gt $zX ]]; then
        zMark="0$zX"
    else
        zMark=$zX
    fi

    rm /tmp/images/CentOS_$zMark.img

    zImgPath=/tmp/images/${zOS}_${zMark}.img
    local zBaseImgName=${zOS}_base.img
    local zVncID=$zMark

    if [[ 0 -eq $(ls /home/images | grep -c $zBaseImgName) ]];then
        zImgPath=/home/images/$zBaseImgName
        zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on $zImgPath'
    else
        mkdir -p /tmp/images
        zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on,backing_file=/home/images/$zBaseImg $zImgPath'
    fi

    if [[ 0 -eq $(ls /home/images | grep -c $zImgPath) ]];then
        eval $zCommand
        if [[ 0 -ne $? ]];then
            printf "\033[31;01mCan't create $zImgPath!!!\n\033[00m"
            exit
        fi
    fi

    zvm_func $zMark $zVncID
}

for ((i=0; i<$zVmNum; i++))
do
    zops_func $i
    if [[ 3 -eq $(( i % 4)) ]]; then
        sleep 2
    fi
done

#-drive file=/home/ISOs/$zISO,readonly=on,media=cdrom \
#-monitor tcp:$zHostIP:$(($1 * 1000)),server,nowait \
