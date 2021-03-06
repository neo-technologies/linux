/*
 * Copyright (C) 2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * This header file contains the interface of the interrupt remapping code to
 * the x86 interrupt management code.
 */

#ifndef __X86_IRQ_REMAPPING_H
#define __X86_IRQ_REMAPPING_H

#include <linux/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/io_apic.h>

struct msi_msg;
struct irq_alloc_info;

#ifdef CONFIG_IRQ_REMAP

extern int irq_remapping_supported(void);
extern void set_irq_remapping_broken(void);
extern int irq_remapping_prepare(void);
extern int irq_remapping_enable(void);
extern void irq_remapping_disable(void);
extern int irq_remapping_reenable(int);
extern int irq_remap_enable_fault_handling(void);
extern void panic_if_irq_remap(const char *msg);

extern struct irq_domain *
irq_remapping_get_ir_irq_domain( struct irq_alloc_info *info);
extern struct irq_domain *
irq_remapping_get_irq_domain(struct irq_alloc_info *info);
extern void irq_remapping_print_chip(struct irq_data *data, struct seq_file *p);

/* Create PCI MSI/MSIx irqdomain, use @parent as the parent irqdomain. */
extern struct irq_domain *arch_create_msi_irq_domain(struct irq_domain *parent);

/* Get parent irqdomain for interrupt remapping irqdomain */
static inline struct irq_domain *arch_get_ir_parent_domain(void)
{
	return x86_vector_domain;
}

#else  /* CONFIG_IRQ_REMAP */

static inline int irq_remapping_supported(void) { return 0; }
static inline void set_irq_remapping_broken(void) { }
static inline int irq_remapping_prepare(void) { return -ENODEV; }
static inline int irq_remapping_enable(void) { return -ENODEV; }
static inline void irq_remapping_disable(void) { }
static inline int irq_remapping_reenable(int eim) { return -ENODEV; }
static inline int irq_remap_enable_fault_handling(void) { return -ENODEV; }

static inline void panic_if_irq_remap(const char *msg)
{
}

static inline struct irq_domain *
irq_remapping_get_ir_irq_domain(struct irq_alloc_info *info)
{
	return NULL;
}

static inline struct irq_domain *
irq_remapping_get_irq_domain(struct irq_alloc_info *info)
{
	return NULL;
}

#define	irq_remapping_print_chip	NULL
#endif /* CONFIG_IRQ_REMAP */
#endif /* __X86_IRQ_REMAPPING_H */
