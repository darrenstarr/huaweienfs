/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <linux/jiffies.h>. Only needs to declare the
 * `jiffies` global; tests/stubs/failover_time_stubs.c provides the
 * storage. */
#ifndef _LINUX_JIFFIES_H
#define _LINUX_JIFFIES_H

#include <linux/types.h>

extern unsigned long jiffies;

#endif /* _LINUX_JIFFIES_H */
