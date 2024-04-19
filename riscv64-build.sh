CROSS_COMPILE=/opt/riscv64/bin/riscv64-unknown-linux-gnu-
make ARCH=riscv generic-64b-defconfig
make
make -C tests/riscv/virt64/basic
mkdir -p ./build/disk/tmp
mkdir -p ./build/disk/system
cp -f ./docs/banner/roman.txt ./build/disk/system/banner.txt
cp -f ./docs/logo/xvisor_logo_name.ppm ./build/disk/system/logo.ppm
mkdir -p ./build/disk/images/riscv/virt64
dtc -q -I dts -O dtb -o ./build/disk/images/riscv/virt64-guest.dtb ./tests/riscv/virt64/virt64-guest.dts
cp -f ./build/tests/riscv/virt64/basic/firmware.bin ./build/disk/images/riscv/virt64/firmware.bin
cp -f ./tests/riscv/virt64/linux/nor_flash.list ./build/disk/images/riscv/virt64/nor_flash.list
cp -f ./tests/riscv/virt64/linux/cmdlist ./build/disk/images/riscv/virt64/cmdlist
cp -f ./tests/riscv/virt64/xscript/one_guest_virt64.xscript ./build/disk/boot.xscript
cp -f /home/liu/Work/hypervisor/xvisor-guest/riscv/linux/build/arch/riscv/boot/Image ./build/disk/images/riscv/virt64/Image
dtc -q -I dts -O dtb -o ./build/disk/images/riscv/virt64/virt64.dtb ./tests/riscv/virt64/linux/virt64.dts
cp -f /home/liu/Work/hypervisor/xvisor-guest/riscv/busybox-1_33_1/rootfs.img ./build/disk/images/riscv/virt64/rootfs.img
genext2fs -B 1024 -b 32768 -d ./build/disk ./build/disk.img


qemu-system-riscv64 -M virt -m 512M -nographic -bios /home/liu/Work/hypervisor/xvisor-guest/riscv/opensbi/build/platform/generic/firmware/fw_jump.bin -kernel ./build/vmm.bin -initrd ./build/disk.img -append 'vmm.bootcmd="vfs mount initrd /;vfs run /boot.xscript;vfs cat /system/banner.txt"'