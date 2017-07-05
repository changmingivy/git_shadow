#!/usr/bin/env sh
# 0、目录结构:conf、images、ISOs
# 1、安装镜像存放于"../ISOs"路径下，命名格式：FreeBSD.iso
# 2、配置好每个OS的基础镜像，置于"../images"路径下，命名格式:FreeBSD_base.img

zISO="${zOS}.iso"
zBaseImg="${zOS}_base.img"
zCpuNum="1"
zMem="1G"
zMaxMem="2G"
zDiskSiz="20G"
zHostNatIf="wlp1s0"
zHostIp="10.10.40.40"  #0x.....

if [[ 0 -eq  $(sysctl net.inet.ip.forwarding)]]; then
	sysctl net.inet.ip.forwarding=1
	kldload dummynet

	ipfw "-q -f flush"

	ipfw "add pipe 10 ip from 172.16.10.1 to 172.16.0.0/16 in"
	ipfw "pipe 10 config delay 200ms bw 300Kbit/s"
	ipfw "add pipe 10 ip from 172.16.10.1 to 172.16.0.0/16 out"
	ipfw "pipe 20 config delay 300ms bw 200Kbit/s"

	ipfw "add nat 20 ip from 172.16.0.0/16 to any"
	ipfw "nat 10 config ip $zHostIp"

	ipfw "add pass any to any"
fi

if [[ $1 == '' ]];then
	zVmNum=1
	printf "\033[31;01m\$VmNum is not specified, defaults to 1\n\033[00m"
elif [[ $1 =~ [0-9][0-9][0-9] ]];then
	zVmNum=$1
	if [[ 255 -lt $1 ]]; then
		printf "\033[31;01m\Too many VM!(should less than 256)\n\033[00m\n"
		exit 1
	fi
else
	printf "\033[31;01m\$1 is not a number!!!\n\033[00m"
	exit 1
fi

if [[ $2 == '' ]];then
	zOS=CentOS
	printf "\033[31;01mOS is not specified, defaults to \"CentOS-6.9\"\n\033[00m"
else
	zOS=$2
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

zvm_func() {
	qemu-system-x86_64 \
	-cpu host -smp $zCpuNum,sockets=$zCpuNum,cores=1,threads=1 \
	-m $zMem,slots=4,maxmem=$zMaxMem \
	-netdev tap,ifname=${zOS}_$1,script=tap.sh,downscript=no,vhost=on,id=vmNic_${zOS}_$1 -device virtio-net-pci,mac=00:e0:4c:49:$1:${zVersion},netdev=vmNic_${zOS}_$1 \
	-drive file=../images/$zImgName,if=none,cache=writeback,media=disk,id=vmDisk_${zOS}_$1 -device virtio-blk-pci,drive=vmDisk_${zOS}_$1 \
	-drive file=../ISOs/$zISO,readonly=on,media=cdrom \
	-boot order=cd \
	-name vm${zOS}_$1 \
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
		zCommand='qemu-img create -f qcow2 -o size=$zDiskSiz,nocow=on,backing_file=../images/$zBaseImg ../images/$zImgName'
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
