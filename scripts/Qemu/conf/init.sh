#!/usr/bin/env bash
#
# 0、目录结构:conf、images、ISOs
# 1、安装镜像存放于"../ISOs"路径下，命名格式：FreeBSD.iso
# 2、配置好每个OS的基础镜像，置于"../images"路径下，命名格式:FreeBSD_base.img
# 

zISO="${zOS}.iso"
zBaseImg="${zOS}_base.img"
zCpuNum="1"
zMem="1G"
zMaxMem="2G"
zDiskSiz="20G"
zHostNatIf="wlp1s0"
zHostIP="10.30.2.126"

zMaxVmNum="80"

modprobe tun
modprobe vhost
modprobe vhost_net

echo 1 > /proc/sys/net/ipv4/ip_forward

iptables -t nat -F POSTROUTING
iptables -t nat -A POSTROUTING -s 172.16.0.0/16 -o $zHostNatIf -j SNAT --to-source $zHostIP

if [[ $1 == '' ]]; then
	zVmNum=1
	printf "\033[31;01m\$VmNum is not specified, defaults to 1\n\033[00m"
elif [[ $1 =~ [0-9][0-9] ]]; then
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
	FreeBSD)
		zVersion=11
		zAddrPos=02
		;;
	CentOS)
		zVersion=06
		zAddrPos=10
		;;
	*)
		printf "\033[31;01mUnknown OS name!!!\n\033[00m"
		printf "Supported OS name:FreeBSD CentOS\n"
		exit
		;;
esac

zvm_func() {
	qemu-system-x86_64 \
	-enable-kvm \
	-machine q35,accel=kvm -device intel-iommu \
	-cpu host -smp $zCpuNum,sockets=$zCpuNum,cores=1,threads=1 \
	-m $zMem,slots=4,maxmem=$zMaxMem \
	-netdev tap,ifname=${zOS}_$1,script=tap.sh,downscript=no,vhost=on,id=vmNic_${zOS}_$1 -device virtio-net-pci,mac=00:e0:4c:49:$1:${zVersion},netdev=vmNic_${zOS}_$1 \
	-drive file=../images/$zImgName,if=none,cache=writeback,media=disk,id=vmDisk_${zOS}_$1 -device virtio-blk-pci,drive=vmDisk_${zOS}_$1 \
	-drive file=../ISOs/$zISO,readonly=on,media=cdrom \
	-boot order=cd \
	-name vm${zOS}_$1 \
	-monitor tcp:$zHostIP:$(($1 * 1000)),server,nowait \
	-vnc :$2 \
	-daemonize
}

zops_func() {
	eval zMark=$(($1 + $zAddrPos))
	local zImgName=${zOS}_$x.img
	local zBaseImgName=${zOS}_base.img
	local zVncID=$zMark

	if [[ 0 -eq $(ls ../images | grep -c $zBaseImgName) ]];then
		zImgName=$zBaseImgName
		zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on ../images/$zImgName'
	else
		mkdir -p /tmp/images
		zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on,backing_file=../images/$zBaseImg /tmp/images/$zImgName'
	fi

	if [[ 0 -eq $(ls ../images | grep -c $zImgName) ]];then
		eval $zCommand
		if [[ 0 -ne $? ]];then
			printf "\033[31;01mCan't create $zImgName!!!\n\033[00m"
			exit
		fi
	fi

	zvm_func $zMark $zVncID
}

for ((i=0; i<$zVmNum; i++))
do
	zops_func $i
done
