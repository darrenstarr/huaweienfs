/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/in.h> — pulls in standard IPv4 sockaddr
 * types via the system headers and aliases the kernel-style names.
 */
#ifndef _LINUX_IN_H
#define _LINUX_IN_H

#include <netinet/in.h>
/* sockaddr_in / in_addr / etc. come from <netinet/in.h>. The kernel
 * names match the userspace names for these. */

#endif /* _LINUX_IN_H */
