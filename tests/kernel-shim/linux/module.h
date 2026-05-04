/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/module.h>.
 *
 * Drops MODULE_* / EXPORT_SYMBOL* declarations to no-ops so source
 * containing them links cleanly in userspace.
 */
#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/compiler.h>

#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_VERSION(x)
#define MODULE_ALIAS(x)
#define MODULE_PARM_DESC(p, d)

#define EXPORT_SYMBOL(s)
#define EXPORT_SYMBOL_GPL(s)
#define EXPORT_SYMBOL_NS(s, ns)
#define EXPORT_SYMBOL_NS_GPL(s, ns)

#define module_init(fn)   /* not invoked from tests */
#define module_exit(fn)   /* not invoked from tests */

#define THIS_MODULE       ((void *)0)

#endif /* _LINUX_MODULE_H */
