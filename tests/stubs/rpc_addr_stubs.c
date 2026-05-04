// SPDX-License-Identifier: GPL-2.0
/*
 * rpc_addr_stubs.c — userspace implementations of the kernel's
 * rpc_pton / rpc_ntop / rpc_cmp_addr family from net/sunrpc/addr.c.
 *
 * The kernel versions wrap in4_pton/in6_pton (in lib/) which we can't
 * link against. The libc inet_pton/inet_ntop family is sufficient for
 * userspace tests; the only quirk is that kernel rpc_pton accepts
 * IPv6 in *both* `[2001:db8::1]` bracketed form AND bare
 * `2001:db8::1` form. We mirror that by stripping brackets first.
 */

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/sunrpc/addr.h>

/* Ignore net_ns — the userspace test harness has no such concept. */
size_t rpc_pton(struct net *net, const char *buf, const size_t buflen,
                struct sockaddr *sa_buf, const size_t salen)
{
    char tmp[64];
    size_t n;
    const char *src = buf;
    size_t copylen = buflen;

    (void)net;
    if (!buf || !sa_buf || buflen == 0 || buflen >= sizeof(tmp))
        return 0;

    /* Strip surrounding brackets for `[ipv6]` form. */
    if (buflen >= 2 && buf[0] == '[' && buf[buflen - 1] == ']') {
        src = buf + 1;
        copylen = buflen - 2;
    }
    memcpy(tmp, src, copylen);
    tmp[copylen] = '\0';

    /* Try IPv4 first (only addresses with a `.` and no `:`). */
    if (strchr(tmp, '.') && !strchr(tmp, ':')) {
        struct sockaddr_in *sin = (struct sockaddr_in *)sa_buf;
        if (salen < sizeof(*sin)) return 0;
        memset(sin, 0, sizeof(*sin));
        if (inet_pton(AF_INET, tmp, &sin->sin_addr) != 1) return 0;
        sin->sin_family = AF_INET;
        n = sizeof(*sin);
    } else if (strchr(tmp, ':')) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa_buf;
        if (salen < sizeof(*sin6)) return 0;
        memset(sin6, 0, sizeof(*sin6));
        if (inet_pton(AF_INET6, tmp, &sin6->sin6_addr) != 1) return 0;
        sin6->sin6_family = AF_INET6;
        n = sizeof(*sin6);
    } else {
        return 0;
    }
    return n;
}

size_t rpc_ntop(const struct sockaddr *sap, char *buf, const size_t buflen)
{
    if (!sap || !buf || buflen == 0) return 0;
    switch (sap->sa_family) {
    case AF_INET: {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sap;
        if (!inet_ntop(AF_INET, &sin->sin_addr, buf, buflen)) return 0;
        return strlen(buf);
    }
    case AF_INET6: {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sap;
        if (!inet_ntop(AF_INET6, &sin6->sin6_addr, buf, buflen)) return 0;
        return strlen(buf);
    }
    }
    return 0;
}

bool rpc_cmp_addr(const struct sockaddr *sap1, const struct sockaddr *sap2)
{
    if (!sap1 || !sap2 || sap1->sa_family != sap2->sa_family) return false;
    switch (sap1->sa_family) {
    case AF_INET: {
        const struct sockaddr_in *a = (const struct sockaddr_in *)sap1;
        const struct sockaddr_in *b = (const struct sockaddr_in *)sap2;
        return a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    case AF_INET6: {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)sap1;
        const struct sockaddr_in6 *b = (const struct sockaddr_in6 *)sap2;
        return memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
    }
    }
    return false;
}

bool rpc_cmp_addr_port(const struct sockaddr *sap1, const struct sockaddr *sap2)
{
    if (!rpc_cmp_addr(sap1, sap2)) return false;
    switch (sap1->sa_family) {
    case AF_INET:
        return ((const struct sockaddr_in *)sap1)->sin_port ==
               ((const struct sockaddr_in *)sap2)->sin_port;
    case AF_INET6:
        return ((const struct sockaddr_in6 *)sap1)->sin6_port ==
               ((const struct sockaddr_in6 *)sap2)->sin6_port;
    }
    return false;
}

/* Kernel in4_pton / in6_pton wrappers around libc inet_pton. The
 * kernel signature is `int (src, srclen, dst, delim, end)` returning
 * 1 on success; we ignore srclen/delim/end and forward to inet_pton. */
int in4_pton(const char *src, int srclen, unsigned char *dst, int delim,
             const char **end)
{
    char buf[INET_ADDRSTRLEN];
    int n;
    (void)delim; (void)end;
    if (!src || !dst) return 0;
    if (srclen < 0) srclen = (int)strlen(src);
    if ((size_t)srclen >= sizeof(buf)) return 0;
    memcpy(buf, src, srclen);
    buf[srclen] = '\0';
    n = inet_pton(AF_INET, buf, dst);
    return n == 1 ? 1 : 0;
}

int in6_pton(const char *src, int srclen, unsigned char *dst, int delim,
             const char **end)
{
    char buf[INET6_ADDRSTRLEN];
    int n;
    (void)delim; (void)end;
    if (!src || !dst) return 0;
    if (srclen < 0) srclen = (int)strlen(src);
    if ((size_t)srclen >= sizeof(buf)) return 0;
    memcpy(buf, src, srclen);
    buf[srclen] = '\0';
    n = inet_pton(AF_INET6, buf, dst);
    return n == 1 ? 1 : 0;
}
