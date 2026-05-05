// SPDX-License-Identifier: GPL-2.0
/*
 * esunrpc_xdr_stubs.c — kernel-API stubs for vendor/esunrpc/net/
 * esunrpc/xdr.c.
 *
 * The XDR helpers operate over xdr_buf which has a scatter-gather
 * pages section; tests only exercise the linear (head[0]) segment,
 * but the SUT still references page helpers we need to stub.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Page primitives — never actually used by the test paths. */
struct page;
struct page *alloc_pages(unsigned int gfp, unsigned int order)
{ (void)gfp; (void)order; return NULL; }
void __free_pages(struct page *p, unsigned int order)
{ (void)p; (void)order; }

void *kmap_local_page(struct page *p) { (void)p; return NULL; }
void kunmap_local(void *addr) { (void)addr; }
void *kmap_atomic(struct page *p) { (void)p; return NULL; }
void kunmap_atomic(void *addr) { (void)addr; }

/* page_address: used by some XDR pages walks. Tests don't reach
 * the page-data branch so any value is fine. */
void *page_address(struct page *p) { (void)p; return NULL; }

/* flush_dcache_page: cache flush; no-op in userspace. */
void flush_dcache_page(struct page *p) { (void)p; }

/* zero_user / zero_user_segments: zero subranges of a page. No-op
 * — tests don't rely on the page contents. */
void zero_user(struct page *p, unsigned int o, unsigned int n)
{ (void)p; (void)o; (void)n; }

/* PAGE_SIZE / PAGE_MASK / PAGE_SHIFT come from compat headers. */

/* clear_page: zero a 4KB page. */
void clear_page(void *p) { if (p) memset(p, 0, 4096); }

/* memcpy_to_page / from_page: scatter-gather copy stubs. */
void memcpy_from_page(void *dst, struct page *p, unsigned int o, unsigned int n)
{ (void)dst; (void)p; (void)o; (void)n; }
void memcpy_to_page(struct page *p, unsigned int o, const void *src, unsigned int n)
{ (void)p; (void)o; (void)src; (void)n; }

/* Reset hook. */
void stub_reset_all(void) { }
