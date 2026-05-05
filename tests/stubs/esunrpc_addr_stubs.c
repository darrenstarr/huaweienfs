// SPDX-License-Identifier: GPL-2.0
/*
 * esunrpc_addr_stubs.c — kernel-API stubs for vendor/esunrpc/net/
 * esunrpc/addr.c.
 *
 * The SUT calls a handful of kernel helpers that we re-implement
 * on libc primitives:
 *
 *   in4_pton / in6_pton — text-to-binary IP parsers
 *   kstrtou8            — text-to-byte parser used by the universal-
 *                         address port decoder
 *   kstrdup             — heap-allocate + copy
 *   strlcat             — bounded string concat
 *
 * The IPv6 scope-id parsing branch in addr.c walks net_device
 * structures via dev_get_by_name_rcu et al.; tests do not exercise
 * that path. Stubs return "not found" so any accidental hit fails
 * cleanly instead of crashing.
 */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <arpa/inet.h>

/* in4_pton / in6_pton: kernel signature is
 *   int in4_pton(const char *src, int srclen, u8 *dst,
 *                int delim, const char **end);
 * Returns 1 on success, 0 on failure. */
int in4_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end)
{
    /* Compute effective length up to delim (or srclen if -1). */
    int len = srclen;
    if (len < 0)
        len = (int)strlen(src);
    /* Find delimiter or end. */
    int dlen = 0;
    while (dlen < len && src[dlen] != (delim == -1 ? '\0' : (char)delim))
        dlen++;
    /* Copy into a NUL-terminated buffer, then call libc inet_pton. */
    char buf[64];
    if (dlen >= (int)sizeof(buf))
        return 0;
    memcpy(buf, src, dlen);
    buf[dlen] = '\0';
    if (inet_pton(AF_INET, buf, dst) != 1)
        return 0;
    if (end)
        *end = src + dlen;
    return 1;
}

int in6_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end)
{
    int len = srclen;
    if (len < 0)
        len = (int)strlen(src);
    int dlen = 0;
    while (dlen < len && src[dlen] != (delim == -1 ? '\0' : (char)delim))
        dlen++;
    char buf[64];
    if (dlen >= (int)sizeof(buf))
        return 0;
    memcpy(buf, src, dlen);
    buf[dlen] = '\0';
    if (inet_pton(AF_INET6, buf, dst) != 1)
        return 0;
    if (end)
        *end = src + dlen;
    return 1;
}

/* kstrtou8: parse a NUL- or '\0'-terminated decimal string into
 * a u8. Returns 0 on success, negative errno on failure. The
 * sunrpc addr.c calls it with the substring already isolated. */
int kstrtou8(const char *s, unsigned int base, unsigned char *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, base ? base : 10);
    if (end == s || *end != '\0')
        return -22; /* -EINVAL */
    if (v > 255)
        return -34; /* -ERANGE */
    *out = (unsigned char)v;
    return 0;
}

/* kstrdup is defined in tests/stubs/kernel_stubs.c. */

/* strlcat: bounded concat returning total intended length. */
size_t strlcat(char *dst, const char *src, size_t size)
{
    size_t dlen = strnlen(dst, size);
    size_t slen = strlen(src);
    if (dlen == size)
        return size + slen;
    if (slen < size - dlen) {
        memcpy(dst + dlen, src, slen + 1);
    } else {
        memcpy(dst + dlen, src, size - dlen - 1);
        dst[size - 1] = '\0';
    }
    return dlen + slen;
}

/* Stubs for the IPv6 scope-id / net-device branch (untested here). */
struct net;
struct net_device;
struct net_device *dev_get_by_name_rcu(struct net *net, const char *name)
{ (void)net; (void)name; return NULL; }
struct net_device *dev_get_by_name(struct net *net, const char *name)
{ (void)net; (void)name; return NULL; }
void dev_put(struct net_device *d) { (void)d; }
unsigned int kstrtouint(const char *s, unsigned int base, unsigned int *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, base ? base : 10);
    if (end == s || *end != '\0') return -22;
    *out = (unsigned int)v;
    return 0;
}
int kstrtou32(const char *s, unsigned int base, unsigned int *out)
{
    return kstrtouint(s, base, out);
}

/* IPv6 address-property predicates: ntop6_noscopeid uses these
 * to pick the printf format. Implementations match RFC 4291. */
struct in6_addr;
int ipv6_addr_any(const void *a)
{
    const unsigned char *p = (const unsigned char *)a;
    for (int i = 0; i < 16; i++)
        if (p[i] != 0) return 0;
    return 1;
}
int ipv6_addr_loopback(const void *a)
{
    const unsigned char *p = (const unsigned char *)a;
    for (int i = 0; i < 15; i++)
        if (p[i] != 0) return 0;
    return p[15] == 1;
}
int ipv6_addr_v4mapped(const void *a)
{
    /* ::ffff:0:0/96 */
    const unsigned char *p = (const unsigned char *)a;
    for (int i = 0; i < 10; i++)
        if (p[i] != 0) return 0;
    return p[10] == 0xff && p[11] == 0xff;
}

/* Reset hook — addr stubs hold no mutable state. */
void stub_reset_all(void) { }
