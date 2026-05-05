/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <esunrpc/clnt.h>. Forward declarations only —
 * sufficient to compile esunrpc files that touch the type but not
 * the layout. Tests requiring full struct layouts should add fields
 * here as needed (mirror the corresponding tests/kernel-shim/linux/
 * sunrpc/clnt.h pattern). */
#ifndef _ESUNRPC_CLNT_H
#define _ESUNRPC_CLNT_H

#include <linux/types.h>
#include <esunrpc/timer.h>

struct esunrpc_rpc_clnt;
struct esunrpc_rpc_xprt;

#endif /* _ESUNRPC_CLNT_H */
