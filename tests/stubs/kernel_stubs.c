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

/* This is the kernel_stubs.c TU — undef the snprintf macro we
 * install in enfs_preempt.h so this file's own calls to snprintf
 * resolve to the libc symbol, not infinite-recurse via our wrapper. */
#define ENFS_KERNEL_STUBS_INTERNAL 1

/* enfs_test_snprintf: snprintf wrapper that recognises kernel-only
 * format specifiers like %pI4 (IPv4) and %pI6 / %pI6c (IPv6, the
 * latter compressed). Used by sunrpc/addr.c et al. via the macro
 * override in <linux/kernel.h>.
 *
 * Strategy: scan fmt for "%pI4" / "%pI6" / "%pI6c"; for each match,
 * emit the formatted address, advance the buffer pointer, then
 * fall through to libc vsnprintf for the remaining segment. Repeat.
 *
 * Returns the kernel-style total bytes that would have been written
 * (libc's vsnprintf behaviour). The kernel returns the actual count
 * written, which differs only when the buffer overflows; addr.c
 * doesn't depend on that distinction.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <arpa/inet.h>

/* Avoid the macro override here — we ARE the override. */
#undef snprintf

int enfs_test_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    char tmp[256];
    char tmpfmt[256];
    va_list ap;
    int total = 0;
    const char *p = fmt;

    /* Scan fmt; whenever we see %pI4 / %pI6 / %pI6c, splice in our
     * own rendering. Otherwise, copy the segment into tmpfmt and
     * pass through to vsnprintf. */
    va_start(ap, fmt);

    /* Walk the format string in chunks separated by IPv6/4 pointer
     * specifiers. Each chunk is forwarded to vsnprintf with its own
     * captured va_arg subsequence — so the iteration here matches
     * the order of args the caller supplied. */
    char *out = buf;
    size_t remain = size;
    while (*p) {
        const char *seg = p;
        while (*p && *p != '%') p++;
        /* Copy any non-% prefix verbatim. */
        if (p != seg) {
            size_t n = p - seg;
            if (remain > 1) {
                size_t copy = n < remain - 1 ? n : remain - 1;
                memcpy(out, seg, copy);
                out += copy;
                remain -= copy;
                *out = '\0';
            }
            total += n;
        }
        if (!*p) break;
        /* p == '%'. Inspect the next chars. */
        if (p[1] == 'p' && p[2] == 'I' && p[3] == '4') {
            const void *addr = va_arg(ap, const void *);
            int n = snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u",
                             ((const unsigned char *)addr)[0],
                             ((const unsigned char *)addr)[1],
                             ((const unsigned char *)addr)[2],
                             ((const unsigned char *)addr)[3]);
            if (remain > 1) {
                int copy = n < (int)remain - 1 ? n : (int)remain - 1;
                memcpy(out, tmp, copy);
                out += copy;
                remain -= copy;
                *out = '\0';
            }
            total += n;
            p += 4;
        } else if (p[1] == 'p' && p[2] == 'I' && p[3] == '6') {
            const void *addr = va_arg(ap, const void *);
            char addrbuf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, addr, addrbuf, sizeof(addrbuf));
            int n = (int)strlen(addrbuf);
            if (remain > 1) {
                int copy = n < (int)remain - 1 ? n : (int)remain - 1;
                memcpy(out, addrbuf, copy);
                out += copy;
                remain -= copy;
                *out = '\0';
            }
            total += n;
            p += 4;
            if (*p == 'c') p++;   /* %pI6c — same output for our purposes */
        } else {
            /* Any other format spec: hand the next chunk to libc.
             * We need to extract just this single conversion so the
             * va_arg cursor stays in sync. Find the conversion char. */
            const char *q = p;
            do { q++; } while (*q && !strchr("diouxXeEfgGcspn%", *q));
            if (!*q) {
                /* Malformed format; copy literally. */
                size_t n = q - p;
                if (remain > 1) {
                    size_t copy = n < remain - 1 ? n : remain - 1;
                    memcpy(out, p, copy);
                    out += copy;
                    remain -= copy;
                    *out = '\0';
                }
                total += n;
                p = q;
                continue;
            }
            size_t flen = (q - p) + 1;
            memcpy(tmpfmt, p, flen);
            tmpfmt[flen] = '\0';
            /* Special case: '%' itself takes no arg. */
            int n;
            if (*q == '%') {
                n = snprintf(tmp, sizeof(tmp), "%%");
            } else if (*q == 'd' || *q == 'i') {
                int v = va_arg(ap, int);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            } else if (*q == 'u' || *q == 'o' || *q == 'x' || *q == 'X') {
                unsigned int v = va_arg(ap, unsigned int);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            } else if (*q == 's') {
                const char *v = va_arg(ap, const char *);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            } else if (*q == 'p') {
                const void *v = va_arg(ap, const void *);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            } else if (*q == 'c') {
                int v = va_arg(ap, int);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            } else {
                /* float/double/etc — just consume a double */
                double v = va_arg(ap, double);
                n = snprintf(tmp, sizeof(tmp), tmpfmt, v);
            }
            if (remain > 1) {
                int copy = n < (int)remain - 1 ? n : (int)remain - 1;
                memcpy(out, tmp, copy);
                out += copy;
                remain -= copy;
                *out = '\0';
            }
            total += n;
            p = q + 1;
        }
    }

    va_end(ap);
    if (size > 0 && remain == size) buf[0] = '\0'; /* nothing written */
    return total;
}
