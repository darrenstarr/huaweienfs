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

/* ipv6_addr_type — return the address-type bitfield. Tests reach
 * the link-local branch when rendering scope ids. We classify the
 * minimum cases addr.c branches on. Real kernel implementation in
 * net/ipv6/addrconf.c is hundreds of lines; this is enough for the
 * tested rendering paths. */
static inline int ipv6_addr_type(const struct in6_addr *a)
{
    if (!a) return 0;
    /* Link-local: fe80::/10 — high byte 0xfe, second byte 0x80..0xbf. */
    if (a->s6_addr[0] == 0xfe && (a->s6_addr[1] & 0xc0) == 0x80)
        return IPV6_ADDR_LINKLOCAL;
    /* Loopback: ::1. */
    {
        static const unsigned char loopback[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
        int i, is_lb = 1;
        for (i = 0; i < 16; i++) if (a->s6_addr[i] != loopback[i]) { is_lb = 0; break; }
        if (is_lb) return IPV6_ADDR_LOOPBACK;
    }
    /* Multicast: ff00::/8. */
    if (a->s6_addr[0] == 0xff)
        return IPV6_ADDR_MULTICAST;
    return 0;
}

/* Minimal struct net_device — addr.c reads ->ifindex on the
 * scope-id parse branch. Tests never reach it (dev_get_by_name
 * returns NULL); stub keeps compiler happy. */
struct net_device {
    int ifindex;
};

#endif /* _NET_IPV6_H */
