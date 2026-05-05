/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_esunrpc_addr.c — unit tests for vendor/esunrpc/net/esunrpc/
 * addr.c (presentation/binary address conversion, renamed from
 * upstream sunrpc).
 *
 * The four exported entry points are pure data transformation:
 *
 *   esunrpc_rpc_ntop        sockaddr -> "192.0.2.10" / "2001:db8::1"
 *   esunrpc_rpc_pton        "192.0.2.10" / "2001:db8::1" -> sockaddr
 *   rpc_sockaddr2uaddr      sockaddr -> "192.0.2.10.0.111"
 *                           (RFC 5665 "universal address" form)
 *   esunrpc_rpc_uaddr2sockaddr  reverse
 *
 * No I/O, no kernel state — easy and important target. We deliberately
 * skip IPv6 scope-id paths (they walk netdev structures); a follow-up
 * issue tracks adding shim infra for those.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <esunrpc/addr.h>

/* Forward decl (static in production, exposed by -Dstatic=). */
char *rpc_sockaddr2uaddr(const struct sockaddr *sap, gfp_t gfp);

/* ============================================================ */
/* Helpers                                                       */
/* ============================================================ */

static struct sockaddr_in v4(const char *s, unsigned short port)
{
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, s, &a.sin_addr);
    return a;
}
static struct sockaddr_in6 v6(const char *s, unsigned short port)
{
    struct sockaddr_in6 a = { 0 };
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(port);
    inet_pton(AF_INET6, s, &a.sin6_addr);
    return a;
}

/* ============================================================ */
/* esunrpc_rpc_ntop — sockaddr -> string                         */
/* ============================================================ */

#define V4_NTOP_TEST(name, addr_str) \
    START_TEST(name) { \
        char buf[64]; \
        struct sockaddr_in a = v4(addr_str, 0); \
        size_t n = esunrpc_rpc_ntop((struct sockaddr *)&a, buf, sizeof(buf)); \
        ck_assert_uint_gt(n, 0); \
        ck_assert_str_eq(buf, addr_str); \
    } END_TEST

V4_NTOP_TEST(ntop_v4_192_0_2_10,    "192.0.2.10")
V4_NTOP_TEST(ntop_v4_192_0_2_1,     "192.0.2.1")
V4_NTOP_TEST(ntop_v4_192_0_2_254,   "192.0.2.254")
V4_NTOP_TEST(ntop_v4_198_51_100_5,  "198.51.100.5")
V4_NTOP_TEST(ntop_v4_203_0_113_99,  "203.0.113.99")
V4_NTOP_TEST(ntop_v4_10_0_0_1,      "10.0.0.1")
V4_NTOP_TEST(ntop_v4_172_16_0_1,    "172.16.0.1")
V4_NTOP_TEST(ntop_v4_192_168_1_1,   "192.168.1.1")
V4_NTOP_TEST(ntop_v4_8_8_8_8,       "8.8.8.8")
V4_NTOP_TEST(ntop_v4_1_1_1_1,       "1.1.1.1")
V4_NTOP_TEST(ntop_v4_127_0_0_1,     "127.0.0.1")
V4_NTOP_TEST(ntop_v4_zero,          "0.0.0.0")
V4_NTOP_TEST(ntop_v4_max,           "255.255.255.255")

#define V6_NTOP_TEST(name, addr_str) \
    START_TEST(name) { \
        char buf[64]; \
        struct sockaddr_in6 a = v6(addr_str, 0); \
        size_t n = esunrpc_rpc_ntop((struct sockaddr *)&a, buf, sizeof(buf)); \
        ck_assert_uint_gt(n, 0); \
        /* Compact form may differ slightly from input; verify by \
         * round-trip parse. */ \
        struct sockaddr_in6 round = { 0 }; \
        round.sin6_family = AF_INET6; \
        ck_assert_int_eq(inet_pton(AF_INET6, buf, &round.sin6_addr), 1); \
        ck_assert(memcmp(&round.sin6_addr, &a.sin6_addr, 16) == 0); \
    } END_TEST

V6_NTOP_TEST(ntop_v6_2001_db8_1,        "2001:db8::1")
V6_NTOP_TEST(ntop_v6_2001_db8_2_18,     "2001:db8:2::18")
V6_NTOP_TEST(ntop_v6_full_form,         "2001:0db8:0002:0000:0000:0000:0000:0011")
V6_NTOP_TEST(ntop_v6_loopback,          "::1")
V6_NTOP_TEST(ntop_v6_fc07,              "fc07:2::4:1")
V6_NTOP_TEST(ntop_v6_2620,              "2620:0:1234::5")

START_TEST(ntop_unsupported_family_returns_zero) {
    struct sockaddr_storage ss = { 0 };
    ss.ss_family = AF_UNIX;
    char buf[64];
    ck_assert_uint_eq(esunrpc_rpc_ntop((struct sockaddr *)&ss, buf, sizeof(buf)), 0);
} END_TEST

/* Note: the SUT's rpc_ntop4 uses snprintf which writes a truncated
 * result + NUL even for tiny buffers; it returns the *intended*
 * length, not 0. Document that with a correctness-of-truncation
 * test instead of a "returns 0" expectation. */
START_TEST(ntop_buffer_too_small_truncates_safely) {
    char buf[4] = { 0xff, 0xff, 0xff, 0xff };
    struct sockaddr_in a = v4("192.0.2.10", 0);
    esunrpc_rpc_ntop((struct sockaddr *)&a, buf, sizeof(buf));
    /* Output must be NUL-terminated within the buffer. */
    ck_assert_int_eq(buf[3], '\0');
} END_TEST

/* ============================================================ */
/* esunrpc_rpc_pton — string -> sockaddr                         */
/* ============================================================ */

#define V4_PTON_TEST(name, addr_str) \
    START_TEST(name) { \
        struct sockaddr_storage ss = { 0 }; \
        size_t n = esunrpc_rpc_pton(NULL, addr_str, strlen(addr_str), \
                                    (struct sockaddr *)&ss, sizeof(ss)); \
        ck_assert_uint_gt(n, 0); \
        ck_assert_int_eq(ss.ss_family, AF_INET); \
        struct sockaddr_in *sin = (struct sockaddr_in *)&ss; \
        struct in_addr expected; \
        ck_assert_int_eq(inet_pton(AF_INET, addr_str, &expected), 1); \
        ck_assert(memcmp(&sin->sin_addr, &expected, 4) == 0); \
    } END_TEST

V4_PTON_TEST(pton_v4_192_0_2_10,    "192.0.2.10")
V4_PTON_TEST(pton_v4_198_51_100_5,  "198.51.100.5")
V4_PTON_TEST(pton_v4_10_0_0_1,      "10.0.0.1")
V4_PTON_TEST(pton_v4_192_168_1_1,   "192.168.1.1")
V4_PTON_TEST(pton_v4_127_0_0_1,     "127.0.0.1")
V4_PTON_TEST(pton_v4_8_8_8_8,       "8.8.8.8")
V4_PTON_TEST(pton_v4_zero,          "0.0.0.0")
V4_PTON_TEST(pton_v4_max,           "255.255.255.255")

#define V6_PTON_TEST(name, addr_str) \
    START_TEST(name) { \
        struct sockaddr_storage ss = { 0 }; \
        size_t n = esunrpc_rpc_pton(NULL, addr_str, strlen(addr_str), \
                                    (struct sockaddr *)&ss, sizeof(ss)); \
        ck_assert_uint_gt(n, 0); \
        ck_assert_int_eq(ss.ss_family, AF_INET6); \
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss; \
        struct in6_addr expected; \
        ck_assert_int_eq(inet_pton(AF_INET6, addr_str, &expected), 1); \
        ck_assert(memcmp(&sin6->sin6_addr, &expected, 16) == 0); \
    } END_TEST

V6_PTON_TEST(pton_v6_2001_db8_1,    "2001:db8::1")
V6_PTON_TEST(pton_v6_2001_db8_2_18, "2001:db8:2::18")
V6_PTON_TEST(pton_v6_full_form,     "2001:0db8:0002:0000:0000:0000:0000:0011")
V6_PTON_TEST(pton_v6_loopback,      "::1")
V6_PTON_TEST(pton_v6_fc07,          "fc07:2::4:1")

#define PTON_REJECT_TEST(name, str) \
    START_TEST(name) { \
        struct sockaddr_storage ss = { 0 }; \
        size_t n = esunrpc_rpc_pton(NULL, str, strlen(str), \
                                    (struct sockaddr *)&ss, sizeof(ss)); \
        ck_assert_uint_eq(n, 0); \
    } END_TEST

PTON_REJECT_TEST(pton_reject_garbage,            "not-an-ip-address")
PTON_REJECT_TEST(pton_reject_v4_too_many_octets, "192.0.2.10.5")
PTON_REJECT_TEST(pton_reject_v4_huge_octet,      "192.0.2.99999")
PTON_REJECT_TEST(pton_reject_v4_negative,        "192.0.2.-1")
PTON_REJECT_TEST(pton_reject_v6_too_many_colons, "1:2:3:4:5:6:7:8:9")
PTON_REJECT_TEST(pton_reject_v6_garbage,         "2001:db8::xyz")

/* ============================================================ */
/* ntop ↔ pton round-trip                                        */
/* ============================================================ */

#define V4_ROUNDTRIP_TEST(name, addr_str) \
    START_TEST(name) { \
        struct sockaddr_in a = v4(addr_str, 0); \
        char buf[64]; \
        size_t n = esunrpc_rpc_ntop((struct sockaddr *)&a, buf, sizeof(buf)); \
        ck_assert_uint_gt(n, 0); \
        struct sockaddr_storage rt = { 0 }; \
        size_t m = esunrpc_rpc_pton(NULL, buf, strlen(buf), \
                                    (struct sockaddr *)&rt, sizeof(rt)); \
        ck_assert_uint_gt(m, 0); \
        ck_assert_int_eq(rt.ss_family, AF_INET); \
        ck_assert(memcmp(&((struct sockaddr_in *)&rt)->sin_addr, \
                         &a.sin_addr, 4) == 0); \
    } END_TEST

V4_ROUNDTRIP_TEST(roundtrip_v4_a, "192.0.2.10")
V4_ROUNDTRIP_TEST(roundtrip_v4_b, "10.20.30.40")
V4_ROUNDTRIP_TEST(roundtrip_v4_c, "1.2.3.4")
V4_ROUNDTRIP_TEST(roundtrip_v4_d, "127.0.0.1")
V4_ROUNDTRIP_TEST(roundtrip_v4_e, "255.255.255.254")

#define V6_ROUNDTRIP_TEST(name, addr_str) \
    START_TEST(name) { \
        struct sockaddr_in6 a = v6(addr_str, 0); \
        char buf[64]; \
        size_t n = esunrpc_rpc_ntop((struct sockaddr *)&a, buf, sizeof(buf)); \
        ck_assert_uint_gt(n, 0); \
        struct sockaddr_storage rt = { 0 }; \
        size_t m = esunrpc_rpc_pton(NULL, buf, strlen(buf), \
                                    (struct sockaddr *)&rt, sizeof(rt)); \
        ck_assert_uint_gt(m, 0); \
        ck_assert_int_eq(rt.ss_family, AF_INET6); \
        ck_assert(memcmp(&((struct sockaddr_in6 *)&rt)->sin6_addr, \
                         &a.sin6_addr, 16) == 0); \
    } END_TEST

V6_ROUNDTRIP_TEST(roundtrip_v6_a, "2001:db8::1")
V6_ROUNDTRIP_TEST(roundtrip_v6_b, "fc07:2::4:1")
V6_ROUNDTRIP_TEST(roundtrip_v6_c, "::1")
V6_ROUNDTRIP_TEST(roundtrip_v6_d, "2620:0:1234::5")
V6_ROUNDTRIP_TEST(roundtrip_v6_e, "fd00::abcd")

/* ============================================================ */
/* rpc_sockaddr2uaddr — produces RFC 5665 universal addresses    */
/* ============================================================ */

START_TEST(uaddr_v4_format) {
    /* Universal addr: "h1.h2.h3.h4.p1.p2" where p = (p1<<8)|p2. */
    struct sockaddr_in a = v4("192.0.2.10", 2049);
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&a, 0);
    ck_assert_ptr_nonnull(u);
    /* port 2049 = 0x0801 = 8.1 */
    ck_assert_str_eq(u, "192.0.2.10.8.1");
    free(u);
} END_TEST

START_TEST(uaddr_v4_port_111) {
    struct sockaddr_in a = v4("127.0.0.1", 111);
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&a, 0);
    ck_assert_ptr_nonnull(u);
    /* port 111 = 0.111 */
    ck_assert_str_eq(u, "127.0.0.1.0.111");
    free(u);
} END_TEST

START_TEST(uaddr_v4_port_zero) {
    struct sockaddr_in a = v4("10.0.0.1", 0);
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&a, 0);
    ck_assert_ptr_nonnull(u);
    ck_assert_str_eq(u, "10.0.0.1.0.0");
    free(u);
} END_TEST

START_TEST(uaddr_v4_port_max) {
    struct sockaddr_in a = v4("10.0.0.1", 65535);
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&a, 0);
    ck_assert_ptr_nonnull(u);
    ck_assert_str_eq(u, "10.0.0.1.255.255");
    free(u);
} END_TEST

START_TEST(uaddr_v6_format) {
    struct sockaddr_in6 a = v6("2001:db8::1", 2049);
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&a, 0);
    ck_assert_ptr_nonnull(u);
    ck_assert_str_eq(u, "2001:db8::1.8.1");
    free(u);
} END_TEST

START_TEST(uaddr_unsupported_family_returns_NULL) {
    struct sockaddr_storage ss = { 0 };
    ss.ss_family = AF_UNIX;
    char *u = rpc_sockaddr2uaddr((struct sockaddr *)&ss, 0);
    ck_assert_ptr_null(u);
} END_TEST

/* ============================================================ */
/* esunrpc_rpc_uaddr2sockaddr — reverse                          */
/* ============================================================ */

START_TEST(uaddr2sock_v4_192_0_2_10_2049) {
    const char *u = "192.0.2.10.8.1";
    struct sockaddr_storage ss = { 0 };
    size_t n = esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u),
                                          (struct sockaddr *)&ss, sizeof(ss));
    ck_assert_uint_gt(n, 0);
    ck_assert_int_eq(ss.ss_family, AF_INET);
    struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
    struct in_addr exp;
    inet_pton(AF_INET, "192.0.2.10", &exp);
    ck_assert(memcmp(&sin->sin_addr, &exp, 4) == 0);
    ck_assert_int_eq(ntohs(sin->sin_port), 2049);
} END_TEST

START_TEST(uaddr2sock_v4_127_0_0_1_111) {
    const char *u = "127.0.0.1.0.111";
    struct sockaddr_storage ss = { 0 };
    size_t n = esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u),
                                          (struct sockaddr *)&ss, sizeof(ss));
    ck_assert_uint_gt(n, 0);
    ck_assert_int_eq(ss.ss_family, AF_INET);
    struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
    ck_assert_int_eq(ntohs(sin->sin_port), 111);
} END_TEST

START_TEST(uaddr2sock_v6_fc07_2049) {
    const char *u = "fc07:2::4:1.8.1";
    struct sockaddr_storage ss = { 0 };
    size_t n = esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u),
                                          (struct sockaddr *)&ss, sizeof(ss));
    ck_assert_uint_gt(n, 0);
    ck_assert_int_eq(ss.ss_family, AF_INET6);
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
    ck_assert_int_eq(ntohs(sin6->sin6_port), 2049);
} END_TEST

START_TEST(uaddr2sock_garbage_returns_zero) {
    const char *u = "not-a-uaddr";
    struct sockaddr_storage ss = { 0 };
    ck_assert_uint_eq(
        esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u),
                                   (struct sockaddr *)&ss, sizeof(ss)),
        0);
} END_TEST

START_TEST(uaddr2sock_too_few_dots_returns_zero) {
    /* Missing port portion. */
    const char *u = "192.0.2.10";
    struct sockaddr_storage ss = { 0 };
    ck_assert_uint_eq(
        esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u),
                                   (struct sockaddr *)&ss, sizeof(ss)),
        0);
} END_TEST

/* uaddr round-trip: (sockaddr → uaddr → sockaddr) ≡ id. */
#define UADDR_ROUNDTRIP_V4(name, addr_str, port) \
    START_TEST(name) { \
        struct sockaddr_in orig = v4(addr_str, port); \
        char *u = rpc_sockaddr2uaddr((struct sockaddr *)&orig, 0); \
        ck_assert_ptr_nonnull(u); \
        struct sockaddr_storage rt = { 0 }; \
        size_t n = esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u), \
                                              (struct sockaddr *)&rt, \
                                              sizeof(rt)); \
        ck_assert_uint_gt(n, 0); \
        ck_assert_int_eq(rt.ss_family, AF_INET); \
        struct sockaddr_in *r = (struct sockaddr_in *)&rt; \
        ck_assert(memcmp(&r->sin_addr, &orig.sin_addr, 4) == 0); \
        ck_assert_int_eq(ntohs(r->sin_port), port); \
        free(u); \
    } END_TEST

UADDR_ROUNDTRIP_V4(uaddr_rt_v4_a, "192.0.2.10",   2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_b, "127.0.0.1",    111)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_c, "10.0.0.1",     0)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_d, "10.0.0.1",     65535)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_e, "198.51.100.5", 12345)

#define UADDR_ROUNDTRIP_V6(name, addr_str, port) \
    START_TEST(name) { \
        struct sockaddr_in6 orig = v6(addr_str, port); \
        char *u = rpc_sockaddr2uaddr((struct sockaddr *)&orig, 0); \
        ck_assert_ptr_nonnull(u); \
        struct sockaddr_storage rt = { 0 }; \
        size_t n = esunrpc_rpc_uaddr2sockaddr(NULL, u, strlen(u), \
                                              (struct sockaddr *)&rt, \
                                              sizeof(rt)); \
        ck_assert_uint_gt(n, 0); \
        ck_assert_int_eq(rt.ss_family, AF_INET6); \
        struct sockaddr_in6 *r = (struct sockaddr_in6 *)&rt; \
        ck_assert(memcmp(&r->sin6_addr, &orig.sin6_addr, 16) == 0); \
        ck_assert_int_eq(ntohs(r->sin6_port), port); \
        free(u); \
    } END_TEST

UADDR_ROUNDTRIP_V6(uaddr_rt_v6_a, "2001:db8::1", 2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_b, "fc07:2::4:1", 111)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_c, "::1",         8080)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_d, "2620::1",     0)

/* ============================================================ */
/* Parameterised: many addresses × many ports for round-trip.    */
/* ============================================================ */

UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p1,  "10.0.0.1",     1)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p256,"10.0.0.1",     256)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p1024,"10.0.0.1",   1024)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p2049,"10.0.0.1",   2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p4567,"10.0.0.1",   4567)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p8888,"10.0.0.1",   8888)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p32768,"10.0.0.1",  32768)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_p65534,"10.0.0.1",  65534)

UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_a, "1.2.3.4",       2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_b, "8.8.8.8",       2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_c, "10.255.255.254",2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_d, "172.16.0.1",    2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_e, "172.31.255.254",2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_f, "203.0.113.99",  2049)
UADDR_ROUNDTRIP_V4(uaddr_rt_v4_addr_g, "198.51.100.5",  2049)

UADDR_ROUNDTRIP_V6(uaddr_rt_v6_p1,    "2001:db8::1", 1)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_p256,  "2001:db8::1", 256)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_p2049, "2001:db8::1", 2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_p32768,"2001:db8::1", 32768)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_p65534,"2001:db8::1", 65534)

UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_a, "fc00::1",    2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_b, "fc07:2::18", 2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_c, "fd00:1::1",  2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_d, "2620:0:1::1",2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_e, "2400::abcd", 2049)
UADDR_ROUNDTRIP_V6(uaddr_rt_v6_addr_f, "2620::ffff", 2049)

/* ============================================================ */
/* Wide ntop battery — exhaustive class-shape coverage.          */
/* ============================================================ */

V4_NTOP_TEST(ntop_v4_a_class_low,    "1.0.0.1")
V4_NTOP_TEST(ntop_v4_a_class_mid,    "100.50.25.12")
V4_NTOP_TEST(ntop_v4_a_class_high,   "126.255.255.254")
V4_NTOP_TEST(ntop_v4_b_class_low,    "128.0.0.1")
V4_NTOP_TEST(ntop_v4_b_class_mid,    "172.16.5.5")
V4_NTOP_TEST(ntop_v4_b_class_high,   "191.255.255.254")
V4_NTOP_TEST(ntop_v4_c_class_low,    "192.0.0.1")
V4_NTOP_TEST(ntop_v4_c_class_mid,    "200.100.50.25")
V4_NTOP_TEST(ntop_v4_c_class_high,   "223.255.255.254")
V4_NTOP_TEST(ntop_v4_test_net_a,     "192.0.2.1")
V4_NTOP_TEST(ntop_v4_test_net_a2,    "192.0.2.99")
V4_NTOP_TEST(ntop_v4_test_net_b,     "198.51.100.1")
V4_NTOP_TEST(ntop_v4_test_net_c,     "203.0.113.1")

V6_NTOP_TEST(ntop_v6_2001_db8_a,     "2001:db8::a")
V6_NTOP_TEST(ntop_v6_2001_db8_b,     "2001:db8::b")
V6_NTOP_TEST(ntop_v6_2001_db8_c,     "2001:db8::c")
V6_NTOP_TEST(ntop_v6_fc00,           "fc00::1")
V6_NTOP_TEST(ntop_v6_fc07_2,         "fc07:2::1")
V6_NTOP_TEST(ntop_v6_fc07_2_4_2,     "fc07:2::4:2")
V6_NTOP_TEST(ntop_v6_fd00,           "fd00::1")
V6_NTOP_TEST(ntop_v6_fd99,           "fd99::beef")
V6_NTOP_TEST(ntop_v6_2620_b,         "2620:0:2d0::1")
V6_NTOP_TEST(ntop_v6_2620_c,         "2620:0:2d0::2")
V6_NTOP_TEST(ntop_v6_2400,           "2400::1")
V6_NTOP_TEST(ntop_v6_2607,           "2607::1")

/* ============================================================ */
/* Suite plumbing                                                */
/* ============================================================ */

static Suite *esunrpc_addr_suite(void)
{
    Suite *s = suite_create("esunrpc_addr");

    TCase *t1 = tcase_create("ntop_v4");
    tcase_add_test(t1, ntop_v4_192_0_2_10);
    tcase_add_test(t1, ntop_v4_192_0_2_1);
    tcase_add_test(t1, ntop_v4_192_0_2_254);
    tcase_add_test(t1, ntop_v4_198_51_100_5);
    tcase_add_test(t1, ntop_v4_203_0_113_99);
    tcase_add_test(t1, ntop_v4_10_0_0_1);
    tcase_add_test(t1, ntop_v4_172_16_0_1);
    tcase_add_test(t1, ntop_v4_192_168_1_1);
    tcase_add_test(t1, ntop_v4_8_8_8_8);
    tcase_add_test(t1, ntop_v4_1_1_1_1);
    tcase_add_test(t1, ntop_v4_127_0_0_1);
    tcase_add_test(t1, ntop_v4_zero);
    tcase_add_test(t1, ntop_v4_max);
    suite_add_tcase(s, t1);

    TCase *t2 = tcase_create("ntop_v6");
    tcase_add_test(t2, ntop_v6_2001_db8_1);
    tcase_add_test(t2, ntop_v6_2001_db8_2_18);
    tcase_add_test(t2, ntop_v6_full_form);
    tcase_add_test(t2, ntop_v6_loopback);
    tcase_add_test(t2, ntop_v6_fc07);
    tcase_add_test(t2, ntop_v6_2620);
    tcase_add_test(t2, ntop_unsupported_family_returns_zero);
    tcase_add_test(t2, ntop_buffer_too_small_truncates_safely);
    suite_add_tcase(s, t2);

    TCase *t3 = tcase_create("pton_v4");
    tcase_add_test(t3, pton_v4_192_0_2_10);
    tcase_add_test(t3, pton_v4_198_51_100_5);
    tcase_add_test(t3, pton_v4_10_0_0_1);
    tcase_add_test(t3, pton_v4_192_168_1_1);
    tcase_add_test(t3, pton_v4_127_0_0_1);
    tcase_add_test(t3, pton_v4_8_8_8_8);
    tcase_add_test(t3, pton_v4_zero);
    tcase_add_test(t3, pton_v4_max);
    suite_add_tcase(s, t3);

    TCase *t4 = tcase_create("pton_v6");
    tcase_add_test(t4, pton_v6_2001_db8_1);
    tcase_add_test(t4, pton_v6_2001_db8_2_18);
    tcase_add_test(t4, pton_v6_full_form);
    tcase_add_test(t4, pton_v6_loopback);
    tcase_add_test(t4, pton_v6_fc07);
    suite_add_tcase(s, t4);

    TCase *t5 = tcase_create("pton_rejects");
    tcase_add_test(t5, pton_reject_garbage);
    tcase_add_test(t5, pton_reject_v4_too_many_octets);
    tcase_add_test(t5, pton_reject_v4_huge_octet);
    tcase_add_test(t5, pton_reject_v4_negative);
    tcase_add_test(t5, pton_reject_v6_too_many_colons);
    tcase_add_test(t5, pton_reject_v6_garbage);
    suite_add_tcase(s, t5);

    TCase *t6 = tcase_create("ntop_pton_roundtrip");
    tcase_add_test(t6, roundtrip_v4_a);
    tcase_add_test(t6, roundtrip_v4_b);
    tcase_add_test(t6, roundtrip_v4_c);
    tcase_add_test(t6, roundtrip_v4_d);
    tcase_add_test(t6, roundtrip_v4_e);
    tcase_add_test(t6, roundtrip_v6_a);
    tcase_add_test(t6, roundtrip_v6_b);
    tcase_add_test(t6, roundtrip_v6_c);
    tcase_add_test(t6, roundtrip_v6_d);
    tcase_add_test(t6, roundtrip_v6_e);
    suite_add_tcase(s, t6);

    TCase *t7 = tcase_create("sockaddr2uaddr");
    tcase_add_test(t7, uaddr_v4_format);
    tcase_add_test(t7, uaddr_v4_port_111);
    tcase_add_test(t7, uaddr_v4_port_zero);
    tcase_add_test(t7, uaddr_v4_port_max);
    tcase_add_test(t7, uaddr_v6_format);
    tcase_add_test(t7, uaddr_unsupported_family_returns_NULL);
    suite_add_tcase(s, t7);

    TCase *t8 = tcase_create("uaddr2sockaddr");
    tcase_add_test(t8, uaddr2sock_v4_192_0_2_10_2049);
    tcase_add_test(t8, uaddr2sock_v4_127_0_0_1_111);
    tcase_add_test(t8, uaddr2sock_v6_fc07_2049);
    tcase_add_test(t8, uaddr2sock_garbage_returns_zero);
    tcase_add_test(t8, uaddr2sock_too_few_dots_returns_zero);
    suite_add_tcase(s, t8);

    TCase *t9 = tcase_create("uaddr_roundtrip");
    tcase_add_test(t9, uaddr_rt_v4_a);
    tcase_add_test(t9, uaddr_rt_v4_b);
    tcase_add_test(t9, uaddr_rt_v4_c);
    tcase_add_test(t9, uaddr_rt_v4_d);
    tcase_add_test(t9, uaddr_rt_v4_e);
    tcase_add_test(t9, uaddr_rt_v6_a);
    tcase_add_test(t9, uaddr_rt_v6_b);
    tcase_add_test(t9, uaddr_rt_v6_c);
    tcase_add_test(t9, uaddr_rt_v6_d);
    suite_add_tcase(s, t9);

    /* Wide round-trip parameterisation. */
    TCase *t10 = tcase_create("uaddr_roundtrip_wide");
    tcase_add_test(t10, uaddr_rt_v4_p1);
    tcase_add_test(t10, uaddr_rt_v4_p256);
    tcase_add_test(t10, uaddr_rt_v4_p1024);
    tcase_add_test(t10, uaddr_rt_v4_p2049);
    tcase_add_test(t10, uaddr_rt_v4_p4567);
    tcase_add_test(t10, uaddr_rt_v4_p8888);
    tcase_add_test(t10, uaddr_rt_v4_p32768);
    tcase_add_test(t10, uaddr_rt_v4_p65534);
    tcase_add_test(t10, uaddr_rt_v4_addr_a);
    tcase_add_test(t10, uaddr_rt_v4_addr_b);
    tcase_add_test(t10, uaddr_rt_v4_addr_c);
    tcase_add_test(t10, uaddr_rt_v4_addr_d);
    tcase_add_test(t10, uaddr_rt_v4_addr_e);
    tcase_add_test(t10, uaddr_rt_v4_addr_f);
    tcase_add_test(t10, uaddr_rt_v4_addr_g);
    tcase_add_test(t10, uaddr_rt_v6_p1);
    tcase_add_test(t10, uaddr_rt_v6_p256);
    tcase_add_test(t10, uaddr_rt_v6_p2049);
    tcase_add_test(t10, uaddr_rt_v6_p32768);
    tcase_add_test(t10, uaddr_rt_v6_p65534);
    tcase_add_test(t10, uaddr_rt_v6_addr_a);
    tcase_add_test(t10, uaddr_rt_v6_addr_b);
    tcase_add_test(t10, uaddr_rt_v6_addr_c);
    tcase_add_test(t10, uaddr_rt_v6_addr_d);
    tcase_add_test(t10, uaddr_rt_v6_addr_e);
    tcase_add_test(t10, uaddr_rt_v6_addr_f);
    suite_add_tcase(s, t10);

    /* Exhaustive ntop battery. */
    TCase *t11 = tcase_create("ntop_wide");
    tcase_add_test(t11, ntop_v4_a_class_low);
    tcase_add_test(t11, ntop_v4_a_class_mid);
    tcase_add_test(t11, ntop_v4_a_class_high);
    tcase_add_test(t11, ntop_v4_b_class_low);
    tcase_add_test(t11, ntop_v4_b_class_mid);
    tcase_add_test(t11, ntop_v4_b_class_high);
    tcase_add_test(t11, ntop_v4_c_class_low);
    tcase_add_test(t11, ntop_v4_c_class_mid);
    tcase_add_test(t11, ntop_v4_c_class_high);
    tcase_add_test(t11, ntop_v4_test_net_a);
    tcase_add_test(t11, ntop_v4_test_net_a2);
    tcase_add_test(t11, ntop_v4_test_net_b);
    tcase_add_test(t11, ntop_v4_test_net_c);
    tcase_add_test(t11, ntop_v6_2001_db8_a);
    tcase_add_test(t11, ntop_v6_2001_db8_b);
    tcase_add_test(t11, ntop_v6_2001_db8_c);
    tcase_add_test(t11, ntop_v6_fc00);
    tcase_add_test(t11, ntop_v6_fc07_2);
    tcase_add_test(t11, ntop_v6_fc07_2_4_2);
    tcase_add_test(t11, ntop_v6_fd00);
    tcase_add_test(t11, ntop_v6_fd99);
    tcase_add_test(t11, ntop_v6_2620_b);
    tcase_add_test(t11, ntop_v6_2620_c);
    tcase_add_test(t11, ntop_v6_2400);
    tcase_add_test(t11, ntop_v6_2607);
    suite_add_tcase(s, t11);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_addr_suite
#include "check_runner.h"
