[![构建状态](https://travis-ci.com/avpatel/xvisor-next.svg?branch=master)](https://travis-ci.com/avpatel/xvisor-next)

# Xvisor

http://xvisor.org

http://xhypervisor.org

请仔细阅读本文档，它将告诉你这是关于什么的，解释如何构建和使用虚拟机监控器，以及当出现问题时该怎么办.

## 什么是Xvisor?
Xvisor一词代表：可扩展的多功能虚拟机监控器（eXtensible versatile hypervisor）.

Xvisor是一个开源的一型虚拟机监控器，旨在提供一个单体的、轻量级的、可移植的、灵活的虚拟化解决方案.

它为ARMv7a-ve、ARMv8a、x86_64、RISC-V等CPU架构提供高性能和低内存占用的虚拟化解决方案.

Xvisor主要支持全虚拟化，因此，支持广泛的未修改的客户操作系统.对Xvisor而言，半虚拟化是可选的，并将以体系结构独立的方式（如VirtIO PCI/MMIO设备）支持，以确保客户操作系统使用半虚拟化时不需修改.

它具备现代虚拟机监控器所期望的大多数特性，如：

- 基于设备树的配置，
- 无节拍和高分辨率的时间保持，
- 线程框架，
- 宿主设备驱动程序框架，
- IO设备仿真框架，
- 运行时可加载模块，
- Pass-through硬件访问，
- 动态客户创建/销毁，
- 管理终端，
- 网络虚拟化，
- 输入设备虚拟化，
- 显示设备虚拟化，
- 以及更多.

它是在下分发的 [GNU通用公共许可证](http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt)下分发的.
更多细节请参见随附的[COPYING](COPYING) 文件.

## 它能在什么硬件上运行？

Xvisor源代码具有高度的可移植性，只要具有分页内存管理单元（PMMU）和GNU C编译器（GCC）的移植，就可以轻松地移植到大多数通用的32位或64位架构上.

请参考源代码顶层目录中的HOSTS文本文件，以获取支持的宿主硬件的详细和格式化列表.

## 文档

对于Xvisor，我们更倾向于源代码级的文档，因此在可能的情况下，我们直接在源代码中描述事物.
这帮助我们在同一地点维护源代码及其文档.

对于源代码级文档，我们严格遵循Doxygen风格.

有关详细信息，请参考[Doxygen manual](http://www.stack.nl/~dimitri/doxygen/manual.html).

此外，我们在`docs`子目录中还有多个`README`文件.
请参考[docs/00-INDEX](docs/00-INDEX) 以获取每个文件或子目录包含内容的列表.

## 输出目录

当编译/配置虚拟机监控器时，默认情况下所有输出文件将存储在虚拟机监控器源目录中名为`build`的目录中.

使用`make O=<output_dir>`选项可以指定输出文件（包括`.config`）的另一个存储位置.

##### 注意

如果要使用`O=<output_dir>`选项，则必须在所有`make`调用中使用此选项.

## 配置

即使你只是升级一个小版本，也不要跳过这一步.

每个发布版都会添加新的配置选项，如果配置文件未按预期设置，就会出现奇怪的问题.

如果你想将现有配置带到新版本且工作量最小，使用`make oldconfig`命令，它只会询问你关于新配置符号的问题.

要配置虚拟机监控器，请使用以下命令之一：

	make <configuration_command>

or

	make O=<output_dir> <configuration_command>

各种配置命令(`<configuration_command>`)包括：

- `config` - 纯文本界面.
- `menuconfig` - 基于文本的彩色菜单、单选列表和对话框.
- `oldconfig` - 基于你现有的`./.config`文件内容默认所有问题，并询问新的配置符号.
- `<xyz>_defconfig` - 使用`arch/$ARCH/configs/<xyz>_defconfig`中的默认值创建一个`./.config`文件.


对于配置，Xvisor使用Openconf，这是Linux Kconfig的修改版.Openconf的动机是从环境变量中获取Xvisor特定信息，并在配置时检查依赖库和工具的语法扩展.

更多信息请参考[Openconf Syntax](tools/openconf/openconf_syntax.txt).语法.

## 编译

确保你至少有gcc 4.x可用.

要编译虚拟机监控器，请使用以下命令之一：

	make

or

	make O=<output_dir>

### 详细的编译/构建输出

通常，虚拟机监控器的构建系统运行在相对安静的模式下（但不是完全无声）.然而，有时你或其他虚拟机监控器开发者需要精确地看到编译、链接或其他命令的执行.对此，使用`verbose`构建模式，通过在`make`命令中插入`VERBOSE=y`

## 测试

配置和/或编译的上述步骤是任何架构的通用步骤，但这对运行虚拟机监控器来说还不够.我们还需要配置/编译/运行虚拟机环境中的客户操作系统的指南.有些客户操作系统甚至可能在编译时期望特定类型的虚拟机监控器配置.有时我们可能还需要为虚拟机环境下的正常运作对客户操作系统进行补丁.

在虚拟机监控器中运行特定类型的操作系统（客户CPU + 客户板）所需的指南可以在以下目录下找到：

	tests/<Guest CPU>/<Guest Board>/README

请参考此README，以获取在虚拟机监控器中运行特定类型操作系统的详细信息.

---

And finally remember

>  It's all JUST FOR FUN....

	.:: HAPPY HACKING ::.
