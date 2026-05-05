/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal userspace shim for <net/ipv6.h>. addr.c uses ipv6_addr_*
 * inline helpers (which we recreate here from libc primitives) and
 * IFA_F_* constants. We deliberately leave out scope-id/netdev
 * helpers since their tests need real net device infrastructure. */
#ifndef _NET_IPV6_H
#define _NET_IPV6_H

#include <linux/in6.h>

/* Subset of inet6_ifaddr flags consumed by scope-id parsing.
 * Tests don't exercise that path, so the values are placeholders. */
#define IFA_F_TEMPORARY      0x01

/* IPv6 address-type bits used by addr.c branches. */
#define IPV6_ADDR_LOOPBACK   0x0010U
#define IPV6_ADDR_LINKLOCAL  0x0020U
#define IPV6_ADDR_SITELOCAL  0x0040U
#define IPV6_ADDR_MULTICAST  0x0002U

/* No-op "is link-local" / "is loopback" — the addr.c paths we test
 * never call into these. Provided as inline stubs so the include
 * chain resolves. */
static inline int ipv6_addr_type(const struct in6_addr *a)
{ (void)a; return 0; }

/* Minimal struct net_device — addr.c reads ->ifindex on the
 * scope-id parse branch. Tests never reach it (dev_get_by_name
 * returns NULL); stub keeps compiler happy. */
struct net_device {
    int ifindex;
};

#endif /* _NET_IPV6_H */
