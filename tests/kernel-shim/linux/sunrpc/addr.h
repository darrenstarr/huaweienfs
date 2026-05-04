/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/sunrpc/addr.h>.
 *
 * The real kernel header declares rpc_pton / rpc_ntop / rpc_cmp_addr
 * etc., which are wrappers around in4_pton/in6_pton internal to
 * net/sunrpc/addr.c. We can't link against those in userspace, so the
 * shim declares the same surface and the stubs (tests/stubs/) provide
 * userspace implementations using the libc inet_pton family.
 */
#ifndef _LINUX_SUNRPC_ADDR_H
#define _LINUX_SUNRPC_ADDR_H

#include <linux/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct net;

/* Parse a string address into sockaddr (sa_buf). Returns the number
 * of bytes written, or 0 on failure. Mirrors kernel rpc_pton's
 * signature. */
size_t rpc_pton(struct net *net, const char *buf, const size_t buflen,
                struct sockaddr *sa_buf, const size_t salen);

/* Format sockaddr as string. Returns chars written. */
size_t rpc_ntop(const struct sockaddr *sap, char *buf, const size_t buflen);

/* True if the two sockaddrs are the same family + same address. */
bool rpc_cmp_addr(const struct sockaddr *sap1, const struct sockaddr *sap2);

/* True if the two sockaddrs are the same family + same address + same
 * port. */
bool rpc_cmp_addr_port(const struct sockaddr *sap1, const struct sockaddr *sap2);

#endif /* _LINUX_SUNRPC_ADDR_H */
