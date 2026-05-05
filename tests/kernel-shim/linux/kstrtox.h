/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <linux/kstrtox.h>. Declarations for the
 * kstrto* family of text-to-integer parsers. Stubs in
 * tests/stubs/esunrpc_addr_stubs.c implement them on libc strtoul. */
#ifndef _LINUX_KSTRTOX_H
#define _LINUX_KSTRTOX_H

#include <linux/types.h>

int kstrtou8(const char *s, unsigned int base, unsigned char *out);
unsigned int kstrtouint(const char *s, unsigned int base, unsigned int *out);

#endif /* _LINUX_KSTRTOX_H */
