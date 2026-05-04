// SPDX-License-Identifier: GPL-2.0
/*
 * kernel_stubs.c — userspace implementations of kernel infrastructure
 * functions that the shim *declares* (in headers) but doesn't define.
 *
 * Most kernel-API shims in tests/kernel-shim/linux/ are header-only
 * (pthread_mutex wrappers, atomic_t macros, etc.). This file exists
 * for symbols that need a real .c definition — currently empty
 * because the Phase-1 POC doesn't need any. Kept around so adding
 * one later doesn't require Makefile changes.
 *
 * Examples of what would land here in future phases:
 *   - schedule_timeout() backed by usleep
 *   - register_kthread(...) backed by pthread_create
 *   - workqueue impl (queue_work, flush_work)
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Empty translation unit — but force a non-empty .o so the Makefile
 * rule never produces a zero-byte object that some toolchains fuss
 * about. */
const char kernel_stubs_marker[] = "enfs userspace test kernel_stubs";

/* kstrdup: kernel string-duplicate. Userspace just defers to libc. */
char *kstrdup(const char *s, unsigned int gfp)
{
    (void)gfp;
    return s ? strdup(s) : NULL;
}

/* strscpy: kernel-style truncating copy. Returns the number of bytes
 * copied (excluding NUL) on success, or -E2BIG if truncated. */
long strscpy(char *dst, const char *src, size_t count)
{
    size_t i;
    if (count == 0) return -7L;     /* -E2BIG */
    for (i = 0; i + 1 < count && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return src[i] ? -7L : (long)i;
}
