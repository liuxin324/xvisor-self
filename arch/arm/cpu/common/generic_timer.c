/**
 * Copyright (c) 2012 Sukanto Ghosh.
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
 * @file generic_timer.c
 * @author Sukanto Ghosh (sukantoghosh@gmail.com)
 * @brief API implementation for ARM architecture generic timers
 */

#include <vmm_error.h>
#include <vmm_heap.h>
#include <vmm_smp.h>
#include <vmm_cpuhp.h>
#include <vmm_stdio.h>
#include <vmm_devtree.h>
#include <vmm_host_irq.h>
#include <vmm_clockchip.h>
#include <vmm_clocksource.h>
#include <vmm_host_aspace.h>
#include <vmm_scheduler.h>
#include <vmm_devemu.h>
#include <generic_timer.h>
#include <cpu_generic_timer.h>
#include <libs/mathlib.h>

#undef DEBUG

#ifdef DEBUG
#define DPRINTF(msg...)			vmm_printf(msg)
#else
#define DPRINTF(msg...)
#endif

enum gen_timer_type {
	GENERIC_SPHYSICAL_TIMER,	// 特殊物理计时器
	GENERIC_PHYSICAL_TIMER, // 物理计时器
	GENERIC_VIRTUAL_TIMER,  // 虚拟计时器
	GENERIC_HYPERVISOR_TIMER, // Hypervisor计时器
};

static u32 generic_timer_hz = 0; // 存储计时器的频率（单位为赫兹）
static u32 generic_timer_mult = 0; // 乘法器，用于时间计算，通常与时间转换相关
static u32 generic_timer_shift = 0; // 移位参数，同样用于时间计算中的优化
/**
 * @description: 获取计时器的频率，并根据需要更新计时器的配置。
 * @param {vmm_devtree_node} *node 指向设备树节点的指针
 * @return {*}
 */
static void generic_timer_get_freq(struct vmm_devtree_node *node)
{
    int rc;

    if (generic_timer_hz == 0) {
        rc = vmm_devtree_clock_frequency(node, &generic_timer_hz); // 从设备树中读取计时器频率,存储在generic_timer_hz中
        if (rc) {
            /* Use preconfigured counter frequency
             * in absence of dts node
             */
            generic_timer_hz = generic_timer_reg_read(GENERIC_TIMER_REG_FREQ); // 如果rc不为0（表示读取失败），则尝试从cntfrq_el0直接读取频率
        } else { // 如果读取成功,将频率写入到计时器寄存器中
            if (generic_timer_freq_writeable()) {
                /* Program the counter frequency
                 * as per the dts node
                 */
                generic_timer_reg_write(GENERIC_TIMER_REG_FREQ, generic_timer_hz);
            }
        }
    }
}
/*读取物理计数器cntpct_el0的值*/
static u64 generic_counter_read(struct vmm_clocksource *cs)
{
	return generic_timer_pcounter_read();
}
/**
 * @description: 时间源初始化的核心
 * @param {vmm_devtree_node} *node
 * @return {*}
 */
static int __init generic_timer_clocksource_init(struct vmm_devtree_node *node)
{
	struct vmm_clocksource *cs;

	generic_timer_get_freq(node); // 获取计时器的频率
	if (generic_timer_hz == 0) { // 频率值存储在全局变量generic_timer_hz中
		return VMM_EFAIL;
	}

	cs = vmm_zalloc(sizeof(struct vmm_clocksource));// 为时间源分配内存
	if (!cs) {
		return VMM_EFAIL;
	}

	cs->name = "gen-timer"; // 时间源的名称
	cs->rating = 400; // 该时间源的可靠性或精确度评级
	cs->read = &generic_counter_read; // 获取当前的cntpct_el0值
	cs->mask = VMM_CLOCKSOURCE_MASK(56);// 限定了时间源的有效范围
	cs->freq = generic_timer_hz; // 时间源的频率
	vmm_clocks_calc_mult_shift(&cs->mult, &cs->shift,
				   generic_timer_hz, VMM_NSEC_PER_SEC, 10);//计算乘法器和位移值，这用于将计时器的计数值转换成纳秒单位
	generic_timer_mult = cs->mult;
	generic_timer_shift = cs->shift;
	cs->priv = NULL;

	return vmm_clocksource_register(cs);//调用vmm_clocksource_register(cs)注册时间源
}
VMM_CLOCKSOURCE_INIT_DECLARE(gtv7clksrc, "arm,armv7-timer", generic_timer_clocksource_init);
VMM_CLOCKSOURCE_INIT_DECLARE(gtv8clksrc, "arm,armv8-timer", generic_timer_clocksource_init);
/**
 * @description: 处理来自Hypervisor层面的定时器中断
 * @param {int} irq 中断号
 * @param {void} *dev  指向与中断关联的设备或数据结构的指针
 * @return {*}
 */
static vmm_irq_return_t generic_hyp_timer_handler(int irq, void *dev)
{
	struct vmm_clockchip *cc = dev; // 将传入的dev参数强制转换为vmm_clockchip结构的指针，vmm_clockchip是一个时钟芯片的表示，它通常包含控制计时器行为的函数指针和状态
	unsigned long ctrl;

	ctrl = generic_timer_reg_read(GENERIC_TIMER_REG_HYP_CTRL);//读取cnthp_ctl_el2寄存器的值
	if (ctrl & GENERIC_TIMER_CTRL_IT_STAT) { // 检查控制寄存器中的中断状态位-这个位如果设置，说明定时器产生了中断
		ctrl |= GENERIC_TIMER_CTRL_IT_MASK;// 设置控制寄存器中的中断屏蔽位-这样做是为了阻止在处理当前中断时再次触发中断
		ctrl &= ~GENERIC_TIMER_CTRL_ENABLE;// 清除控制寄存器中的启用位-禁用定时器
		generic_timer_reg_write(GENERIC_TIMER_REG_HYP_CTRL, ctrl);// 写回控制寄存器cnthp_ctl_el2
		cc->event_handler(cc);//调用时钟芯片结构中的事件处理函数
		return VMM_IRQ_HANDLED;//返回VMM_IRQ_HANDLED，表示这个中断已经被正确处理
	}

	return VMM_IRQ_NONE;//返回VMM_IRQ_NONE，表示此次中断没有被处理
}
/**
 * @description: 停止Hypervisor层的定时器-此函数确保定时器被安全地停止，不会产生额外的中断
 * @return {*}
 */
static void generic_timer_stop(void)
{
	unsigned long ctrl;

	ctrl = generic_timer_reg_read(GENERIC_TIMER_REG_HYP_CTRL);//读取cnthp_ctl_el2寄存器的值
	ctrl |= GENERIC_TIMER_CTRL_IT_MASK; // 设置控制寄存器中的中断屏蔽位
	ctrl &= ~GENERIC_TIMER_CTRL_ENABLE;// 清除控制寄存器中的启用位-禁用定时器
	generic_timer_reg_write(GENERIC_TIMER_REG_HYP_CTRL, ctrl);
}
/**
 * @description: 设置定时器的操作模式
 * @return {*}
 */
static void generic_timer_set_mode(enum vmm_clockchip_mode mode,
				struct vmm_clockchip *cc)
{
	switch (mode) {
	case VMM_CLOCKCHIP_MODE_UNUSED:
	case VMM_CLOCKCHIP_MODE_SHUTDOWN:
		generic_timer_stop();//停止定时器
		break;
	default:
		break;
	}
}
/**
 * @description: Hypervisor层的定时器，设置其下一个触发事件
 * @return {*}
 */
static int generic_timer_set_next_event(unsigned long evt,
				     struct vmm_clockchip *unused)
{
	unsigned long ctrl;

	ctrl = generic_timer_reg_read(GENERIC_TIMER_REG_HYP_CTRL);//cnthp_ctl_el2
	ctrl |= GENERIC_TIMER_CTRL_ENABLE; // 修改控制寄存器以启用定时器并解除中断屏蔽
	ctrl &= ~GENERIC_TIMER_CTRL_IT_MASK;

	generic_timer_reg_write(GENERIC_TIMER_REG_HYP_TVAL, evt);// cnthp_tval_el2 定义了从现在起到定时器下次触发的时间
	generic_timer_reg_write(GENERIC_TIMER_REG_HYP_CTRL, ctrl);// 写回修改后的控制寄存器

	return 0;
}

/**
 * @description: 此函数的目的是将物理定时器的中断注入到特定的虚拟CPU
 * @param {vmm_vcpu} *vcpu 虚拟CPU
 * @param {generic_timer_context} *cntx 通用定时器上下文信息
 * @return {*}
 */
static void generic_phys_irq_inject(struct vmm_vcpu *vcpu,
				    struct generic_timer_context *cntx)
{
	int rc;
	u32 pirq;

	pirq = cntx->phys_timer_irq;
	if (pirq == 0) { // 检查物理定时器中断号
		vmm_printf("%s: Physical timer irq not available (VCPU=%s)\n",
			   __func__, vcpu->name);
		return;
	}
	/*向虚拟CPU的所属虚拟机注入中断*/
	rc = vmm_devemu_emulate_percpu_irq2(vcpu->guest, pirq,
					    vcpu->subid, 0, 1);
	if (rc) {
		vmm_printf("%s: Emulate VCPU=%s irq=%d "
			   "level0=0 level1=1 failed (error %d)\n",
			   __func__, vcpu->name, pirq, rc);
	}
}
/**
 * @description: 物理定时器中断的处理函数，确保中断被正确处理，并在适当的上下文中将中断传递到虚拟CPU
 * @param {int} irq 触发中断的中断号
 * @param {void} *dev 指向与中断相关的设备或数据结构的指针
 * @return {*}
 */
static vmm_irq_return_t generic_phys_timer_handler(int irq, void *dev)
{
	u32 ctl;
	struct vmm_vcpu *vcpu;
	struct generic_timer_context *cntx;

	ctl = generic_timer_reg_read(GENERIC_TIMER_REG_PHYS_CTRL);// cntp_ctl_el0
	if (!(ctl & GENERIC_TIMER_CTRL_IT_STAT)) { /* 检查中断状态位（GENERIC_TIMER_CTRL_IT_STAT）。如果此位未设置，表明收到了一个不合预期的中断（可能是硬件问题），函数记录警告并返回VMM_IRQ_NONE*/
		/* We got interrupt without status bit set.
		 * Looks like we are running on buggy hardware.
		 */
		DPRINTF("%s: suprious interrupt\n", __func__);
		return VMM_IRQ_NONE;
	}

	ctl |= GENERIC_TIMER_CTRL_IT_MASK; // 更新控制寄存器，设置中断屏蔽位（GENERIC_TIMER_CTRL_IT_MASK）以防止进一步的中断，然后写回寄存器
	generic_timer_reg_write(GENERIC_TIMER_REG_PHYS_CTRL, ctl);

	vcpu = vmm_scheduler_current_vcpu(); // 获取当前处理器上正在执行的虚拟CPU
	if (!vcpu->is_normal) { //如果当前虚拟CPU不是正常模式（is_normal标志未设置），函数记录此情况并返回VMM_IRQ_NONE，表示这是一个孤立的中断
		/* We accidently got an interrupt meant for normal VCPU
		 * that was previously running on this host CPU.
		 */
		DPRINTF("%s: In orphan context (current VCPU=%s)\n",
			__func__, vcpu->name);
		return VMM_IRQ_NONE;
	}

	cntx = arm_gentimer_context(vcpu);// 调用arm_gentimer_context获取当前虚拟CPU的定时器上下文
	if (!cntx) {
		/* We accidently got an interrupt meant another normal VCPU */
		DPRINTF("%s: Invalid normal context (current VCPU=%s)\n",
			__func__, vcpu->name);
		return VMM_IRQ_NONE;
	}

	generic_phys_irq_inject(vcpu, cntx);//调用generic_phys_irq_inject将中断注入到正确的虚拟CPU

	return VMM_IRQ_HANDLED;
}
/**
 * @description: 将虚拟定时器的中断注入到指定的VCPU
 * @return {*}
 */
static void generic_virt_irq_inject(struct vmm_vcpu *vcpu,
				    struct generic_timer_context *cntx)
{
	int rc;
	u32 virq;

	virq = cntx->virt_timer_irq;
	if (virq == 0) {
		vmm_printf("%s: Virtual timer irq not available (VCPU=%s)\n",
			   __func__, vcpu->name);
		return;
	}

	rc = vmm_devemu_emulate_percpu_irq2(vcpu->guest, virq,
					    vcpu->subid, 0, 1);
	if (rc) {
		vmm_printf("%s: Emulate VCPU=%s irq=%d "
			   "level0=0 level1=1 failed (error %d)\n",
			   __func__, vcpu->name, virq, rc);
	}
}
/**
 * @description: 此函数作为虚拟定时器中断的处理函数，确保中断被正确处理，并在适当的上下文中将中断传递到虚拟CPU
 * @param {int} irq
 * @param {void} *dev
 * @return {*}
 */
static vmm_irq_return_t generic_virt_timer_handler(int irq, void *dev)
{
	u32 ctl;
	struct vmm_vcpu *vcpu;
	struct generic_timer_context *cntx;

	ctl = generic_timer_reg_read(GENERIC_TIMER_REG_VIRT_CTRL);// cntv_ctl_el0
	if (!(ctl & GENERIC_TIMER_CTRL_IT_STAT)) {
		/* We got interrupt without status bit set.
		 * Looks like we are running on buggy hardware.
		 */
		DPRINTF("%s: suprious interrupt\n", __func__);
		return VMM_IRQ_NONE;
	}

	ctl |= GENERIC_TIMER_CTRL_IT_MASK;
	generic_timer_reg_write(GENERIC_TIMER_REG_VIRT_CTRL, ctl);

	vcpu = vmm_scheduler_current_vcpu();
	if (!vcpu->is_normal) {
		/* We accidently got an interrupt meant for normal VCPU
		 * that was previously running on this host CPU.
		 */
		DPRINTF("%s: In orphan context (current VCPU=%s)\n",
			__func__, vcpu->name);
		return VMM_IRQ_NONE;
	}

	cntx = arm_gentimer_context(vcpu);
	if (!cntx) {
		/* We accidently got an interrupt meant another normal VCPU */
		DPRINTF("%s: Invalid normal context (current VCPU=%s)\n",
			__func__, vcpu->name);
		return VMM_IRQ_NONE;
	}

	generic_virt_irq_inject(vcpu, cntx);

	return VMM_IRQ_HANDLED;
}

static u32 timer_irq[4], timer_num_irqs;
/**
 * @description: 用于启动和初始化一个通用计时器 (generic timer)，包括为 Hypervisor 和物理/虚拟计时器设置中断处理程序
 * @param {vmm_cpuhp_notify} *cpuhp 用于CPU热插拔通知的结构
 * @param {u32} cpu CPU编号
 * @return {*}
 */
static int generic_timer_startup(struct vmm_cpuhp_notify *cpuhp, u32 cpu)
{
	int rc;
	u32 val;
	struct vmm_clockchip *cc;

	/* Ensure hypervisor timer is stopped */
	generic_timer_stop(); 

	/* Create generic hypervisor timer clockchip */
	cc = vmm_zalloc(sizeof(struct vmm_clockchip));
	if (!cc) {
		return VMM_EFAIL;
	}
	cc->name = "gen-hyp-timer"; // 计时器名称
	cc->hirq = timer_irq[GENERIC_HYPERVISOR_TIMER];
	cc->rating = 400; // 评级
	cc->cpumask = vmm_cpumask_of(vmm_smp_processor_id()); // CPU亲和性
	cc->features = VMM_CLOCKCHIP_FEAT_ONESHOT; // 功能（例如单次触发）
	cc->freq = generic_timer_hz; // 频率
	vmm_clocks_calc_mult_shift(&cc->mult, &cc->shift,
				   VMM_NSEC_PER_SEC, generic_timer_hz, 10);//计算时间转换的乘数和移位参数，用于将计时器的时间单位转换为纳秒
	cc->min_delta_ns = vmm_clockchip_delta2ns(0xF, cc);//设置最小和最大延迟
	cc->max_delta_ns = vmm_clockchip_delta2ns(0x7FFFFFFF, cc);
	cc->set_mode = &generic_timer_set_mode; // 设置模式
	cc->set_next_event = &generic_timer_set_next_event;
	cc->priv = NULL;

	/* Register hypervisor timer clockchip */
	/*调用 vmm_clockchip_register 来注册计时器*/
	rc = vmm_clockchip_register(cc);
	if (rc) {
		goto fail_free_cc;
	}

	/* Register irq handler for hypervisor timer */
	/*注册Hypervisor计时器的中断处理函数*/
	rc = vmm_host_irq_register(timer_irq[GENERIC_HYPERVISOR_TIMER],
				   "gen-hyp-timer",
				   &generic_hyp_timer_handler, cc);
	if (rc) {
		goto fail_unreg_cc;
	}
	/* 为物理计时器和虚拟计时器注册中断处理函数 */
	if (timer_num_irqs > 1) {
		/* Register irq handler for physical timer */
		
		rc = vmm_host_irq_register(timer_irq[GENERIC_PHYSICAL_TIMER],
					   "gen-phys-timer",
					   &generic_phys_timer_handler,
					   NULL);
		if (rc) {
			goto fail_unreg_htimer;
		}
	}

	if (timer_num_irqs > 2) {
		/* Register irq handler for virtual timer */
		rc = vmm_host_irq_register(timer_irq[GENERIC_VIRTUAL_TIMER],
					   "gen-virt-timer",
					   &generic_virt_timer_handler,
					   NULL);
		if (rc) {
			goto fail_unreg_ptimer;
		}
	}

	if (timer_num_irqs > 1) {
		val = generic_timer_reg_read(GENERIC_TIMER_REG_HCTL);
		val |= GENERIC_TIMER_HCTL_KERN_PCNT_EN;
		val |= GENERIC_TIMER_HCTL_KERN_PTMR_EN;
		generic_timer_reg_write(GENERIC_TIMER_REG_HCTL, val);
	}

	return VMM_OK;

fail_unreg_ptimer:
	if (timer_num_irqs > 1) {
		vmm_host_irq_unregister(timer_irq[GENERIC_PHYSICAL_TIMER],
					&generic_phys_timer_handler);
	}
fail_unreg_htimer:
	vmm_host_irq_unregister(timer_irq[GENERIC_HYPERVISOR_TIMER],
				&generic_hyp_timer_handler);
fail_unreg_cc:
	vmm_clockchip_unregister(cc);
fail_free_cc:
	vmm_free(cc);
	return rc;
}

static struct vmm_cpuhp_notify generic_timer_cpuhp = {
	.name = "GENERIC_TIMER",
	.state = VMM_CPUHP_STATE_CLOCKCHIP,
	.startup = generic_timer_startup,
};
/**
 * @description: 此函数的目的是为系统初始化通用计时器，并注册相应的中断处理器
 * @param {vmm_devtree_node} *node
 * @return {*}
 */
static int __init generic_timer_clockchip_init(struct vmm_devtree_node *node)
{
	/* Get and Check generic timer frequency */
	/*获取并检查计时器频率*/
	generic_timer_get_freq(node);
	if (generic_timer_hz == 0) {
		return VMM_EFAIL;
	}

	/* Get hypervisor timer irq number */
	timer_irq[GENERIC_HYPERVISOR_TIMER] = vmm_devtree_irq_parse_map(node,
						GENERIC_HYPERVISOR_TIMER);
	if (!timer_irq[GENERIC_HYPERVISOR_TIMER]) {
		return VMM_ENODEV;
	}

	/* Get physical timer irq number */
	timer_irq[GENERIC_PHYSICAL_TIMER] = vmm_devtree_irq_parse_map(node,
						GENERIC_PHYSICAL_TIMER);
	if (!timer_irq[GENERIC_PHYSICAL_TIMER]) {
		return VMM_ENODEV;
	}

	/* Get virtual timer irq number */
	timer_irq[GENERIC_VIRTUAL_TIMER] = vmm_devtree_irq_parse_map(node,
						GENERIC_VIRTUAL_TIMER);
	if (!timer_irq[GENERIC_VIRTUAL_TIMER]) {
		return VMM_ENODEV;
	}

	/* Number of generic timer irqs */
	timer_num_irqs = vmm_devtree_irq_count(node);
	if (!timer_num_irqs) {
		return VMM_EFAIL;
	}
	/*注册CPU热插拔处理函数*/
	return vmm_cpuhp_register(&generic_timer_cpuhp, TRUE);
}
VMM_CLOCKCHIP_INIT_DECLARE(gtv7clkchip, "arm,armv7-timer", generic_timer_clockchip_init);
VMM_CLOCKCHIP_INIT_DECLARE(gtv8clkchip, "arm,armv8-timer", generic_timer_clockchip_init);
/**
 * @description: 处理物理定时器到期事件的回调函数-通过操控特定的虚拟CPU (VCPU) 上下文来处理定时器到期事件，并将中断注入到相应的虚拟CPU中
 * @param {vmm_timer_event} *ev 指向到期定时器事件的指针
 * @return {*}
 */
static void generic_phys_timer_expired(struct vmm_timer_event *ev)
{
	struct vmm_vcpu *vcpu = ev->priv;
	struct generic_timer_context *cntx = arm_gentimer_context(vcpu);// 获取指向该VCPU相关的物理定时器上下文的指针

	BUG_ON(!cntx);

	cntx->cntpctl |= GENERIC_TIMER_CTRL_IT_MASK;// 设置定时器控制寄存器中的中断屏蔽位，避免中断的重复触发

	generic_phys_irq_inject(vcpu, cntx);// 将物理定时器的中断注入到相关的VCPU
}

/**
 * @description:  处理虚拟定时器到期事件
 * @param {vmm_timer_event} *ev
 * @return {*}
 */
static void generic_virt_timer_expired(struct vmm_timer_event *ev)
{
	struct vmm_vcpu *vcpu = ev->priv;
	struct generic_timer_context *cntx = arm_gentimer_context(vcpu);

	BUG_ON(!cntx);

	cntx->cntvctl |= GENERIC_TIMER_CTRL_IT_MASK;

	generic_virt_irq_inject(vcpu, cntx);
}

/**
 * @description: 这个函数负责设置一个新的或已存在的定时器上下文，为特定的虚拟CPU配置物理和虚拟定时器
 * @param {void} *vcpu_ptr 指向虚拟CPU的指针
 * @param {void} * *context 指向指向定时器上下文的指针
 * @param {u32} phys_irq 物理定时器中断号
 * @param {u32} virt_irq 虚拟定时器中断号
 * @return {*}
 */
int generic_timer_vcpu_context_init(void *vcpu_ptr,
				    void **context,
				    u32 phys_irq, u32 virt_irq)

{
	struct generic_timer_context *cntx;

	if (!context || !vcpu_ptr) {
		return VMM_EINVALID;
	}

	if (!(*context)) {
		*context = vmm_zalloc(sizeof(*cntx));
		if (!(*context)) {
			return VMM_ENOMEM;
		}
		cntx = *context; //使用INIT_TIMER_EVENT宏设置物理和虚拟定时器事件的处理函数和关联的虚拟CPU
		INIT_TIMER_EVENT(&cntx->phys_ev,
				 generic_phys_timer_expired, vcpu_ptr);
		INIT_TIMER_EVENT(&cntx->virt_ev,
				 generic_virt_timer_expired, vcpu_ptr);
	} else {
		cntx = *context;
	}
	/*配置定时器控制和中断号*/
	cntx->cntpctl = GENERIC_TIMER_CTRL_IT_MASK;
	cntx->cntvctl = GENERIC_TIMER_CTRL_IT_MASK;
	cntx->cntpcval = 0;
	cntx->cntvcval = 0;
	cntx->cntkctl = 0;
	cntx->cntvoff = 0;
	cntx->phys_timer_irq = phys_irq;
	cntx->virt_timer_irq = virt_irq;
	/*停止现有定时器事件*/
	vmm_timer_event_stop(&cntx->phys_ev);
	vmm_timer_event_stop(&cntx->virt_ev);

	return VMM_OK;
}
/**
 * @description: 这个函数负责清理和释放虚拟CPU的通用定时器上下文
 * @param {void} *vcpu_ptr
 * @param {void} *
 * @return {*}
 */
int generic_timer_vcpu_context_deinit(void *vcpu_ptr, void **context)
{
	struct generic_timer_context *cntx;

	if (!context || !vcpu_ptr) {
		return VMM_EINVALID;
	}
	if (!(*context)) {
		return VMM_EINVALID;
	}

	cntx = *context;

	vmm_timer_event_stop(&cntx->phys_ev);
	vmm_timer_event_stop(&cntx->virt_ev);

	vmm_free(cntx);

	return VMM_OK;
}
/**
 * @description: 此函数负责保存VCPU的通用定时器上下文，包括各种控制寄存器和计数值，确保在VCPU状态被暂停或迁移时，所有定时器相关信息都得以保留
 * @param {void} *vcpu_ptr
 * @param {void} *context
 * @return {*}
 */
void generic_timer_vcpu_context_save(void *vcpu_ptr, void *context)
{
	u64 ev_nsecs;
	struct generic_timer_context *cntx = context;

	if (!cntx) {
		return;
	}

#ifdef HAVE_GENERIC_TIMER_REGS_SAVE
	generic_timer_regs_save(cntx);
#else
	cntx->cntpctl = generic_timer_reg_read(GENERIC_TIMER_REG_PHYS_CTRL);
	cntx->cntvctl = generic_timer_reg_read(GENERIC_TIMER_REG_VIRT_CTRL);
	cntx->cntpcval = generic_timer_reg_read64(GENERIC_TIMER_REG_PHYS_CVAL);
	cntx->cntvcval = generic_timer_reg_read64(GENERIC_TIMER_REG_VIRT_CVAL);
	cntx->cntkctl = generic_timer_reg_read(GENERIC_TIMER_REG_KCTL);
	generic_timer_reg_write(GENERIC_TIMER_REG_PHYS_CTRL,
				GENERIC_TIMER_CTRL_IT_MASK);
	generic_timer_reg_write(GENERIC_TIMER_REG_VIRT_CTRL,
				GENERIC_TIMER_CTRL_IT_MASK);
#endif

	if ((cntx->cntpctl & GENERIC_TIMER_CTRL_ENABLE) &&
	    !(cntx->cntpctl & GENERIC_TIMER_CTRL_IT_MASK)) {
		ev_nsecs = generic_timer_pcounter_read();
		/* Check if timer already expired while saving the context */
		if (cntx->cntpcval <= ev_nsecs) {
			/* Immediate expiry */
			ev_nsecs = 0;
		} else {
			ev_nsecs = cntx->cntpcval - ev_nsecs;
			ev_nsecs = vmm_clocksource_delta2nsecs(ev_nsecs,
							generic_timer_mult,
							generic_timer_shift);
		}
		vmm_timer_event_start(&cntx->phys_ev, ev_nsecs);
	}

	if ((cntx->cntvctl & GENERIC_TIMER_CTRL_ENABLE) &&
	    !(cntx->cntvctl & GENERIC_TIMER_CTRL_IT_MASK)) {
		ev_nsecs = generic_timer_pcounter_read();
		/* Check if timer already expired while saving the context */
		if ((cntx->cntvcval + cntx->cntvoff) <= ev_nsecs) {
			/* Immediate expiry */
			ev_nsecs = 0;
		} else {
			ev_nsecs = (cntx->cntvcval + cntx->cntvoff) -
				   ev_nsecs;
			ev_nsecs = vmm_clocksource_delta2nsecs(ev_nsecs,
							generic_timer_mult,
							generic_timer_shift);
		}
		vmm_timer_event_start(&cntx->virt_ev, ev_nsecs);
	}
}
/**
 * @description: 此函数用于在VCPU恢复运行时恢复其通用定时器上下文
 * @param {void} *vcpu_ptr
 * @param {void} *context
 * @return {*}
 */
void generic_timer_vcpu_context_restore(void *vcpu_ptr, void *context)
{
	struct vmm_vcpu *vcpu = vcpu_ptr;
	struct generic_timer_context *cntx = context;

	if (!cntx) {
		return;
	}

	vmm_timer_event_stop(&cntx->phys_ev);
	vmm_timer_event_stop(&cntx->virt_ev);

	if (!cntx->cntvoff) {
		cntx->cntvoff = vmm_manager_guest_reset_timestamp(vcpu->guest);
		cntx->cntvoff = cntx->cntvoff * generic_timer_hz;
		cntx->cntvoff = udiv64(cntx->cntvoff, 1000000000ULL);
	}
}
/**
 * @description: 此函数用于处理VCPU恢复后的后续操作，主要是根据保存的定时器状态决定是否立即触发定时器中断
 * @param {void} *vcpu_ptr
 * @param {void} *context
 * @return {*}
 */
void generic_timer_vcpu_context_post_restore(void *vcpu_ptr, void *context)
{
	u64 pcnt;
	struct vmm_vcpu *vcpu = vcpu_ptr;
	struct generic_timer_context *cntx = context;

	if (!cntx) {
		return;
	}

	pcnt = generic_timer_pcounter_read();

	if ((cntx->cntpctl & GENERIC_TIMER_CTRL_ENABLE) &&
	    !(cntx->cntpctl & GENERIC_TIMER_CTRL_IT_MASK) &&
	     (cntx->cntpcval <= pcnt)) {
		cntx->cntpctl |= GENERIC_TIMER_CTRL_IT_MASK;
		generic_phys_irq_inject(vcpu, cntx);
	}

	if ((cntx->cntvctl & GENERIC_TIMER_CTRL_ENABLE) &&
	    !(cntx->cntvctl & GENERIC_TIMER_CTRL_IT_MASK) &&
	     ((cntx->cntvoff + cntx->cntvcval) <= pcnt)) {
		cntx->cntvctl |= GENERIC_TIMER_CTRL_IT_MASK;
		generic_virt_irq_inject(vcpu, cntx);
	}

#ifdef HAVE_GENERIC_TIMER_REGS_RESTORE
	generic_timer_regs_restore(cntx);
#else
	generic_timer_reg_write64(GENERIC_TIMER_REG_VIRT_OFF, cntx->cntvoff);
	generic_timer_reg_write(GENERIC_TIMER_REG_KCTL, cntx->cntkctl);
	generic_timer_reg_write64(GENERIC_TIMER_REG_PHYS_CVAL, cntx->cntpcval);
	generic_timer_reg_write64(GENERIC_TIMER_REG_VIRT_CVAL, cntx->cntvcval);
	generic_timer_reg_write(GENERIC_TIMER_REG_PHYS_CTRL, cntx->cntpctl);
	generic_timer_reg_write(GENERIC_TIMER_REG_VIRT_CTRL, cntx->cntvctl);
#endif
}
