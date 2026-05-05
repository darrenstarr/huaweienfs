/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal <asm/param.h> for userspace tests. HZ is the only thing
 * compiled SUTs use from this header. */
#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifndef HZ
#define HZ 1000UL
#endif

#endif /* _ASM_PARAM_H */
