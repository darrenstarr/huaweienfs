/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <esunrpc/addr.h>. Declarations of the
 * exported address-conversion API. */
#ifndef _ESUNRPC_ADDR_H
#define _ESUNRPC_ADDR_H

#include <linux/types.h>
#include <linux/socket.h>

struct net;

/* Maximum reasonable presentation forms for the address API. */
#ifndef RPC_MAX_ADDRBUFLEN
#define RPC_MAX_ADDRBUFLEN  (63u)
#endif

extern size_t esunrpc_rpc_ntop(const struct sockaddr *sap,
                               char *buf, const size_t buflen);
extern size_t esunrpc_rpc_pton(struct net *net, const char *buf,
                               const size_t buflen,
                               struct sockaddr *sap, const size_t salen);
extern size_t esunrpc_rpc_uaddr2sockaddr(struct net *net,
                                         const char *uaddr,
                                         const size_t uaddr_len,
                                         struct sockaddr *sap,
                                         const size_t salen);
extern char  *rpc_sockaddr2uaddr(const struct sockaddr *sap, gfp_t gfp);

#endif /* _ESUNRPC_ADDR_H */
