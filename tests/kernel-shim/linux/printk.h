/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/printk.h>.
 *
 * Routes pr_info / pr_err / pr_warn / pr_debug to fprintf(stderr) so
 * test runs surface diagnostic output. Tests can capture this via
 * stderr if they want to assert on log content (none currently do).
 */
#ifndef _LINUX_PRINTK_H
#define _LINUX_PRINTK_H

#include <stdio.h>

#define KERN_INFO    ""
#define KERN_ERR     ""
#define KERN_WARNING ""
#define KERN_DEBUG   ""
#define KERN_NOTICE  ""

#define printk(fmt, ...)    fprintf(stderr, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)   fprintf(stderr, "[info] " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)    fprintf(stderr, "[err]  " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)   fprintf(stderr, "[warn] " fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)  ((void)0)
#define pr_notice(fmt, ...) fprintf(stderr, "[ntc]  " fmt, ##__VA_ARGS__)

#endif /* _LINUX_PRINTK_H */
