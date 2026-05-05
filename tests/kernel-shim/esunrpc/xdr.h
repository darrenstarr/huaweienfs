/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <esunrpc/xdr.h>. Mirrors the layout of the
 * real header for the subset xdr.c actually depends on. */
#ifndef _ESUNRPC_XDR_H
#define _ESUNRPC_XDR_H

#include <linux/types.h>

#define XDR_UNIT      sizeof(__be32)
#define XDR_QUADLEN(l) (((l) + 3) >> 2)

struct xdr_netobj {
    unsigned int   len;
    unsigned char *data;
};

/* Subset of struct iovec since linux/uio.h pulls in glibc and
 * collides. xdr_buf uses .iov_base + .iov_len. */
struct kvec {
    void  *iov_base;
    size_t iov_len;
};

struct xdr_buf {
    struct kvec       head[1];
    struct kvec       tail[1];
    struct page     **pages;        /* untested in userspace */
    struct bio_vec   *bvec;          /* untested in userspace */
    unsigned int      page_base;
    unsigned int      page_len;
    unsigned int      flags;
    unsigned int      buflen;       /* total buffer length */
    unsigned int      len;          /* in-use length */
};

#define XDRBUF_READ          0x01
#define XDRBUF_WRITE         0x02
#define XDRBUF_SPARSE_PAGES  0x04

struct rpc_rqst;

/* For xdr_decode_array2 / xdr_encode_array2 — describes a flat
 * array embedded in an XDR stream. We keep the layout matching
 * upstream so xdr.c compiles. */
typedef int (*xdr_xcode_elem_t)(struct xdr_array2_desc *desc, void *elem);
struct xdr_array2_desc {
    unsigned int        elem_size;
    unsigned int        array_len;
    unsigned int        array_maxlen;
    xdr_xcode_elem_t    xcode;
};

struct xdr_stream {
    __be32          *p;
    struct xdr_buf  *buf;
    __be32          *end;
    struct kvec     *iov;
    struct kvec      scratch;
    struct page    **page_ptr;
    void            *page_kaddr;
    unsigned int     nwords;
    struct rpc_rqst *rqst;
};

extern void  esunrpc_xdr_init_encode(struct xdr_stream *xdr,
                                     struct xdr_buf *buf,
                                     __be32 *p, struct rpc_rqst *rqst);
extern void  esunrpc_xdr_init_decode(struct xdr_stream *xdr,
                                     struct xdr_buf *buf,
                                     __be32 *p, struct rpc_rqst *rqst);
extern __be32 *esunrpc_xdr_reserve_space(struct xdr_stream *xdr,
                                          size_t nbytes);
extern __be32 *esunrpc_xdr_inline_decode(struct xdr_stream *xdr,
                                          size_t nbytes);
extern unsigned int esunrpc_xdr_stream_pos(const struct xdr_stream *xdr);
extern void  esunrpc_xdr_terminate_string(const struct xdr_buf *buf,
                                          const u32 len);

extern __be32 *esunrpc_xdr_encode_opaque_fixed(__be32 *p, const void *ptr,
                                                unsigned int nbytes);
extern __be32 *esunrpc_xdr_encode_opaque(__be32 *p, const void *ptr,
                                          unsigned int nbytes);
extern __be32 *esunrpc_xdr_encode_string(__be32 *p, const char *str);
extern __be32 *esunrpc_xdr_encode_netobj(__be32 *p,
                                          const struct xdr_netobj *obj);

/* In the production xdr.h xdr_encode_array is a static inline
 * delegating to esunrpc_xdr_encode_opaque. We provide it as an
 * extern in the userspace shim — the stub body lives in
 * tests/stubs/esunrpc_xdr_stubs.c so the SUT picks up the correct
 * prototype (otherwise gcc assumes int and sign-extends the
 * returned __be32 *). */
extern __be32 *xdr_encode_array(__be32 *p, const void *src, unsigned int n);

#endif /* _ESUNRPC_XDR_H */
