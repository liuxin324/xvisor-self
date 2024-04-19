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
 * @file vmm_virtio.c
 * @author Pranav Sawargaonkar (pranav.sawargaonkar@gmail.com)
 * @brief VirtIO Para-virtualization Framework Implementation
 */

#include <vmm_error.h>
#include <vmm_macros.h>
#include <vmm_heap.h>
#include <vmm_mutex.h>
#include <vmm_stdio.h>
#include <vmm_host_io.h>
#include <vmm_guest_aspace.h>
#include <vmm_modules.h>
#include <vio/vmm_virtio.h>
#include <libs/mathlib.h>
#include <libs/stringlib.h>

#define MODULE_DESC		"VirtIO Para-virtualization Framework"
#define MODULE_AUTHOR		"Pranav Sawargaonkar"
#define MODULE_LICENSE		"GPL"
#define MODULE_IPRIORITY	VMM_VIRTIO_IPRIORITY
#define	MODULE_INIT		vmm_virtio_core_init
#define	MODULE_EXIT		vmm_virtio_core_exit

/*
 * virtio_mutex protects entire virtio subsystem and is taken every time
 * virtio device or emulator is registered or unregistered.
 */

static DEFINE_MUTEX(virtio_mutex); //初始化一个互斥锁-保护整个VirtIO子系统

static LIST_HEAD(virtio_dev_list);//初始化一个链表-存储所有已注册的VirtIO设备

static LIST_HEAD(virtio_emu_list);//初始化一个链表-存储所有已注册的VirtIO设备仿真器

/* ========== VirtIO queue implementations ========== */

struct vmm_guest *vmm_virtio_queue_guest(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->guest : NULL;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_guest);

u32 vmm_virtio_queue_desc_count(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->desc_count : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_desc_count);

u32 vmm_virtio_queue_align(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->align : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_align);

physical_addr_t vmm_virtio_queue_guest_pfn(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->guest_pfn : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_guest_pfn);

physical_size_t vmm_virtio_queue_guest_page_size(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->guest_page_size : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_guest_page_size);

physical_addr_t vmm_virtio_queue_guest_addr(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->guest_addr : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_guest_addr);

physical_addr_t vmm_virtio_queue_host_addr(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->host_addr : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_host_addr);

physical_size_t vmm_virtio_queue_total_size(struct vmm_virtio_queue *vq)
{
	return (vq) ? vq->total_size : 0;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_total_size);

u32 vmm_virtio_queue_max_desc(struct vmm_virtio_queue *vq)
{
	if (!vq || !vq->guest) {
		return 0;
	}

	return vq->desc_count;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_max_desc);
/**
 * @description: 获取VirtIO队列中特定索引处的描述符
 * @param {vmm_virtio_queue} *vq 指向VirtIO队列的指针
 * @param {u16} indx 要获取的描述符的索引
 * @param {vmm_vring_desc} *desc 指向vmm_vring_desc结构的指针，用于接收描述符的内容
 * @return {*} 如果操作成功，返回VMM_OK；否则返回相应的错误码。
 */
int vmm_virtio_queue_get_desc(struct vmm_virtio_queue *vq, u16 indx,struct vmm_vring_desc *desc)
{
	u32 ret;
	physical_addr_t desc_pa;
	//判断非空
	if (!vq || !vq->guest || !desc) {
		return VMM_EINVALID;
	}
	// 计算描述符在物理内存中的地址
	desc_pa = vq->vring.desc_pa + indx * sizeof(*desc);
	/*从客户机的物理地址空间读取描述符内容到desc指向的结构中*/
	ret = vmm_guest_memory_read(vq->guest, desc_pa,
				    desc, sizeof(*desc), TRUE);
	if (ret != sizeof(*desc)) {
		return VMM_EIO;
	}

	return VMM_OK;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_get_desc);
/**
 * @description: 弹出VirtIO队列中下一个可用的描述符的索引
 * @param {vmm_virtio_queue} *vq 指向VirtIO队列的指针
 * @return {*}
 */
u16 vmm_virtio_queue_pop(struct vmm_virtio_queue *vq)
{
	u16 val;
	u32 ret;
	physical_addr_t avail_pa;

	if (!vq || !vq->guest) {
		return 0;
	}
	//使用umod32函数计算下一个可用描述符的索引。这是通过对last_avail_idx进行自增并取模desc_count实现的，确保索引值在有效范围内
	ret = umod32(vq->last_avail_idx++, vq->desc_count);
	// 计算avail数组中对应索引的物理地址，这是队列中存储下一个可用描述符索引的地方
	avail_pa = vq->vring.avail_pa +
		   offsetof(struct vmm_vring_avail, ring[ret]);
	//从客户机的物理内存中读取avail数组中存储的描述符索引值
	ret = vmm_guest_memory_read(vq->guest, avail_pa,
				    &val, sizeof(val), TRUE);
	if (ret != sizeof(val)) {
		vmm_printf("%s: read failed at avail_pa=0x%"PRIPADDR"\n",
			   __func__, avail_pa);
		return 0;
	}

	return val;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_pop);
/**
 * @description: 检查VirtIO队列中是否有新的描述符可供设备处理
 * @param {vmm_virtio_queue} *vq
 * @return {*}
 */
bool vmm_virtio_queue_available(struct vmm_virtio_queue *vq)
{
	u16 val;
	u32 ret;
	physical_addr_t avail_pa;

	if (!vq || !vq->guest) {
		return FALSE;
	}
	/*计算avail结构中idx字段的物理地址，并从客户机的内存中读取此值*/
	avail_pa = vq->vring.avail_pa +
		   offsetof(struct vmm_vring_avail, idx);
	ret = vmm_guest_memory_read(vq->guest, avail_pa,
				    &val, sizeof(val), TRUE);
	/*比较读取到的idx值与队列中记录的last_avail_idx。如果这两个值不相等，说明队列中有新的描述符可供处理*/
	if (ret != sizeof(val)) {
		vmm_printf("%s: read failed at avail_pa=0x%"PRIPADDR"\n",
			   __func__, avail_pa);
		return FALSE;
	}

	return val != vq->last_avail_idx;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_available);
/**
 * @description: 判断是否需要向设备发送中断信号，以通知设备处理队列中的描述符
 * @param {vmm_virtio_queue} *vq
 * @return {*}
 */
bool vmm_virtio_queue_should_signal(struct vmm_virtio_queue *vq)
{
	u32 ret;
	u16 old_idx, new_idx, event_idx;
	physical_addr_t used_pa, avail_pa;

	if (!vq || !vq->guest) {
		return FALSE;
	}
	/*读取used结构中的idx字段（即新的索引值）和avail结构中的事件索引（event_idx）*/
	old_idx = vq->last_used_signalled;

	used_pa = vq->vring.used_pa +
		  offsetof(struct vmm_vring_used, idx);
	ret = vmm_guest_memory_read(vq->guest, used_pa,
				    &new_idx, sizeof(new_idx), TRUE);
	if (ret != sizeof(new_idx)) {
		vmm_printf("%s: read failed at used_pa=0x%"PRIPADDR"\n",
			   __func__, used_pa);
		return FALSE;
	}

	avail_pa = vq->vring.avail_pa +
		   offsetof(struct vmm_vring_avail, ring[vq->vring.num]);
	ret = vmm_guest_memory_read(vq->guest, avail_pa,
				    &event_idx, sizeof(event_idx), TRUE);
	if (ret != sizeof(event_idx)) {
		vmm_printf("%s: read failed at avail_pa=0x%"PRIPADDR"\n",
			   __func__, avail_pa);
		return FALSE;
	}
	/*使用vmm_vring_need_event函数根据event_idx、新旧索引值判断是否需要发送中断信号*/
	/*如果需要发送中断信号，更新last_used_signalled为新的索引值，并返回TRUE*/
	if (vmm_vring_need_event(event_idx, new_idx, old_idx)) {
		vq->last_used_signalled = new_idx;
		return TRUE;
	}

	return FALSE;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_should_signal);
/**
 * @description: 设置avail事件索引，通常在处理完队列中的所有描述符后调用
 * @param {vmm_virtio_queue} *vq
 * @return {*}
 */
void vmm_virtio_queue_set_avail_event(struct vmm_virtio_queue *vq)
{
	u16 val;
	u32 ret;
	physical_addr_t avail_evt_pa;

	if (!vq || !vq->guest) {
		return;
	}
	/*计算used结构中ring[num]字段的物理地址，这是事件索引应该写入的位置*/
	val = vq->last_avail_idx;
	avail_evt_pa = vq->vring.used_pa +
		  offsetof(struct vmm_vring_used, ring[vq->vring.num]);
	/*将last_avail_idx值写入到此物理地址，表示设备已经处理完这么多描述符*/
	ret = vmm_guest_memory_write(vq->guest, avail_evt_pa,
				     &val, sizeof(val), TRUE);
	if (ret != sizeof(val)) {
		vmm_printf("%s: write failed at avail_evt_pa=0x%"PRIPADDR"\n",
			   __func__, avail_evt_pa);
	}

}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_set_avail_event);
/**
 * @description: 用于在处理完描述符后更新VirtIO队列的“已使用”（used）部分
 * @param {vmm_virtio_queue} *vq 指向VirtIO队列的指针
 * @param {u32} head  完成处理的描述符链的头部索引
 * @param {u32} len  完成处理的描述符链的长度
 * @return {*}
 */
void vmm_virtio_queue_set_used_elem(struct vmm_virtio_queue *vq,u32 head, u32 len)
{
	u32 ret;
	u16 used_idx;
	struct vmm_vring_used_elem used_elem;
	physical_addr_t used_idx_pa, used_elem_pa;

	if (!vq || !vq->guest) {
		return;
	}
	/*读取“已使用”索引（used_idx），这是下一个“已使用”元素（used element）应该放置的位置*/
	used_idx_pa = vq->vring.used_pa +
		      offsetof(struct vmm_vring_used, idx);
	ret = vmm_guest_memory_read(vq->guest, used_idx_pa,
				    &used_idx, sizeof(used_idx), TRUE);
	if (ret != sizeof(used_idx)) {
		vmm_printf("%s: read failed at used_idx_pa=0x%"PRIPADDR"\n",
			   __func__, used_idx_pa);
	}
	/*根据head和len值创建一个新的“已使用”元素*/
	used_elem.id = head;
	used_elem.len = len;
	ret = umod32(used_idx, vq->vring.num);
	used_elem_pa = vq->vring.used_pa +
		       offsetof(struct vmm_vring_used, ring[ret]);
	/*计算新的“已使用”元素应该写入的物理地址，并将元素写入客户机内存*/
	ret = vmm_guest_memory_write(vq->guest, used_elem_pa,
				     &used_elem, sizeof(used_elem), TRUE);
	if (ret != sizeof(used_elem)) {
		vmm_printf("%s: write failed at used_elem_pa=0x%"PRIPADDR"\n",
			   __func__, used_elem_pa);
	}
	/*最后，used_idx加一，更新“已使用”索引，并将其写回客户机内存*/
	used_idx++;
	ret = vmm_guest_memory_write(vq->guest, used_idx_pa,
				     &used_idx, sizeof(used_idx), TRUE);
	if (ret != sizeof(used_idx)) {
		vmm_printf("%s: write failed at used_idx_pa=0x%"PRIPADDR"\n",
			   __func__, used_idx_pa);
	}
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_set_used_elem);

/*判断VirtIO队列是否已经完成了设置*/
bool vmm_virtio_queue_setup_done(struct vmm_virtio_queue *vq)
{
	return (vq) ? ((vq->guest) ? TRUE : FALSE) : FALSE;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_setup_done);
/**
 * @description: 清理或重置VirtIO队列
 * @param {vmm_virtio_queue} *vq
 * @return {*}
 */
int vmm_virtio_queue_cleanup(struct vmm_virtio_queue *vq)
{
	if (!vq || !vq->guest) {
		goto done;
	}

	vq->last_avail_idx = 0;
	vq->last_used_signalled = 0;

	vq->guest = NULL;

	vq->desc_count = 0;
	vq->align = 0;
	vq->guest_pfn = 0;
	vq->guest_page_size = 0;

	vq->guest_addr = 0;
	vq->host_addr = 0;
	vq->total_size = 0;

done:
	return VMM_OK;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_cleanup);

/**
 * @description: 设置（初始化）一个VirtIO队列
 * @param {vmm_virtio_queue} *vq
 * @param {vmm_guest} *guest 指向VirtIO队列所属客户机的指针
 * @param {physical_addr_t} guest_pfn 客户机物理帧号，用于计算队列的客户机物理地址
 * @param {physical_size_t} guest_page_size  客户机页面大小，与guest_pfn一起用于计算物理地址
 * @param {u32} desc_count  队列中描述符的数量
 * @param {u32} align 队列对齐要求
 * @return {*}
 */
int vmm_virtio_queue_setup(struct vmm_virtio_queue *vq,
			   struct vmm_guest *guest,
			   physical_addr_t guest_pfn,
			   physical_size_t guest_page_size,
			   u32 desc_count, u32 align)
{
	int rc = VMM_OK;
	u32 reg_flags;
	physical_addr_t gphys_addr, hphys_addr;
	physical_size_t gphys_size, avail_size;

	if (!vq || !guest) {
		return VMM_EFAIL;
	}
	// 先清理队列
	if ((rc = vmm_virtio_queue_cleanup(vq))) {
		vmm_printf("%s: cleanup failed\n", __func__);
		return rc;
	}
	// 计算队列在客户机物理地址空间中的起始地址和大小
	gphys_addr = guest_pfn * guest_page_size;
	gphys_size = vmm_vring_size(desc_count, align);
	// 调用vmm_guest_physical_map函数将队列映射到宿主物理地址空间
	if ((rc = vmm_guest_physical_map(guest, gphys_addr, gphys_size,
					 &hphys_addr, &avail_size,
					 &reg_flags))) {
		vmm_printf("%s: vmm_guest_physical_map() failed\n", __func__);
		return VMM_EFAIL;
	}
	// 检查队列是否映射到RAM
	if (!(reg_flags & VMM_REGION_ISRAM)) {
		vmm_printf("%s: region is not backed by RAM\n", __func__);
		return VMM_EINVALID;
	}
	// 检查队列的可用大小是否大于所需大小
	if (avail_size < gphys_size) {
		vmm_printf("%s: available size less than required size\n",
			   __func__);
		return VMM_EINVALID;
	}
	// 调用vmm_vring_init函数初始化队列的vring结构
	vmm_vring_init(&vq->vring, desc_count, NULL, gphys_addr, align);
	// 设置队列的其他属性，包括它所属的客户机、描述符计数、对齐要求、客户机页面大小等
	vq->guest = guest;
	vq->desc_count = desc_count;
	vq->align = align;
	vq->guest_pfn = guest_pfn;
	vq->guest_page_size = guest_page_size;

	vq->guest_addr = gphys_addr;
	vq->host_addr = hphys_addr;
	vq->total_size = gphys_size;

	return VMM_OK;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_setup);

/*
 * Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, max descriptor count
 * if we're at the end.
 */

/**
 * @description: 获取VirtIO队列中下一个描述符
 * @param {vmm_virtio_queue} *vq 指向VirtIO队列的指针
 * @param {vmm_vring_desc} *desc 当前描述符
 * @param {u32} i  当前描述符的索引
 * @param {u32} max 描述符数量的上限
 * @return {*}
 */
static unsigned next_desc(struct vmm_virtio_queue *vq,
			  struct vmm_vring_desc *desc,
			  u32 i, u32 max)
{
	int rc;
	u32 next;
	// 检查当前描述符是否有NEXT标志，即是否有后续描述符
	if (!(desc->flags & VMM_VRING_DESC_F_NEXT)) {
		return max;
	}
	// 计算下一个描述符的索引
	next = desc->next;
	// 从客户机的物理内存中读取下一个描述符的内容
	rc = vmm_virtio_queue_get_desc(vq, next, desc);
	if (rc) {
		vmm_printf("%s: failed to get descriptor next=%d error=%d\n",
			   __func__, next, rc);
		return max;
	}

	return next;
}

/**
 * @description: 根据给定的头描述符索引(head)，获取一连串IO向量（即一组内存段）的信息
 * @param {vmm_virtio_queue} *vq 指向virtio队列的指针
 * @param {u16} head  当前处理的描述符链的头描述符索引
 * @param {vmm_virtio_iovec} *iov 用于存放获取到的IO向量信息的数组
 * @param {u32} *ret_iov_cnt  返回找到的IO向量数量的指针
 * @param {u32} *ret_total_len 返回所有IO向量的总长度的指针
 * @param {u16} *ret_head 返回头描述符索引的指针
 * @return {*}
 */
int vmm_virtio_queue_get_head_iovec(struct vmm_virtio_queue *vq,
				    u16 head, struct vmm_virtio_iovec *iov,
				    u32 *ret_iov_cnt, u32 *ret_total_len,
				    u16 *ret_head)
{
	int i, rc = VMM_OK;
	u16 idx, max;
	struct vmm_vring_desc desc;

	if (!vq || !vq->guest || !iov) {
		goto fail;
	}

	idx = head;
	// 初始化返回值
	if (ret_iov_cnt) {
		*ret_iov_cnt = 0;
	}
	if (ret_total_len) {
		*ret_total_len = 0;
	}
	if (ret_head) {
		*ret_head = 0;
	}
	// 获取队列中描述符的最大数量
	max = vmm_virtio_queue_max_desc(vq);
	// 从队列中获取头描述符
	rc = vmm_virtio_queue_get_desc(vq, idx, &desc);
	if (rc) {
		vmm_printf("%s: failed to get descriptor idx=%d error=%d\n",
			   __func__, idx, rc);
		goto fail;
	}
	//如果头描述符是一个间接描述符（即它引用了另一组描述符），则当前不支持处理，函数会返回不支持的错误
	if (desc.flags & VMM_VRING_DESC_F_INDIRECT) {
#if 0
		max = desc[idx].len / sizeof(struct vring_desc);
		desc = guest_flat_to_host(kvm, desc[idx].addr);
		idx = 0;
#endif
		vmm_printf("%s: indirect descriptor not supported idx=%d\n",
			   __func__, idx);
		rc = VMM_ENOTSUPP;
		goto fail;
	}

	i = 0;
	/*循环通过调用next_desc函数遍历描述符链，直到到达链尾*/
	do {
		//在循环中，每个描述符的地址和长度被存储到iov数组中
		iov[i].addr = desc.addr;
		iov[i].len = desc.len;

		if (ret_total_len) {
			*ret_total_len += desc.len;
		}
		//根据描述符的flags设置IO向量的flags（表示读或写）
		if (desc.flags & VMM_VRING_DESC_F_WRITE) {
			iov[i].flags = 1;  /* Write */
		} else {
			iov[i].flags = 0; /* Read */
		}

		i++; //更新返回的总长度
	} while ((idx = next_desc(vq, &desc, idx, max)) != max);

	if (ret_iov_cnt) {
		*ret_iov_cnt = i;
	}
	// 更新可用事件，以通知设备已准备好处理更多的描述符
	vmm_virtio_queue_set_avail_event(vq);
	//设置返回的头描述符索引（如果提供了指针）并返回成功
	if (ret_head) {
		*ret_head = head;
	}

	return VMM_OK;
//如果函数在任何点失败（例如，获取描述符信息时），它将重置返回的IO向量数量和总长度
fail:
	if (ret_iov_cnt) {
		*ret_iov_cnt = 0;
	}
	if (ret_total_len) {
		*ret_total_len = 0;
	}
	return rc;
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_get_head_iovec);

/**
 * @description: 从virtio队列中获取一个数据块的IO向量信息
 * @param {vmm_virtio_queue} *vq 指向virtio队列的指针
 * @param {vmm_virtio_iovec} *iov 用于存放获取到的IO向量信息的数组
 * @param {u32} *ret_iov_cnt 返回的IO向量数量
 * @param {u32} *ret_total_len 返回的所有IO向量的总长度
 * @param {u16} *ret_head 返回的头描述符索引
 * @return {*}
 */
int vmm_virtio_queue_get_iovec(struct vmm_virtio_queue *vq,
			       struct vmm_virtio_iovec *iov,
			       u32 *ret_iov_cnt, u32 *ret_total_len,
			       u16 *ret_head)
{
	u16 head = vmm_virtio_queue_pop(vq);//调用vmm_virtio_queue_pop从virtio队列中获取下一个请求头编号
	//使用获取的请求头编号作为参数，调用vmm_virtio_queue_get_head_iovec函数获取与此请求头相关的所有IO向量信息，并填充到iov数组中
	return vmm_virtio_queue_get_head_iovec(vq, head, iov,
					       ret_iov_cnt, ret_total_len,
					       ret_head);
}
VMM_EXPORT_SYMBOL(vmm_virtio_queue_get_iovec);

/**
 * @description: 此函数从一组指定的IO向量中读取数据到一个缓冲区
 * @param {vmm_virtio_device} *dev 指向VirtIO设备的指针
 * @param {vmm_virtio_iovec} *iov 指向IO向量数组的指针
 * @param {u32} iov_cnt IO向量的数量
 * @param {void} *buf 目标缓冲区，用于存放读取的数据
 * @param {u32} buf_len 缓冲区的大小
 * @return {*}
 */
u32 vmm_virtio_iovec_to_buf_read(struct vmm_virtio_device *dev,
				 struct vmm_virtio_iovec *iov,
				 u32 iov_cnt, void *buf,
				 u32 buf_len)
{
	u32 i = 0, pos = 0, len = 0;
	//遍历所有IO向量，直到处理完所有向量或填满缓冲区为止
	for (i = 0; i < iov_cnt && pos < buf_len; i++) { //对于每个向量，计算实际要读取的长度（确保不会超出缓冲区大小）
		len = ((buf_len - pos) < iov[i].len) ?
				(buf_len - pos) : iov[i].len;
		//调用vmm_guest_memory_read从客户的内存中读取数据到缓冲区
		len = vmm_guest_memory_read(dev->guest, iov[i].addr,
					    buf + pos, len, TRUE);
		if (!len) {
			break;
		}
		//更新已读取的数据量
		pos += len;
	}

	return pos;
}
VMM_EXPORT_SYMBOL(vmm_virtio_iovec_to_buf_read);

/**
 * @description: 此函数从缓冲区写入数据到一组指定的IO向量中
 * @param {vmm_virtio_device} *dev
 * @param {vmm_virtio_iovec} *iov
 * @param {u32} iov_cnt
 * @param {void} *buf
 * @param {u32} buf_len
 * @return {*}
 */
u32 vmm_virtio_buf_to_iovec_write(struct vmm_virtio_device *dev,
				  struct vmm_virtio_iovec *iov,
				  u32 iov_cnt, void *buf,
				  u32 buf_len)
{
	u32 i = 0, pos = 0, len = 0;
	//遍历所有IO向量，直到处理完所有向量或缓冲区中的数据全部写入为止
	for (i = 0; i < iov_cnt && pos < buf_len; i++) {
		//对于每个向量，计算实际要写入的长度
		len = ((buf_len - pos) < iov[i].len) ?
					(buf_len - pos) : iov[i].len;

		len = vmm_guest_memory_write(dev->guest, iov[i].addr,
					     buf + pos, len, TRUE);
		if (!len) {
			break;
		}

		pos += len;//更新已写入的数据量
	}

	return pos;
}
VMM_EXPORT_SYMBOL(vmm_virtio_buf_to_iovec_write);

/**
 * @description: 将给定的IO向量所指向的客户内存区域填充为零
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @param {vmm_virtio_iovec} *iov 指向IO向量数组的指针，这些向量指定了需要被填充为零的客户内存区域
 * @param {u32} iov_cnt IO向量的数量
 * @return {*}
 */
void vmm_virtio_iovec_fill_zeros(struct vmm_virtio_device *dev,
				 struct vmm_virtio_iovec *iov,
				 u32 iov_cnt)
{
	u32 i = 0, pos = 0, len = 0;
	u8 zeros[16];
	//初始化一个全为零的缓冲区zeros
	memset(zeros, 0, sizeof(zeros));
	//遍历所有IO向量，对每个向量指定的内存区域执行填充操作
	while (i < iov_cnt) {
		len = (iov[i].len < 16) ? iov[i].len : 16;//len是当前向量所指定的内存区域长度和zeros缓冲区大小中较小的一个
		len = vmm_guest_memory_write(dev->guest, iov[i].addr + pos,
					     zeros, len, TRUE);
		if (!len) {
			break;
		}
		//更新已填充的数据量
		pos += len;
		if (pos == iov[i].len) {
			pos = 0;
			i++;
		}
	}
}
VMM_EXPORT_SYMBOL(vmm_virtio_iovec_fill_zeros);

/* ========== VirtIO device and emulator implementations ========== */
/**
 * @description: 重置指定的virtio设备所关联的仿真器
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @return {*}
 */
static int __virtio_reset_emulator(struct vmm_virtio_device *dev)
{
	if (dev && dev->emu && dev->emu->reset) { //检查dev及其emu成员和emu->reset回调是否存在
		return dev->emu->reset(dev);  //调用emu->reset回调
	}

	return VMM_OK;
}

/**
 * @description: 将virtio设备与指定的仿真器连接起来
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @param {vmm_virtio_emulator} *emu 指向要连接的仿真器的指针
 * @return {*}
 */
static int __virtio_connect_emulator(struct vmm_virtio_device *dev,
				     struct vmm_virtio_emulator *emu)
{
	if (dev && emu && emu->connect) {
		return emu->connect(dev, emu);
	}

	return VMM_OK;
}
/**
 * @description: 断开virtio设备与其仿真器之间的连接
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @return {*}
 */
static void __virtio_disconnect_emulator(struct vmm_virtio_device *dev)
{
	if (dev && dev->emu && dev->emu->disconnect) {
		dev->emu->disconnect(dev);
	}
}

/**
 * @description: 读取和virtio设备的仿真器配置
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @param {u32} offset  配置数据的偏移量
 * @param {void} *dst 读操作的目标缓冲区
 * @param {u32} dst_len 要读的数据长度
 * @return {*}
 */
static int __virtio_config_read_emulator(struct vmm_virtio_device *dev,
					 u32 offset, void *dst, u32 dst_len)
{
	if (dev && dev->emu && dev->emu->read_config) {
		return dev->emu->read_config(dev, offset, dst, dst_len);
	}

	return VMM_OK;
}

/**
 * @description: 
 * @param {vmm_virtio_device} *dev 指向virtio设备的指针
 * @param {u32} offset 配置数据的偏移量 
 * @param {void} *src 写操作的源缓冲区
 * @param {u32} src_len 要写的数据长度
 * @return {*}
 */
static int __virtio_config_write_emulator(struct vmm_virtio_device *dev,
					  u32 offset, void *src, u32 src_len)
{
	if (dev && dev->emu && dev->emu->write_config) {
		return dev->emu->write_config(dev, offset, src, src_len);
	}

	return VMM_OK;
}

/**
 * @description: 检查给定的virtio设备是否与给定的设备ID列表匹配
 * @param {vmm_virtio_device_id} *ids 设备ID列表的指针
 * @param {vmm_virtio_device} *dev 要检查的virtio设备的指针
 * @return {*}
 */
static bool __virtio_match_device(const struct vmm_virtio_device_id *ids,
				  struct vmm_virtio_device *dev)
{
	while (ids->type) {
		if (ids->type == dev->id.type)
			return TRUE;
		ids++;
	}
	return FALSE;
}

/**
 * @description: 将一个virtio设备与一个仿真器绑定
 * @param {vmm_virtio_device} *dev 需要绑定仿真器的virtio设备
 * @param {vmm_virtio_emulator} *emu 要绑定到设备的仿真器
 * @return {*}
 */
static int __virtio_bind_emulator(struct vmm_virtio_device *dev,
				  struct vmm_virtio_emulator *emu)
{
	int rc = VMM_EINVALID;
	if (__virtio_match_device(emu->id_table, dev)) {//检查设备与仿真器是否匹配
		dev->emu = emu;
		if ((rc = __virtio_connect_emulator(dev, emu))) {
			dev->emu = NULL;
		}
	}
	return rc;
}
/**
 * @description: 为一个未绑定仿真器的virtio设备找到合适的仿真器并绑定
 * @param {vmm_virtio_device} *dev
 * @return {*}
 */
static int __virtio_find_emulator(struct vmm_virtio_device *dev)
{
	struct vmm_virtio_emulator *emu;

	if (!dev || dev->emu) {
		return VMM_EINVALID;
	}

	list_for_each_entry(emu, &virtio_emu_list, node) {
		if (!__virtio_bind_emulator(dev, emu)) {
			return VMM_OK;
		}
	}

	return VMM_EFAIL;
}
/**
 * @description: 为所有未绑定仿真器的virtio设备尝试绑定指定的仿真器
 * @param {vmm_virtio_emulator} *emu
 * @return {*}
 */
static void __virtio_attach_emulator(struct vmm_virtio_emulator *emu)
{
	struct vmm_virtio_device *dev;

	if (!emu) {
		return;
	}
	/*遍历所有virtio设备，对于每一个未绑定仿真器的设备，尝试使用__virtio_bind_emulator绑定当前仿真器*/
	list_for_each_entry(dev, &virtio_dev_list, node) {
		if (!dev->emu) {
			__virtio_bind_emulator(dev, emu);
		}
	}
}

/**
 * @description: 读取virtio设备的配置
 * @param {vmm_virtio_device} *dev
 * @param {u32} offset
 * @param {void} *dst
 * @param {u32} dst_len
 * @return {*}
 */
int vmm_virtio_config_read(struct vmm_virtio_device *dev,
			   u32 offset, void *dst, u32 dst_len)
{
	return __virtio_config_read_emulator(dev, offset, dst, dst_len);
}
VMM_EXPORT_SYMBOL(vmm_virtio_config_read);

/*写入virtio设备的配置*/
int vmm_virtio_config_write(struct vmm_virtio_device *dev,
			    u32 offset, void *src, u32 src_len)
{
	return __virtio_config_write_emulator(dev, offset, src, src_len);
}
VMM_EXPORT_SYMBOL(vmm_virtio_config_write);
/*重置virtio设备*/
int vmm_virtio_reset(struct vmm_virtio_device *dev)
{
	return __virtio_reset_emulator(dev);
}
VMM_EXPORT_SYMBOL(vmm_virtio_reset);
/**
 * @description: 注册一个virtio设备
 * @param {vmm_virtio_device} *dev
 * @return {*}
 */
int vmm_virtio_register_device(struct vmm_virtio_device *dev)
{
	int rc = VMM_OK;

	if (!dev || !dev->tra) {
		return VMM_EFAIL;
	}
	/*初始化设备链表节点，清空仿真器指针和仿真器数据*/
	INIT_LIST_HEAD(&dev->node);
	dev->emu = NULL;
	dev->emu_data = NULL;

	vmm_mutex_lock(&virtio_mutex);
	/*将设备添加到全局设备列表virtio_dev_list*/
	list_add_tail(&dev->node, &virtio_dev_list);
	rc = __virtio_find_emulator(dev);//尝试为该设备找到合适的仿真器并绑定

	vmm_mutex_unlock(&virtio_mutex);

	return rc;
}
VMM_EXPORT_SYMBOL(vmm_virtio_register_device);
/**
 * @description: 注销一个已注册的virtio设备
 * @param {vmm_virtio_device} *dev
 * @return {*}
 */
void vmm_virtio_unregister_device(struct vmm_virtio_device *dev)
{
	if (!dev) {
		return;
	}

	vmm_mutex_lock(&virtio_mutex);

	__virtio_disconnect_emulator(dev);
	list_del(&dev->node);

	vmm_mutex_unlock(&virtio_mutex);
}
VMM_EXPORT_SYMBOL(virtio_unregister_device);
/**
 * @description: 注册一个virtio设备仿真器
 * @param {vmm_virtio_emulator} *emu
 * @return {*}
 */
int vmm_virtio_register_emulator(struct vmm_virtio_emulator *emu)
{
	bool found;
	struct vmm_virtio_emulator *vemu;

	if (!emu) {
		return VMM_EFAIL;
	}

	vemu = NULL;
	found = FALSE;

	vmm_mutex_lock(&virtio_mutex);
	/*遍历已注册的仿真器列表，检查是否已存在同名仿真器，若存在则返回失败*/
	list_for_each_entry(vemu, &virtio_emu_list, node) {
		if (strcmp(vemu->name, emu->name) == 0) {
			found = TRUE;
			break;
		}
	}

	if (found) {
		vmm_mutex_unlock(&virtio_mutex);
		return VMM_EFAIL;
	}
	/*初始化仿真器链表节点，将仿真器添加到全局仿真器列表virtio_emu_list*/
	INIT_LIST_HEAD(&emu->node);
	list_add_tail(&emu->node, &virtio_emu_list);
	/*尝试为所有未绑定仿真器的virtio设备绑定当前注册的仿真器*/
	__virtio_attach_emulator(emu);

	vmm_mutex_unlock(&virtio_mutex);

	return VMM_OK;
}
VMM_EXPORT_SYMBOL(vmm_virtio_register_emulator);
/**
 * @description: 注销（unregister）一个已经注册的virtio设备仿真器（emulator）
 * @param {vmm_virtio_emulator} *emu
 * @return {*}
 */
void vmm_virtio_unregister_emulator(struct vmm_virtio_emulator *emu)
{
	struct vmm_virtio_device *dev;

	vmm_mutex_lock(&virtio_mutex);
	/*使用list_del函数将emu指向的仿真器从全局仿真器列表virtio_emu_list中移除*/
	list_del(&emu->node);
	/* 通过list_for_each_entry宏遍历所有已注册的virtio设备列表virtio_dev_list */
	list_for_each_entry(dev, &virtio_dev_list, node) {
		if (dev->emu == emu) { /*检查绑定的仿真器:*/
			__virtio_disconnect_emulator(dev);//断开仿真器连接
			__virtio_find_emulator(dev); //尝试为设备找到新的仿真器
		}
	}

	vmm_mutex_unlock(&virtio_mutex);
}
VMM_EXPORT_SYMBOL(vmm_virtio_unregister_emulator);

static int __init vmm_virtio_core_init(void)
{
	/* Nothing to be done */
	return VMM_OK;
}

static void __exit vmm_virtio_core_exit(void)
{
	/* Nothing to be done */
}

VMM_DECLARE_MODULE(MODULE_DESC,
			MODULE_AUTHOR,
			MODULE_LICENSE,
			MODULE_IPRIORITY,
			MODULE_INIT,
			MODULE_EXIT);
