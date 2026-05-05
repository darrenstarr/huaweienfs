/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal <linux/bvec.h> shim. struct bio_vec only — used by
 * xdr.c's bvec helpers. */
#ifndef _LINUX_BVEC_H
#define _LINUX_BVEC_H

#include <linux/types.h>

struct page;
struct bio_vec {
    struct page *bv_page;
    unsigned int bv_len;
    unsigned int bv_offset;
};

#endif /* _LINUX_BVEC_H */
