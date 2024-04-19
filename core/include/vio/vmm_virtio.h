/**
 * Copyright (c) 2013 Pranav Sawargaonkar.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file vmm_virtio.h
 * @author Pranav Sawargaonkar (pranav.sawargaonkar@gmail.com)
 * @brief VirtIO Core Framework Interface
 */
#ifndef __VMM_VIRTIO_H__
#define __VMM_VIRTIO_H__

#include <vmm_types.h>
#include <vio/vmm_virtio_config.h>
#include <vio/vmm_virtio_ids.h>
#include <vio/vmm_virtio_ring.h>
#include <libs/list.h>

/** VirtIO module intialization priority */
#define VMM_VIRTIO_IPRIORITY			1
/* VirtIO设备名称的最大长度为64 */
#define VMM_VIRTIO_DEVICE_MAX_NAME_LEN		64
/* VirtIO中断请求（IRQ）的优先级。VMM_VIRTIO_IRQ_LOW为0，表示低优先级中断；VMM_VIRTIO_IRQ_HIGH为1，表示高优先级中断 */
#define VMM_VIRTIO_IRQ_LOW			0
#define VMM_VIRTIO_IRQ_HIGH			1

struct vmm_guest;
struct vmm_virtio_device;

/*表示一个I/O向量（即一段连续的内存区域），用于VirtIO设备的数据传输*/
struct vmm_virtio_iovec {
	/* Address (guest-physical). */
	u64 addr;
	/* Length. */
	u32 len;
	/* The flags as indicated above. */
	u16 flags;
};

/* VirtIO的一个队列 */
struct vmm_virtio_queue {
	/* The last_avail_idx field is an index to ->ring of struct vring_avail.
	   It's where we assume the next request index is at.  */
	/* Last_avail_idx 字段是 struct vring_avail 的 ->ring 的索引。这是我们假设下一个请求索引所在的位置。 */
	/* 这两个字段用于跟踪队列中的可用和已用描述符的索引 */
	u16			last_avail_idx;
	u16			last_used_signalled;
	/* 表示此队列的环形缓冲区。环形缓冲区是VirtIO队列的核心数据结构，用于存放数据传输请求和响应 */
	struct vmm_vring	vring;
	/*指向此队列所属的客户（虚拟机）的指针*/
	struct vmm_guest	*guest;
	/*描述符的数量，即环形缓冲区中可以存放多少个请求或响应*/
	u32			desc_count;
	/*队列对齐要求，用于确保环形缓冲区的性能最优化*/
	u32			align;
	/*队列在客户物理内存中的页帧号*/
	physical_addr_t		guest_pfn;
	/*客户（虚拟机）的页大小*/
	physical_size_t		guest_page_size;
	/* 队列在客户物理地址空间起始地址*/
	physical_addr_t		guest_addr;
	/*队列在宿主物理地址空间中的起始地址*/
	physical_addr_t		host_addr;
	/*队列所需的总物理空间*/
	physical_size_t		total_size;
};

struct vmm_virtio_device_id {
	u32 type; //设备类型的唯一标识符，用于区分不同的VirtIO设备，例如网络设备、块设备等
};

struct vmm_virtio_device {
	char name[VMM_VIRTIO_DEVICE_MAX_NAME_LEN]; //设备名称
	struct vmm_emudev *edev; // 指向虚拟机中模拟的设备的指针

	struct vmm_virtio_device_id id;// 设备的ID，包含设备类型

	struct vmm_virtio_transport *tra;// 与设备关联的传输机制的指针，定义了如何通知设备虚拟队列的变化
	void *tra_data; // 传输机制相关的数据

	struct vmm_virtio_emulator *emu;//  指向与设备关联的仿真器的指针，定义了设备的行为和操作
	void *emu_data; // 仿真器相关的数据

	struct dlist node; // 用于将设备插入到链表中的节点
	struct vmm_guest *guest; // 指向设备所属的客户（虚拟机）的指针
};

struct vmm_virtio_transport {
	const char *name; // 传输机制的名称

	int  (*notify)(struct vmm_virtio_device *, u32 vq);// 一个函数指针，当虚拟队列有更新时调用，通知设备处理新的请求
};

struct vmm_virtio_emulator {
	const char *name; // 仿真器的名称
	const struct vmm_virtio_device_id *id_table; // 支持的设备类型ID数组的指针

	/* VirtIO operations */
	// 获取和设置设备特性
	u64 (*get_host_features) (struct vmm_virtio_device *dev); 
	void (*set_guest_features) (struct vmm_virtio_device *dev,
				    u32 select, u32 features);
	// 初始化一个虚拟队列				
	int (*init_vq) (struct vmm_virtio_device *dev, u32 vq, u32 page_size,
			u32 align, u32 pfn);
	//获取和设置虚拟队列的页面帧号和大小
	int (*get_pfn_vq) (struct vmm_virtio_device *dev, u32 vq);
	int (*get_size_vq) (struct vmm_virtio_device *dev, u32 vq);
	int (*set_size_vq) (struct vmm_virtio_device *dev, u32 vq, int size);
	//通知设备有队列更新
	int (*notify_vq) (struct vmm_virtio_device *dev, u32 vq);
	// 设备状态变化时调用
	void (*status_changed) (struct vmm_virtio_device *dev,
				u32 new_status);

	/* Emulator operations */
	// 读写设备配置
	int (*read_config)(struct vmm_virtio_device *dev,
			   u32 offset, void *dst, u32 dst_len);
	int (*write_config)(struct vmm_virtio_device *dev,
			    u32 offset, void *src, u32 src_len);
	//重置设备
	int (*reset)(struct vmm_virtio_device *dev);
	// 连接或断开设备与其仿真器
	int  (*connect)(struct vmm_virtio_device *dev,
			struct vmm_virtio_emulator *emu);
	void (*disconnect)(struct vmm_virtio_device *dev);
	// 用于将仿真器插入到链表中的节点
	struct dlist node;
};

/** Get guest to which the queue belongs
 *  Note: only available after queue setup is done
 */
/*获取与虚拟队列关联的虚拟机实例*/
struct vmm_guest *vmm_virtio_queue_guest(struct vmm_virtio_queue *vq);

/** Get maximum number of descriptors in queue
 *  Note: only available after queue setup is done
 */
/*获取队列中描述符的最大数量*/
u32 vmm_virtio_queue_desc_count(struct vmm_virtio_queue *vq);

/** Get queue alignment
 *  Note: only available after queue setup is done
 */
/*获取队列的对齐要求*/
u32 vmm_virtio_queue_align(struct vmm_virtio_queue *vq);

/** Get guest page frame number of queue
 *  Note: only available after queue setup is done
 */
/*获取队列的页面帧号*/
physical_addr_t vmm_virtio_queue_guest_pfn(struct vmm_virtio_queue *vq);

/** Get guest page size for this queue
 *  Note: only available after queue setup is done
 */
/*获取为该队列设定的页面大小*/
physical_size_t vmm_virtio_queue_guest_page_size(struct vmm_virtio_queue *vq);

/** Get guest physical address of this queue
 *  Note: only available after queue setup is done
 */
/*获取队列在虚拟机中的物理地址*/
physical_addr_t vmm_virtio_queue_guest_addr(struct vmm_virtio_queue *vq);

/** Get host physical address of this queue
 *  Note: only available after queue setup is done
 */
/*获取队列在宿主机中的物理地址*/
physical_addr_t vmm_virtio_queue_host_addr(struct vmm_virtio_queue *vq);

/** Get total physical space required by this queue
 *  Note: only available after queue setup is done
 */
/*获取队列所需的总物理空间*/
physical_size_t virtio_queue_total_size(struct vmm_virtio_queue *vq);

/** Retrive maximum number of vring descriptors
 *  Note: works only after queue setup is done
 */
/*检索队列中vring描述符的最大数量*/
u32 vmm_virtio_queue_max_desc(struct vmm_virtio_queue *vq);

/** Retrive vring descriptor at given index
 *  Note: works only after queue setup is done
 */
/*检索给定索引处的vring描述符*/
int vmm_virtio_queue_get_desc(struct vmm_virtio_queue *vq, u16 indx,
			      struct vmm_vring_desc *desc);

/** Pop the index of next available descriptor
 *  Note: works only after queue setup is done
 */
/*弹出下一个可用的描述符索引*/
u16 vmm_virtio_queue_pop(struct vmm_virtio_queue *vq);

/** Check whether any descriptor is available or not
 *  Note: works only after queue setup is done
 */
/*检查是否有可用的描述符*/
bool vmm_virtio_queue_available(struct vmm_virtio_queue *vq);

/** Check whether queue notification is required
 *  Note: works only after queue setup is done
 */
/*检检查是否需要通知设备队列有更新*/
bool vmm_virtio_queue_should_signal(struct vmm_virtio_queue *vq);

/** Update avail_event in vring
 *  Note: works only after queue setup is done
 */
/*更新vring的可用事件*/
void vmm_virtio_queue_set_avail_event(struct vmm_virtio_queue *vq);

/** Update used element in vring
 *  Note: works only after queue setup is done
 */
/*更新vring的的已使用元素*/
void vmm_virtio_queue_set_used_elem(struct vmm_virtio_queue *vq,
				    u32 head, u32 len);

/** Check whether queue setup is done by guest or not */
/*检查队列设置是否完成*/
bool vmm_virtio_queue_setup_done(struct vmm_virtio_queue *vq);

/** Cleanup or reset the queue
 *  Note: After cleanup we need to setup queue before reusing it.
 */
/*清理或重置队列。在队列不再需要时释放资源*/
int vmm_virtio_queue_cleanup(struct vmm_virtio_queue *vq);

/** Setup or initialize the queue
 *  Note: If queue was already setup then it will cleanup first.
 */
/*设置或初始化队列*/
int vmm_virtio_queue_setup(struct vmm_virtio_queue *vq,
			   struct vmm_guest *guest,
			   physical_addr_t guest_pfn,
			   physical_size_t guest_page_size,
			   u32 desc_count, u32 align);

/** Get guest IO vectors based on given head
 *  Note: works only after queue setup is done
 */
// 获取给定头部的guest IO向量
int vmm_virtio_queue_get_head_iovec(struct vmm_virtio_queue *vq,
				    u16 head, struct vmm_virtio_iovec *iov,
				    u32 *ret_iov_cnt, u32 *ret_total_len,
				    u16 *ret_head);

/** Get guest IO vectors based on current head
 *  Note: works only after queue setup is done
 */
// 获取当前头部的guest IO向量
int vmm_virtio_queue_get_iovec(struct vmm_virtio_queue *vq,
			       struct vmm_virtio_iovec *iov,
			       u32 *ret_iov_cnt, u32 *ret_total_len,
			       u16 *ret_head);

/** Read contents from guest IO vectors to a buffer */
// 从guest IO向量中读取内容到缓冲区
u32 vmm_virtio_iovec_to_buf_read(struct vmm_virtio_device *dev,
				 struct vmm_virtio_iovec *iov,
				 u32 iov_cnt, void *buf,
				 u32 buf_len);

/** Write contents to guest IO vectors from a buffer */
// 从缓冲区中向guest IO向量写入内容
u32 vmm_virtio_buf_to_iovec_write(struct vmm_virtio_device *dev,
				  struct vmm_virtio_iovec *iov,
				  u32 iov_cnt, void *buf,
				  u32 buf_len);

/** Fill guest IO vectors with zeros */
// 用零填充guest IO向量
void vmm_virtio_iovec_fill_zeros(struct vmm_virtio_device *dev,
				 struct vmm_virtio_iovec *iov,
				 u32 iov_cnt);

/** Read VirtIO device configuration */
// 读取和写入VirtIO设备配置
int vmm_virtio_config_read(struct vmm_virtio_device *dev,
			   u32 offset, void *dst, u32 dst_len);

/** Write VirtIO device configuration */
// 写入VirtIO设备配置
int vmm_virtio_config_write(struct vmm_virtio_device *dev,
			    u32 offset, void *src, u32 src_len);

/** Reset VirtIO device */
//重置VirtIO设备
int vmm_virtio_reset(struct vmm_virtio_device *dev);

/** Register VirtIO device */
//注册VirtIO设备
int vmm_virtio_register_device(struct vmm_virtio_device *dev);

/** UnRegister VirtIO device */
//注销VirtIO设备
void vmm_virtio_unregister_device(struct vmm_virtio_device *dev);

/** Register VirtIO device emulator */
//注册VirtIO设备仿真器
int vmm_virtio_register_emulator(struct vmm_virtio_emulator *emu);

/** UnRegister VirtIO device emulator */
//注销VirtIO设备仿真器
void vmm_virtio_unregister_emulator(struct vmm_virtio_emulator *emu);

#endif /* __VMM_VIRTIO_H__ */
