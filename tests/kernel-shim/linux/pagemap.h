/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal <linux/pagemap.h> shim. Defines just the constants and
 * forward decls that XDR helpers reference. Real page operations
 * are stubbed in tests/stubs/esunrpc_xdr_stubs.c. */
#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <linux/types.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE  4096UL
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#ifndef PAGE_MASK
#define PAGE_MASK  (~(PAGE_SIZE - 1))
#endif

struct page;

#endif /* _LINUX_PAGEMAP_H */
