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

/* Wide kernel-API stubs. xdr.c references many symbols even on the
 * code paths the tests don't reach; provide stubs so the link
 * resolves. */
#include <arpa/inet.h>

unsigned int cpu_to_be32(unsigned int x) { return htonl(x); }
unsigned int be32_to_cpu(unsigned int x) { return ntohl(x); }

/* xdr_encode_array: in the real esunrpc/xdr.h this is a static
 * inline wrapper around esunrpc_xdr_encode_opaque. Our shim header
 * doesn't carry it, so do the same delegation here. */
__be32 *esunrpc_xdr_encode_opaque(__be32 *p, const void *ptr, unsigned int nbytes);
__be32 *xdr_encode_array(__be32 *p, const void *src, unsigned int n)
{ return esunrpc_xdr_encode_opaque(p, src, n); }

void *kmalloc_objs(unsigned int n, unsigned int sz, unsigned int gfp)
{ (void)n; (void)sz; (void)gfp; return NULL; }

void bvec_set_virt(struct bio_vec *bv, void *p, unsigned int len)
{ (void)bv; (void)p; (void)len; }

unsigned int offset_in_page(unsigned long addr) { return addr & 4095; }

void pr_warn_once(const char *fmt, ...) { (void)fmt; }

struct page *alloc_page(unsigned int gfp) { (void)gfp; return NULL; }

void WARN_ONCE(int cond, const char *fmt, ...) { (void)cond; (void)fmt; }

/* xdr.h has many static-inline helpers in production. Tests don't
 * include the full header here, so stub the ones the SUT calls. */
struct xdr_stream;
void xdr_reset_scratch_buffer(struct xdr_stream *xdr) { (void)xdr; }
void xdr_set_scratch_buffer(struct xdr_stream *xdr, void *p, unsigned int n)
{ (void)xdr; (void)p; (void)n; }
void xdr_commit_encode(struct xdr_stream *xdr) { (void)xdr; }
unsigned int xdr_align_size(unsigned int n) { return (n + 3) & ~3; }
unsigned int xdr_stream_remaining(const struct xdr_stream *xdr)
{ (void)xdr; return 0; }
unsigned int xdr_pad_size(unsigned int n) { return (4 - (n & 3)) & 3; }
int PageHighMem(struct page *p) { (void)p; return 0; }

/* xdr_buf_subsegment / xdr_buf_unwrap variants. */
struct xdr_buf;
unsigned int xdr_buf_subsegment(struct xdr_buf *buf, struct xdr_buf *sub,
                                 unsigned int base, unsigned int len)
{ (void)buf; (void)sub; (void)base; (void)len; return 0; }

/* Page kmap variants (older API); xdr.c uses these on some paths. */
void *kmap(struct page *p) { (void)p; return NULL; }
void kunmap(struct page *p) { (void)p; }

/* Scatter-gather list helpers. The XDR copy-from-buffer paths walk
 * an sg-list when handing data to crypto. Tests don't enter those
 * paths so the stubs only need to satisfy the linker. */
struct scatterlist;
void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{ (void)sgl; (void)nents; }
void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int len)
{ (void)sg; (void)buf; (void)len; }
void sg_set_page(struct scatterlist *sg, struct page *p,
                 unsigned int len, unsigned int off)
{ (void)sg; (void)p; (void)len; (void)off; }

/* Higher-level XDR helpers some xdr.c paths call. The auth-opaque
 * encode/decode wrappers thread through these; tests don't exercise
 * those wrappers so a benign no-op is fine. */
int xdr_stream_decode_opaque_inline(struct xdr_stream *xdr, void **ptr,
                                     size_t maxlen)
{ (void)xdr; (void)maxlen; if (ptr) *ptr = NULL; return -1; }

int xdr_stream_decode_u32(struct xdr_stream *xdr, uint32_t *ptr)
{ (void)xdr; if (ptr) *ptr = 0; return -1; }

int xdr_stream_encode_u32(struct xdr_stream *xdr, uint32_t v)
{ (void)xdr; (void)v; return -1; }

int xdr_stream_encode_opaque(struct xdr_stream *xdr, const void *p,
                              size_t len)
{ (void)xdr; (void)p; (void)len; return -1; }

/* Kernel string helper — duplicate up to len bytes, NUL-terminate. */
char *kmemdup_nul(const char *s, size_t len, unsigned int gfp)
{
    char *out;
    (void)gfp;
    if (!s) return NULL;
    out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

/* Reset hook. */
void stub_reset_all(void) { }
