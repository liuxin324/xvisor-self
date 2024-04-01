# BusyBox RootFS 用于 ARM/ARM64 客户端

## 引言
[BusyBox](https://busybox.net) 提供了多个精简版的 Unix 工具集于一个可执行文件中。
它能够在多种 POSIX 环境中运行，例如 Linux、Android、FreeBSD 和其他环境，
如专有内核，尽管它提供的许多工具都是为了与 Linux 内核提供的接口一起工作而设计的。
它是为了资源非常有限的嵌入式操作系统而创建的。
BusyBox 根据 GNU 通用公共许可协议的条款发布。
更多信息请阅读
[BusyBox page on Wikipedia](http://en.wikipedia.org/wiki/BusyBox).


## 工具链
用于编译 BusyBox rootfs 的工具链必须支持 C 库。
目前，我们有两个可选项来自免费可用的工具链：
1. [CodeSourcery Lite ARM GNU/Linux Toolchain](http://www.mentor.com/embedded-software/sourcery-tools/sourcery-codebench/editions/lite-edition/)
    是针对 ARMv5te 或更高处理器的软浮点工具链。

交叉编译前缀为 `arm-none-linux-gnueabi- `
它使用默认选项构建：`-mfloat-abi=soft` 和 `-march=armv5te`

2. [ARM Ltd GNU/Linux Toolchains](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-a/downloads)
    - **软浮点工具链** 用于 ARMv7 处理器的软件浮点.

        交叉编译前缀为 ` arm-none-linux-gnueabi-`
        它使用默认选项构建：`-mfloat-abi=soft、`
        `-mfpu-name=vfpv3-d16` and `-march=armv7 `

    - **硬浮点工具链** 用于 ARMv7 处理器的硬件浮点.

        交叉编译前缀为 `arm-none-linux-gnueabihf-`.
        它使用默认选项构建: `-mfloat-abi=hard`,
        `-mfpu-name=vfpv3-d16` and `-march=armv7`.

通常建议使用软浮点和 armv5te 工具链构建 BusyBox，
以便所产生的根文件系统适用于 ARMv5、ARMv6 和 ARMv7 客户端。

在现实场景中，我们需要适用于 ARMv5 或更高处理器的硬浮点工具链，
但这样的工具链不是免费可用的，我们需要使用交叉工具脚本手动构建。

如果你只关心 ARMv7 和 ARMv8 处理器，应该使用 ARM Ltd 工具链。


## RAMDISK 生成

请按照以下步骤使用 BusyBox 构建 RAMDISK 作为 ARM Linux 客户端的 RootFS（根据你的工作区替换所有 <> 括号）：

1. 根据所选工具链为 Xvisor 设置构建环境

    - ARM64 GNU/Linux 工具链

        ```bash
        export CROSS_COMPILE=aarch64-none-linux-gnu-
        --export CROSS_COMPILE=/usr/local/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- 
        ```

    - CodeSourcery Lite ARM GNU/Linux 工具链

        ```bash
        export CROSS_COMPILE=arm-none-linux-gnueabi-
        ```

    - ARM Ltd GNU/Linux 软浮点工具链

        ```bash
        export CROSS_COMPILE=arm-none-linux-gnueabi-
        ```

    - ARM Ltd GNU/Linux 硬浮点工具链

        ```bash
        export CROSS_COMPILE=arm-none-linux-gnueabihf-
        ```

2. 进入 Xvisor 源代码目录

    ```bash
    cd <xvisor_source_directory>
    ```

3. 将 `defconfig` 文件复制到 Busybox 源代码目录

    ```bash
    cp tests/common/busybox/busybox-<busybox_version>_defconfig <busybox_source_directory>/.config
    --cp tests/common/busybox/busybox-1.33.1_defconfig /home/liu/Work/hypervisor/xvisor-guest/busybox-1_33_1/.config
    ```

4. 进入 Busybox 源代码目录

    ```bash
    cd <busybox_source_directory>
    ```

5. 配置 Busybox 源代码

    ```bash
    make oldconfig
    ```

6. 在 `_install` 下构建 Busybox RootFS

    ```bash
    make install
    ```

7. 填充 Busybox RootFS

    ```bash
    mkdir -p ./_install/etc/init.d
    mkdir -p ./_install/dev
    mkdir -p ./_install/proc
    mkdir -p ./_install/sys
    ln -sf /sbin/init ./_install/init
    cp -f <xvisor_source_directory>/tests/common/busybox/fstab ./_install/etc/fstab
    cp -f <xvisor_source_directory>/tests/common/busybox/rcS ./_install/etc/init.d/rcS
    cp -f <xvisor_source_directory>/tests/common/busybox/motd ./_install/etc/motd
    cp -f <xvisor_source_directory>/tests/common/busybox/logo_linux_clut224.ppm ./_install/etc/logo_linux_clut224.ppm
    cp -f <xvisor_source_directory>/tests/common/busybox/logo_linux_vga16.ppm ./_install/etc/logo_linux_vga16.ppm
    ```

    ---
    cp -f /home/liu/Work/hypervisor/xvisor-self/tests/common/busybox/fstab ./_install/etc/fstab
    cp -f /home/liu/Work/hypervisor/xvisor-self/tests/common/busybox/rcS ./_install/etc/init.d/rcS
    cp -f /home/liu/Work/hypervisor/xvisor-self/tests/common/busybox/motd ./_install/etc/motd
    cp -f /home/liu/Work/hypervisor/xvisor-self/tests/common/busybox/logo_linux_clut224.ppm ./_install/etc/logo_linux_clut224.ppm
    cp -f /home/liu/Work/hypervisor/xvisor-self/tests/common/busybox/logo_linux_vga16.ppm ./_install/etc/logo_linux_vga16.ppm
    ---
8. 使用以下选项之一创建 RootFS 镜像（首选 INITRAMFS）

    - INITRAMFS cpio image

        ```bash
        cd ./_install; find ./ | cpio -o -H newc > ../rootfs.img; cd -
        ```

    - OR, INITRAMFS compressed cpio image

        ```bash
        cd ./_install; find ./ | cpio -o -H newc | gzip -9 > ../rootfs.img; cd -
        ```

    - OR, INITRD etx2 image (legacy)

        ```bash
        genext2fs -b 6500 -N 1024 -U -d ./_install ./rootfs.ext2
        ```

