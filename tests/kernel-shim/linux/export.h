/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <linux/export.h>. EXPORT_SYMBOL_* are no-ops
 * in tests — symbols are visible by virtue of being in the same
 * binary as the test runner. */
#ifndef _LINUX_EXPORT_H
#define _LINUX_EXPORT_H

#ifndef EXPORT_SYMBOL
#define EXPORT_SYMBOL(s)
#endif
#ifndef EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL_GPL(s)
#endif
#ifndef EXPORT_SYMBOL_NS
#define EXPORT_SYMBOL_NS(s, ns)
#endif
#ifndef EXPORT_SYMBOL_NS_GPL
#define EXPORT_SYMBOL_NS_GPL(s, ns)
#endif

#endif /* _LINUX_EXPORT_H */
