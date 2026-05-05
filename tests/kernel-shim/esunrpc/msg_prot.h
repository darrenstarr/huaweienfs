/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <esunrpc/msg_prot.h>. Just the maxlen
 * constants used by addr.c. */
#ifndef _ESUNRPC_MSG_PROT_H
#define _ESUNRPC_MSG_PROT_H

#define RPCBIND_MAXUADDRLEN   (56u)   /* "fe80::1234%eth0.65535.65535" */
/* sizeof(".255.255") == 9 — matches upstream sunrpc msg_prot.h. */
#define RPCBIND_MAXUADDRPLEN  (sizeof(".255.255"))

#endif /* _ESUNRPC_MSG_PROT_H */
