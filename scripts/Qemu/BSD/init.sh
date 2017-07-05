#!/usr/bin/env bash
#
# 0、目录结构:conf、images、ISOs
# 1、安装镜像存放于"../ISOs"路径下，命名格式：FreeBSD.iso
# 2、配置好每个OS的基础镜像，置于"../images"路径下，命名格式:FreeBSD_base.img
# 

zMaxVmNum="80"

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

zISO="${zOS}.iso"
zBaseImg="${zOS}_base.img"
zCpuNum="4"
zMem="640M"
zDiskSiz="20G"
zHostNatIf="wlp1s0"
zHostIP="10.30.2.126"
zBridgeIf=bridge0

if [[ 0 -eq `ifconfig br0 $zBridgeIf | wc -l` ]]; then
	ifconfig $zBridgeIf create
	ifconfig $zBridgeIf up
	ifconfig $zBridgeIf "172.16.0.1/16"
fi

sysctl net.inet.ip.forwarding=1
#kldload dummynet

ipfw "-q -f flush"

#ipfw "add pipe 10 ip from 172.16.10.1 to 172.16.0.0/16 in"
#ipfw "pipe 10 config delay 200ms bw 300Kbit/s"
#ipfw "add pipe 10 ip from 172.16.10.1 to 172.16.0.0/16 out"
#ipfw "pipe 20 config delay 300ms bw 200Kbit/s"

ipfw "add nat 20 ip from 172.16.0.0/16 to any"
ipfw "nat 10 config ip $zHostIp"

ipfw "add pass any to any"

zvm_func() {
	qemu-system-x86_64 \
	-machine q35 -device intel-iommu \
	-cpu host -smp $zCpuNum,sockets=$zCpuNum,cores=1,threads=1 \
	-m $zMem \
	-netdev tap,ifname=${zOS}_$1,script=tap.sh,downscript=no,vhost=on,id=vmNic_${zOS}_$1 -device virtio-net-pci,mac=00:e0:4c:49:$1:${zVersion},netdev=vmNic_${zOS}_$1 \
	-drive file=$zImgPath,if=none,cache=writeback,media=disk,id=vmDisk_${zOS}_$1 -device virtio-blk-pci,drive=vmDisk_${zOS}_$1 \
	-drive file=../ISOs/$zISO,readonly=on,media=cdrom \
	-boot order=cd \
	-name vm${zOS}_$1 \
	-monitor tcp:$zHostIP:$(($1 * 1000)),server,nowait \
	-vnc :$2 \
	-daemonize
}

zops_func() {
	eval zMark=$(($1 + $zAddrPos))
	zImgPath=/tmp/images/${zOS}_${zMark}.img
	local zBaseImgName=${zOS}_base.img
	local zVncID=$zMark

	if [[ 0 -eq $(ls ../images | grep -c $zBaseImgName) ]];then
		zImgPath=../images/$zBaseImgName
		zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on $zImgPath'
	else
		mkdir -p /tmp/images
		zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on,backing_file=`dirname $PWD`/images/$zBaseImg $zImgPath'
	fi

	if [[ 0 -eq $(ls ../images | grep -c $zImgPath) ]];then
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
	rm /tmp/images/CentOS_$(($i + $zAddrPos)).img
	zops_func $i
done
