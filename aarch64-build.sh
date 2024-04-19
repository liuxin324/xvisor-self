#!/bin/bash
make ARCH=arm generic-v8-defconfig
make
mkimage -A arm64 -O linux -T kernel -C none -a 0x00080000 -e 0x00080000 -n Xvisor -d build/vmm.bin build/uvmm.bin
make -C tests/arm64/virt-v8/basic
# 创建必要的目录结构
mkdir -p ./build/disk/tmp
mkdir -p ./build/disk/system
mkdir -p ./build/disk/images/arm64/virt-v8

# 复制banner和logo文件到system目录
cp -f ./docs/banner/roman.txt ./build/disk/system/banner.txt
cp -f ./docs/logo/xvisor_logo_name.ppm ./build/disk/system/logo.ppm

# 使用Device Tree Compiler生成设备树二进制文件
dtc -q -I dts -O dtb -o ./build/disk/images/arm64/virt-v8-guest.dtb ./tests/arm64/virt-v8/virt-v8-guest.dts
dtc -q -I dts -O dtb -o ./build/disk/images/arm64/virt-v8/virt-v8.dtb ./tests/arm64/virt-v8/linux/virt-v8.dts

# 复制固件、闪存列表、命令列表和脚本文件到images目录
cp -f ./build/tests/arm64/virt-v8/basic/firmware.bin ./build/disk/images/arm64/virt-v8/firmware.bin
cp -f ./tests/arm64/virt-v8/linux/nor_flash.list ./build/disk/images/arm64/virt-v8/nor_flash.list
cp -f ./tests/arm64/virt-v8/linux/cmdlist ./build/disk/images/arm64/virt-v8/cmdlist
cp -f ./tests/arm64/virt-v8/xscript/one_novgic_guest_virt-v8.xscript ./build/disk/boot.xscript

# 复制Linux内核镜像和根文件系统镜像到images目录
cp -f /home/liu/Work/hypervisor/xvisor-guest/linux-rpi-5.17.y/build/arch/arm64/boot/Image ./build/disk/images/arm64/virt-v8/Image
cp -f /home/liu/Work/hypervisor/xvisor-guest/busybox-1_33_1/rootfs.img ./build/disk/images/arm64/virt-v8/rootfs.img

# 生成磁盘镜像文件
genext2fs -B 1024 -b 32768 -d ./build/disk ./build/disk.img
mkimage -A arm64 -O linux -T ramdisk -a 0x00000000 -n "Xvisor Ramdisk" -d build/disk.img build/udisk.img