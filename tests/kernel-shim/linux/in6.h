/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/in6.h>.
 *
 * sockaddr_in6 / in6_addr come from netinet/in.h. glibc names the
 * union inside in6_addr `__in6_u` with member `__u6_addr32`; the
 * kernel UAPI uses `in6_u` / `u6_addr32`. Add the kernel names as
 * macro aliases so source files written for the kernel API compile
 * unchanged here.
 */
#ifndef _LINUX_IN6_H
#define _LINUX_IN6_H

#include <netinet/in.h>

#define in6_u       __in6_u
#define u6_addr8    __u6_addr8
#define u6_addr16   __u6_addr16
#define u6_addr32   __u6_addr32

#endif /* _LINUX_IN6_H */
