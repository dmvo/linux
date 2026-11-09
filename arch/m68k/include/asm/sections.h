/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _M68K_SECTIONS_H
#define _M68K_SECTIONS_H

#include <linux/types.h>

#ifdef CONFIG_ROSCO_M68K
static inline bool __is_kernel_text(unsigned long addr);
static inline bool __is_kernel(unsigned long addr);
#define __is_kernel_text __is_kernel_text
#define __is_kernel __is_kernel
#endif

#include <asm-generic/sections.h>

#ifdef CONFIG_ROSCO_M68K
/* Weak bounds are zero in the ordinary RAM link, set by the split linker. */
extern char __xiptext_start[] __weak, __xiptext_end[] __weak;

static inline bool __is_kernel_text(unsigned long addr)
{
	return (addr >= (unsigned long)_stext && addr < (unsigned long)_etext) ||
	       (addr >= (unsigned long)__xiptext_start &&
		addr < (unsigned long)__xiptext_end);
}

static inline bool __is_kernel(unsigned long addr)
{
	return __is_kernel_text(addr) ||
	       (addr >= (unsigned long)_stext && addr < (unsigned long)_end) ||
	       (addr >= (unsigned long)__init_begin &&
		addr < (unsigned long)__init_end);
}
#endif

#endif /* _M68K_SECTIONS_H */
