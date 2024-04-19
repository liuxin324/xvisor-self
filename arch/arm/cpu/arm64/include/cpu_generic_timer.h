/**
 * Copyright (c) 2013 Sukanto Ghosh.
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
 * @file cpu_generic_timer.h
 * @author Sukanto Ghosh (sukantoghosh@gmail.com)
 * @brief CPU specific functions for ARM architecture generic timers
 */

#ifndef __CPU_GENERIC_TIMER_H__
#define __CPU_GENERIC_TIMER_H__

#include <vmm_types.h>
#include <arch_barrier.h>
#include <cpu_inline_asm.h>

#define generic_timer_pcounter_read()	mrs(cntpct_el0) // 读取物理计数器cntpct_el0的值
#define generic_timer_vcounter_read()	mrs(cntvct_el0) // 读取虚拟计数器cntvct_el0的值

/* cntfrq_el0 is writeable by highest implemented EL.
 * We are running at EL2 and if EL3 is not implemented, 
 * hypervisor can write to cntfrq_el0 */

/*检查cntfrq_el0（计时器频率寄存器）是否可写。这取决于CPU是否支持EL3（异常级别3）。
如果CPU不支持EL3，并且代码运行在EL2（通常是Hypervisor层），Hypervisor可以写入这个寄存器。
这通过宏cpu_supports_el3()判断，如果CPU不支持EL3，则返回true表示cntfrq_el0是可写的*/
#define generic_timer_freq_writeable()	(!cpu_supports_el3())

static inline void generic_timer_reg_write(int reg, u32 val)
{
	switch (reg) {
	case GENERIC_TIMER_REG_FREQ: // 写入 Counter-timer Frequency Register
		msr(cntfrq_el0, val);
		break;
	case GENERIC_TIMER_REG_HCTL:// Counter-timer Hypervisor Control Register-cnthctl_el2
		msr(cnthctl_el2, val);
		break;
	case GENERIC_TIMER_REG_KCTL:// Counter-timer Kernel Control Register -cntkctl_el1
		msr(cntkctl_el1, val);
		break;
	case GENERIC_TIMER_REG_HYP_CTRL:// Counter-timer Hypervisor Physical Timer Control Register
		msr(cnthp_ctl_el2, val);
		break;
	case GENERIC_TIMER_REG_HYP_TVAL:// Counter-timer Physical Timer TimerValue Register (EL2)
		msr(cnthp_tval_el2, val);
		break;
	case GENERIC_TIMER_REG_PHYS_CTRL:// Counter-timer Physical Timer Control Register
		msr(cntp_ctl_el0, val);
		break;
	case GENERIC_TIMER_REG_PHYS_TVAL:// Counter-timer Physical Timer TimerValue Register
		msr(cntp_tval_el0, val);
		break;
	case GENERIC_TIMER_REG_VIRT_CTRL:// Counter-timer Virtual Timer Control Register
		msr(cntv_ctl_el0, val);
		break;
	case GENERIC_TIMER_REG_VIRT_TVAL:// Counter-timer Virtual Timer TimerValue Register
		msr(cntv_tval_el0, val);
		break;
	default:
		vmm_panic("Trying to write invalid generic-timer register\n");
	}

	isb();
}

static inline u32 generic_timer_reg_read(int reg)
{
	u32 val;

	switch (reg) {
	case GENERIC_TIMER_REG_FREQ:
		val = mrs(cntfrq_el0);
		break;
	case GENERIC_TIMER_REG_HCTL:
		val = mrs(cnthctl_el2);
		break;
	case GENERIC_TIMER_REG_KCTL:
		val = mrs(cntkctl_el1);
		break;
	case GENERIC_TIMER_REG_HYP_CTRL:
		val = mrs(cnthp_ctl_el2);
		break;
	case GENERIC_TIMER_REG_HYP_TVAL:
		val = mrs(cnthp_tval_el2);
		break;
	case GENERIC_TIMER_REG_PHYS_CTRL:
		val = mrs(cntp_ctl_el0);
		break;
	case GENERIC_TIMER_REG_PHYS_TVAL:
		val = mrs(cntp_tval_el0);
		break;
	case GENERIC_TIMER_REG_VIRT_CTRL:
		val = mrs(cntv_ctl_el0);
		break;
	case GENERIC_TIMER_REG_VIRT_TVAL:
		val = mrs(cntv_tval_el0);
		break;
	default:
		vmm_panic("Trying to read invalid generic-timer register\n");
	}

	return val;
}

static inline void generic_timer_reg_write64(int reg, u64 val)
{
	switch (reg) {
	case GENERIC_TIMER_REG_HYP_CVAL:
		msr(cnthp_cval_el2, val); // Counter-timer Physical Timer CompareValue Register (EL2)
		break;
	case GENERIC_TIMER_REG_PHYS_CVAL:
		msr(cntp_cval_el0, val);// Counter-timer Physical Timer CompareValue Register
		break;
	case GENERIC_TIMER_REG_VIRT_CVAL:
		msr(cntv_cval_el0, val);// Counter-timer Virtual Timer CompareValue Register
		break;
	case GENERIC_TIMER_REG_VIRT_OFF:
		msr(cntvoff_el2, val);// Counter-timer Virtual Offset Register
		break;
	default:
		vmm_panic("Trying to write invalid generic-timer register\n");
	}

	isb();
}

static inline u64 generic_timer_reg_read64(int reg)
{
	u64 val;

	switch (reg) {
	case GENERIC_TIMER_REG_HYP_CVAL:
		val = mrs(cnthp_cval_el2);// Counter-timer Physical Timer CompareValue Register (EL2)
		break;
	case GENERIC_TIMER_REG_PHYS_CVAL:
		val = mrs(cntp_cval_el0);// Counter-timer Physical Timer CompareValue Register
		break;
	case GENERIC_TIMER_REG_VIRT_CVAL:
		val = mrs(cntv_cval_el0);// Counter-timer Virtual Timer CompareValue Register
		break;
	case GENERIC_TIMER_REG_VIRT_OFF:
		val = mrs(cntvoff_el2);// Counter-timer Virtual Offset Register
		break;
	default:
		vmm_panic("Trying to read invalid generic-timer register\n");
	}

	return val;
}

#define HAVE_GENERIC_TIMER_REGS_SAVE

void generic_timer_regs_save(void *cntx);

#define HAVE_GENERIC_TIMER_REGS_RESTORE

void generic_timer_regs_restore(void *cntx);

#endif	/* __CPU_GENERIC_TIMER_H__ */
