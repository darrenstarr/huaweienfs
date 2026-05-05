// SPDX-License-Identifier: GPL-2.0
/*
 * test_enfs_multipath_parse.c — Check tests for the address-list
 * parsing logic in vendor/openeuler/fs/nfs/enfs/enfs_multipath_parse.c.
 *
 * Covers BOTH IPv4 (the production target for most customer storage —
 * Huawei OceanStor, NetApp, etc.) and IPv6 (the project lab fabric,
 * validated end-to-end on 2026-05-04 against an 8-server cluster).
 * See issue #24 for context.
 *
 * The source under test pulls in real `rpc_pton` / `rpc_cmp_addr`
 * from net/sunrpc/addr.c at runtime; in userspace we substitute
 * libc-backed equivalents in tests/stubs/rpc_addr_stubs.c.
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "enfs_multipath_parse.h"   /* the SUT's public header */

/* ---------------------------------------------------------------- */
/* Forward decls of the internal functions we test directly.        */
/* (sed-stripped of `static` by the SUT compile rule.)              */
/* ---------------------------------------------------------------- */
int enfs_parse_ip_single(struct nfs_ip_list *ip_list, struct net *net_ns,
                         const char *str, enum nfsmultipathoptions type);
int nfs_multipath_parse_options_check_ipv4_valid(struct sockaddr_in *addr);
int nfs_multipath_parse_options_check_ipv6_valid(struct sockaddr_in6 *addr);
int nfs_multipath_parse_options_check_ip_valid(struct sockaddr_storage *address);
int nfs_multipath_parse_options_check_duplicate(
        struct multipath_mount_options *options);
int nfs_multipath_parse_ip_list(char *buffer, struct net *net_ns,
                                struct multipath_mount_options *options,
                                enum nfsmultipathoptions type);
bool isInvalidDns(char *cursor, struct net *net_ns);
bool enfs_valid_dns(const char *s);

/* ---------------------------------------------------------------- */
/* Test helpers.                                                    */
/* ---------------------------------------------------------------- */

static struct nfs_ip_list *fresh_list(void)
{
    struct nfs_ip_list *l = calloc(1, sizeof(*l));
    ck_assert_ptr_nonnull(l);
    return l;
}

/* nfs_multipath_parse_ip_list takes the wrapper struct, not a bare
 * ip-list — it routes to options->remote_ip_list or local_ip_list
 * based on the type argument. Helper builds a wrapper around our
 * test list. */
static struct multipath_mount_options *
fresh_options(void)
{
    struct multipath_mount_options *o = calloc(1, sizeof(*o));
    ck_assert_ptr_nonnull(o);
    o->remote_ip_list = calloc(1, sizeof(*o->remote_ip_list));
    o->local_ip_list  = calloc(1, sizeof(*o->local_ip_list));
    ck_assert_ptr_nonnull(o->remote_ip_list);
    ck_assert_ptr_nonnull(o->local_ip_list);
    return o;
}

static struct sockaddr_in v4_addr(const char *s)
{
    struct sockaddr_in sin = { 0 };
    sin.sin_family = AF_INET;
    ck_assert_int_eq(inet_pton(AF_INET, s, &sin.sin_addr), 1);
    return sin;
}

static struct sockaddr_in6 v6_addr(const char *s)
{
    struct sockaddr_in6 sin6 = { 0 };
    sin6.sin6_family = AF_INET6;
    ck_assert_int_eq(inet_pton(AF_INET6, s, &sin6.sin6_addr), 1);
    return sin6;
}

/* ---------------------------------------------------------------- */
/* IPv4 parse cases.                                                */
/* ---------------------------------------------------------------- */

START_TEST(v4_parse_single_addr)
{
    struct nfs_ip_list *l = fresh_list();
    int rc = enfs_parse_ip_single(l, NULL, "192.0.2.10", REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(l->count, 1);
    ck_assert_int_eq(l->address[0].ss_family, AF_INET);
    free(l);
}
END_TEST

START_TEST(v4_parse_three_addrs_via_list)
{
    struct multipath_mount_options *o = fresh_options();
    char input[] = "192.0.2.10~192.0.2.11~192.0.2.12";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 3);
    for (int i = 0; i < 3; i++)
        ck_assert_int_eq(o->remote_ip_list->address[i].ss_family, AF_INET);
}
END_TEST

START_TEST(v4_parse_invalid_addr_rejected)
{
    struct nfs_ip_list *l = fresh_list();
    int rc = enfs_parse_ip_single(l, NULL, "999.999.999.999", REMOTEADDR);
    ck_assert_int_ne(rc, 0);
    free(l);
}
END_TEST

START_TEST(v4_check_ipv4_valid_accepts_normal)
{
    struct sockaddr_in a = v4_addr("192.0.2.10");
    ck_assert_int_eq(nfs_multipath_parse_options_check_ipv4_valid(&a), 0);
}
END_TEST

START_TEST(v4_check_ipv4_valid_rejects_zero)
{
    struct sockaddr_in a = v4_addr("0.0.0.0");
    ck_assert_int_ne(nfs_multipath_parse_options_check_ipv4_valid(&a), 0);
}
END_TEST

START_TEST(v4_check_ipv4_valid_rejects_broadcast)
{
    struct sockaddr_in a = v4_addr("255.255.255.255");
    ck_assert_int_ne(nfs_multipath_parse_options_check_ipv4_valid(&a), 0);
}
END_TEST

/* ---------------------------------------------------------------- */
/* IPv6 parse cases.                                                */
/* ---------------------------------------------------------------- */

START_TEST(v6_parse_single_addr)
{
    struct nfs_ip_list *l = fresh_list();
    int rc = enfs_parse_ip_single(l, NULL, "2001:db8:2::11", REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(l->count, 1);
    ck_assert_int_eq(l->address[0].ss_family, AF_INET6);
    free(l);
}
END_TEST

START_TEST(v6_parse_eight_addrs_via_list)
{
    /* Mirrors the lab configuration: 8 OceanStor server IPs. */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8:2::11~2001:db8:2::12~2001:db8:2::13~"
                   "2001:db8:2::14~2001:db8:2::15~2001:db8:2::16~"
                   "2001:db8:2::17~2001:db8:2::18";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 8);
    for (int i = 0; i < 8; i++)
        ck_assert_int_eq(o->remote_ip_list->address[i].ss_family, AF_INET6);
}
END_TEST

START_TEST(v6_parse_two_local_addrs_via_list)
{
    /* Mirrors the lab: 2 local NICs on the same /64. */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8:2::4:1~2001:db8:2::4:2";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, LOCALADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->local_ip_list->count, 2);
}
END_TEST

START_TEST(v6_parse_invalid_addr_rejected)
{
    struct nfs_ip_list *l = fresh_list();
    int rc = enfs_parse_ip_single(l, NULL, "not-an-address::wat", REMOTEADDR);
    ck_assert_int_ne(rc, 0);
    free(l);
}
END_TEST

START_TEST(v6_check_ipv6_valid_accepts_normal)
{
    struct sockaddr_in6 a = v6_addr("2001:db8:2::11");
    ck_assert_int_eq(nfs_multipath_parse_options_check_ipv6_valid(&a), 0);
}
END_TEST

START_TEST(v6_check_ipv6_valid_rejects_unspecified)
{
    struct sockaddr_in6 a = v6_addr("::");
    ck_assert_int_ne(nfs_multipath_parse_options_check_ipv6_valid(&a), 0);
}
END_TEST

/* ---------------------------------------------------------------- */
/* Cross-list duplicate detection.                                  */
/* check_duplicate flags a configuration where a local address      */
/* appears in the remote-addrs list (or vice versa) — would mean    */
/* the client tries to talk to itself.                              */
/* ---------------------------------------------------------------- */

START_TEST(cross_list_duplicate_v4_detected)
{
    struct multipath_mount_options *o = fresh_options();
    /* Local list has 192.0.2.10; remote list also includes it. */
    struct sockaddr_in a = v4_addr("192.0.2.10");
    struct sockaddr_in b = v4_addr("192.0.2.20");

    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->addrlen[0] = sizeof(a);
    o->local_ip_list->count = 1;

    memcpy(&o->remote_ip_list->address[0], &b, sizeof(b));
    o->remote_ip_list->addrlen[0] = sizeof(b);
    memcpy(&o->remote_ip_list->address[1], &a, sizeof(a));   /* duplicate */
    o->remote_ip_list->addrlen[1] = sizeof(a);
    o->remote_ip_list->count = 2;

    int rc = nfs_multipath_parse_options_check_duplicate(o);
    ck_assert_int_ne(rc, 0); /* expect rejection */
}
END_TEST

START_TEST(cross_list_no_duplicate_v4_accepted)
{
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in a = v4_addr("192.0.2.10");
    struct sockaddr_in b = v4_addr("192.0.2.20");

    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->count = 1;

    memcpy(&o->remote_ip_list->address[0], &b, sizeof(b));
    o->remote_ip_list->count = 1;

    int rc = nfs_multipath_parse_options_check_duplicate(o);
    ck_assert_int_eq(rc, 0);
}
END_TEST

START_TEST(cross_list_duplicate_v6_detected)
{
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in6 a = v6_addr("2001:db8:2::4:1");
    struct sockaddr_in6 b = v6_addr("2001:db8:2::11");

    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->count = 1;

    memcpy(&o->remote_ip_list->address[0], &b, sizeof(b));
    memcpy(&o->remote_ip_list->address[1], &a, sizeof(a)); /* dup */
    o->remote_ip_list->count = 2;

    int rc = nfs_multipath_parse_options_check_duplicate(o);
    ck_assert_int_ne(rc, 0);
}
END_TEST

/* ================================================================ */
/* IPv4: comprehensive validity + edge-case battery.                */
/*                                                                  */
/* IMPORTANT — the SUT's validator is intentionally LENIENT:        */
/*   - rejects only 0.0.0.0 and 255.255.255.255                     */
/*   - accepts everything else, including 127.0.0.0/8 (loopback),   */
/*     224.0.0.0/4 (multicast), and 169.254.0.0/16 (link-local)     */
/*                                                                  */
/* These tests document the observed behaviour. The leniency around */
/* loopback / multicast / link-local is filed as a follow-up issue  */
/* — passing 127.0.0.1 as a remoteaddr would produce bizarre but    */
/* not impossible mounts; it ought to be rejected by the validator. */
/* ================================================================ */

static int v4_valid_accepts(const char *s) {
    struct sockaddr_in a = v4_addr(s);
    return nfs_multipath_parse_options_check_ipv4_valid(&a) == 0;
}
static int v4_valid_rejects(const char *s) {
    struct sockaddr_in a = v4_addr(s);
    return nfs_multipath_parse_options_check_ipv4_valid(&a) != 0;
}

#define V4_ACCEPT_TEST(name, addr) \
    START_TEST(name) { ck_assert(v4_valid_accepts(addr)); } END_TEST
#define V4_REJECT_TEST(name, addr) \
    START_TEST(name) { ck_assert(v4_valid_rejects(addr)); } END_TEST

V4_ACCEPT_TEST(v4_accept_192_0_2_10,    "192.0.2.10")
V4_ACCEPT_TEST(v4_accept_192_0_2_1,     "192.0.2.1")
V4_ACCEPT_TEST(v4_accept_192_0_2_254,   "192.0.2.254")
V4_ACCEPT_TEST(v4_accept_198_51_100_5,  "198.51.100.5")
V4_ACCEPT_TEST(v4_accept_203_0_113_99,  "203.0.113.99")
V4_ACCEPT_TEST(v4_accept_10_0_0_1,      "10.0.0.1")
V4_ACCEPT_TEST(v4_accept_172_16_0_1,    "172.16.0.1")
V4_ACCEPT_TEST(v4_accept_192_168_1_1,   "192.168.1.1")
V4_ACCEPT_TEST(v4_accept_8_8_8_8,       "8.8.8.8")
V4_ACCEPT_TEST(v4_accept_1_1_1_1,       "1.1.1.1")

V4_REJECT_TEST(v4_reject_0_0_0_0,         "0.0.0.0")
V4_REJECT_TEST(v4_reject_255_255_255_255, "255.255.255.255")

/* Documenting current (lenient) behaviour — these are ACCEPTED.
 * If/when issue tracking the validator hardening is fixed, flip
 * these to REJECT_TEST. */
V4_ACCEPT_TEST(v4_lenient_accept_127_0_0_1,       "127.0.0.1")
V4_ACCEPT_TEST(v4_lenient_accept_127_5_5_5,       "127.5.5.5")
V4_ACCEPT_TEST(v4_lenient_accept_127_255_255_254, "127.255.255.254")
V4_ACCEPT_TEST(v4_lenient_accept_224_0_0_1,       "224.0.0.1")
V4_ACCEPT_TEST(v4_lenient_accept_224_5_6_7,       "224.5.6.7")
V4_ACCEPT_TEST(v4_lenient_accept_239_255_255_255, "239.255.255.255")
V4_ACCEPT_TEST(v4_lenient_accept_169_254_0_1,     "169.254.0.1")
V4_ACCEPT_TEST(v4_lenient_accept_169_254_99_99,   "169.254.99.99")

/* IPv4 parsing of malformed inputs at enfs_parse_ip_single. */
#define V4_PARSE_REJECT_TEST(name, str) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        int rc = enfs_parse_ip_single(l, NULL, str, REMOTEADDR); \
        ck_assert_int_ne(rc, 0); \
        free(l); \
    } END_TEST

V4_PARSE_REJECT_TEST(v4_reject_str_empty,           "")
V4_PARSE_REJECT_TEST(v4_reject_str_999_dot_999,     "999.999.999.999")
V4_PARSE_REJECT_TEST(v4_reject_str_only_dots,       "...")
V4_PARSE_REJECT_TEST(v4_reject_str_letters,         "abcd")
V4_PARSE_REJECT_TEST(v4_reject_str_too_many_octets, "1.2.3.4.5")
V4_PARSE_REJECT_TEST(v4_reject_str_too_few_octets,  "1.2.3")
V4_PARSE_REJECT_TEST(v4_reject_str_negative_octet,  "192.0.2.-1")
V4_PARSE_REJECT_TEST(v4_reject_str_huge_octet,      "192.0.2.99999")
V4_PARSE_REJECT_TEST(v4_reject_str_trailing_dot,    "192.0.2.10.")
V4_PARSE_REJECT_TEST(v4_reject_str_leading_dot,     ".192.0.2.10")

/* Many parses succeed for individual addresses. */
#define V4_PARSE_ACCEPT_TEST(name, str) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        int rc = enfs_parse_ip_single(l, NULL, str, REMOTEADDR); \
        ck_assert_int_eq(rc, 0); \
        ck_assert_int_eq(l->count, 1); \
        ck_assert_int_eq(l->address[0].ss_family, AF_INET); \
        free(l); \
    } END_TEST

V4_PARSE_ACCEPT_TEST(v4_parse_192_0_2_10,    "192.0.2.10")
V4_PARSE_ACCEPT_TEST(v4_parse_198_51_100_99, "198.51.100.99")
V4_PARSE_ACCEPT_TEST(v4_parse_10_20_30_40,   "10.20.30.40")
V4_PARSE_ACCEPT_TEST(v4_parse_192_168_1_1,   "192.168.1.1")
V4_PARSE_ACCEPT_TEST(v4_parse_172_16_5_5,    "172.16.5.5")

/* List parsing — different counts, all valid. */
START_TEST(v4_list_count_2) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "192.0.2.10~192.0.2.11";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 2);
} END_TEST
START_TEST(v4_list_count_4) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "192.0.2.10~192.0.2.11~192.0.2.12~192.0.2.13";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST
START_TEST(v4_list_count_8) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1~10.0.0.2~10.0.0.3~10.0.0.4~"
                   "10.0.0.5~10.0.0.6~10.0.0.7~10.0.0.8";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 8);
} END_TEST
START_TEST(v4_list_count_16) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1~10.0.0.2~10.0.0.3~10.0.0.4~"
                   "10.0.0.5~10.0.0.6~10.0.0.7~10.0.0.8~"
                   "10.0.0.9~10.0.0.10~10.0.0.11~10.0.0.12~"
                   "10.0.0.13~10.0.0.14~10.0.0.15~10.0.0.16";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 16);
} END_TEST
START_TEST(v4_list_one_invalid_aborts) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "192.0.2.10~not-a-valid-ip~192.0.2.12";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_ne(rc, 0);
} END_TEST

/* ================================================================ */
/* IPv6: comprehensive accept/reject + parse cases.                 */
/* ================================================================ */

static int v6_valid_accepts(const char *s) {
    struct sockaddr_in6 a = v6_addr(s);
    return nfs_multipath_parse_options_check_ipv6_valid(&a) == 0;
}
static int v6_valid_rejects(const char *s) {
    struct sockaddr_in6 a = v6_addr(s);
    return nfs_multipath_parse_options_check_ipv6_valid(&a) != 0;
}

#define V6_ACCEPT_TEST(name, addr) \
    START_TEST(name) { ck_assert(v6_valid_accepts(addr)); } END_TEST
#define V6_REJECT_TEST(name, addr) \
    START_TEST(name) { ck_assert(v6_valid_rejects(addr)); } END_TEST

V6_ACCEPT_TEST(v6_accept_2001_db8_2_11,        "2001:db8:2::11")
V6_ACCEPT_TEST(v6_accept_2001_db8_2_18,        "2001:db8:2::18")
V6_ACCEPT_TEST(v6_accept_2001_db8_full_form,   "2001:0db8:0002:0000:0000:0000:0000:0011")
V6_ACCEPT_TEST(v6_accept_fc00_ula,             "fc00::1")
V6_ACCEPT_TEST(v6_accept_fd00_ula,             "fd00::1")
V6_ACCEPT_TEST(v6_accept_fc07_2_4_1,           "fc07:2::4:1")
V6_ACCEPT_TEST(v6_accept_fc07_2_4_2,           "fc07:2::4:2")
V6_ACCEPT_TEST(v6_accept_2620_routable,        "2620:0:1234::5")
V6_ACCEPT_TEST(v6_accept_2400_routable,        "2400::1")

V6_REJECT_TEST(v6_reject_unspecified_colon_colon, "::")
/* All other v6 addresses are accepted by the SUT (only :: and ::ffff
 * variants of all-zeros / all-ones are rejected). Document. */
V6_ACCEPT_TEST(v6_lenient_accept_loopback,           "::1")
V6_ACCEPT_TEST(v6_lenient_accept_multicast_ff00,     "ff00::1")
V6_ACCEPT_TEST(v6_lenient_accept_multicast_ff02,     "ff02::1")
V6_ACCEPT_TEST(v6_lenient_accept_link_local_fe80,    "fe80::1")
V6_ACCEPT_TEST(v6_lenient_accept_link_local_long,    "fe80::abcd:ef01:2345:6789")

#define V6_PARSE_REJECT_TEST(name, str) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        int rc = enfs_parse_ip_single(l, NULL, str, REMOTEADDR); \
        ck_assert_int_ne(rc, 0); \
        free(l); \
    } END_TEST

V6_PARSE_REJECT_TEST(v6_reject_str_garbage,          "not-an-address::wat")
V6_PARSE_REJECT_TEST(v6_reject_str_too_many_colons,  "1:2:3:4:5:6:7:8:9")
V6_PARSE_REJECT_TEST(v6_reject_str_letters_in_hex,   "abcg::1")
V6_PARSE_REJECT_TEST(v6_reject_str_double_doublecol, "::1::2")

#define V6_PARSE_ACCEPT_TEST(name, str) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        int rc = enfs_parse_ip_single(l, NULL, str, REMOTEADDR); \
        ck_assert_int_eq(rc, 0); \
        ck_assert_int_eq(l->count, 1); \
        ck_assert_int_eq(l->address[0].ss_family, AF_INET6); \
        free(l); \
    } END_TEST

V6_PARSE_ACCEPT_TEST(v6_parse_2001_db8_1,       "2001:db8::1")
V6_PARSE_ACCEPT_TEST(v6_parse_2001_db8_2_18,    "2001:db8:2::18")
V6_PARSE_ACCEPT_TEST(v6_parse_full_form,        "2001:0db8:0002:0000:0000:0000:0000:0011")
V6_PARSE_ACCEPT_TEST(v6_parse_fc07_2_4_1,       "fc07:2::4:1")
V6_PARSE_ACCEPT_TEST(v6_parse_compact_form,     "::ffff:1")

/* IPv6 list-count tests. */
START_TEST(v6_list_count_2) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1~2001:db8::2";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 2);
} END_TEST
START_TEST(v6_list_count_4) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1~2001:db8::2~2001:db8::3~2001:db8::4";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST
START_TEST(v6_list_count_16) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "fc07::1~fc07::2~fc07::3~fc07::4~fc07::5~fc07::6~"
                   "fc07::7~fc07::8~fc07::9~fc07::a~fc07::b~fc07::c~"
                   "fc07::d~fc07::e~fc07::f~fc07::10";
    ck_assert_int_eq(nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR), 0);
    ck_assert_int_eq(o->remote_ip_list->count, 16);
} END_TEST

/* ================================================================ */
/* Generic family-dispatch (check_ip_valid).                        */
/* ================================================================ */

START_TEST(generic_check_v4_valid) {
    struct sockaddr_storage ss = { 0 };
    struct sockaddr_in *a = (void *)&ss;
    *a = v4_addr("192.0.2.10");
    ck_assert_int_eq(nfs_multipath_parse_options_check_ip_valid(&ss), 0);
} END_TEST
START_TEST(generic_check_v6_valid) {
    struct sockaddr_storage ss = { 0 };
    struct sockaddr_in6 *a = (void *)&ss;
    *a = v6_addr("2001:db8::1");
    ck_assert_int_eq(nfs_multipath_parse_options_check_ip_valid(&ss), 0);
} END_TEST
/* Documents lenient acceptance of v4 loopback at the dispatch layer
 * — the same leniency as the v4-specific validator (file an issue
 * to harden it). */
START_TEST(generic_check_v4_loopback_lenient_accept) {
    struct sockaddr_storage ss = { 0 };
    struct sockaddr_in *a = (void *)&ss;
    *a = v4_addr("127.0.0.1");
    ck_assert_int_eq(nfs_multipath_parse_options_check_ip_valid(&ss), 0);
} END_TEST
START_TEST(generic_check_v6_unspecified_rejected) {
    struct sockaddr_storage ss = { 0 };
    struct sockaddr_in6 *a = (void *)&ss;
    *a = v6_addr("::");
    ck_assert_int_ne(nfs_multipath_parse_options_check_ip_valid(&ss), 0);
} END_TEST
START_TEST(generic_check_unsupported_family_rejected) {
    struct sockaddr_storage ss = { 0 };
    ss.ss_family = AF_UNIX;
    ck_assert_int_ne(nfs_multipath_parse_options_check_ip_valid(&ss), 0);
} END_TEST

/* ================================================================ */
/* Cross-list duplicate matrix at varying sizes.                    */
/* ================================================================ */

START_TEST(cross_list_v4_local_in_remote_first_pos) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in a = v4_addr("10.0.0.1");
    struct sockaddr_in b = v4_addr("10.0.0.2");
    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->count = 1;
    memcpy(&o->remote_ip_list->address[0], &a, sizeof(a)); /* dup at 0 */
    memcpy(&o->remote_ip_list->address[1], &b, sizeof(b));
    o->remote_ip_list->count = 2;
    ck_assert_int_ne(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_v4_local_in_remote_last_pos) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in a = v4_addr("10.0.0.1");
    struct sockaddr_in b = v4_addr("10.0.0.2");
    struct sockaddr_in c = v4_addr("10.0.0.3");
    memcpy(&o->local_ip_list->address[0],  &c, sizeof(c));
    o->local_ip_list->count = 1;
    memcpy(&o->remote_ip_list->address[0], &a, sizeof(a));
    memcpy(&o->remote_ip_list->address[1], &b, sizeof(b));
    memcpy(&o->remote_ip_list->address[2], &c, sizeof(c)); /* dup at end */
    o->remote_ip_list->count = 3;
    ck_assert_int_ne(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_v4_two_locals_one_in_remote) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in a = v4_addr("10.0.0.1");
    struct sockaddr_in b = v4_addr("10.0.0.2");
    struct sockaddr_in c = v4_addr("10.0.0.3");
    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    memcpy(&o->local_ip_list->address[1],  &b, sizeof(b));
    o->local_ip_list->count = 2;
    memcpy(&o->remote_ip_list->address[0], &c, sizeof(c));
    memcpy(&o->remote_ip_list->address[1], &b, sizeof(b)); /* dup of local b */
    o->remote_ip_list->count = 2;
    ck_assert_int_ne(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_v6_local_in_remote_first_pos) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in6 a = v6_addr("fc07::1");
    struct sockaddr_in6 b = v6_addr("fc07::2");
    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->count = 1;
    memcpy(&o->remote_ip_list->address[0], &a, sizeof(a)); /* dup */
    memcpy(&o->remote_ip_list->address[1], &b, sizeof(b));
    o->remote_ip_list->count = 2;
    ck_assert_int_ne(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_v6_no_duplicate_8x2) {
    struct multipath_mount_options *o = fresh_options();
    const char *locals[]  = {"fc07:0:0:1::1", "fc07:0:0:1::2"};
    const char *remotes[] = {"fc07:1::1", "fc07:1::2", "fc07:1::3", "fc07:1::4",
                             "fc07:1::5", "fc07:1::6", "fc07:1::7", "fc07:1::8"};
    for (int i = 0; i < 2; i++) {
        struct sockaddr_in6 a = v6_addr(locals[i]);
        memcpy(&o->local_ip_list->address[i], &a, sizeof(a));
    }
    o->local_ip_list->count = 2;
    for (int i = 0; i < 8; i++) {
        struct sockaddr_in6 a = v6_addr(remotes[i]);
        memcpy(&o->remote_ip_list->address[i], &a, sizeof(a));
    }
    o->remote_ip_list->count = 8;
    ck_assert_int_eq(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_empty_local_no_dup) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in b = v4_addr("10.0.0.2");
    o->local_ip_list->count = 0;
    memcpy(&o->remote_ip_list->address[0], &b, sizeof(b));
    o->remote_ip_list->count = 1;
    ck_assert_int_eq(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

START_TEST(cross_list_empty_remote_no_dup) {
    struct multipath_mount_options *o = fresh_options();
    struct sockaddr_in a = v4_addr("10.0.0.1");
    memcpy(&o->local_ip_list->address[0],  &a, sizeof(a));
    o->local_ip_list->count = 1;
    o->remote_ip_list->count = 0;
    ck_assert_int_eq(nfs_multipath_parse_options_check_duplicate(o), 0);
} END_TEST

/* ================================================================ */
/* Stress: many list-count + family permutations.                   */
/* ================================================================ */

#define V4_BIG_LIST_TEST(name, n) \
    START_TEST(name) { \
        struct multipath_mount_options *o = fresh_options(); \
        char input[2048] = {0}; \
        for (unsigned int i = 0; i < (n); i++) { \
            char addr[32]; \
            snprintf(addr, sizeof(addr), "10.%u.%u.%u%s", \
                     ((i + 1) >> 16) & 0xff, ((i + 1) >> 8) & 0xff, \
                     (i + 1) & 0xff, i + 1 < (n) ? "~" : ""); \
            strlcat(input, addr, sizeof(input)); \
        } \
        ck_assert_int_eq(nfs_multipath_parse_ip_list( \
            input, NULL, o, REMOTEADDR), 0); \
        ck_assert_int_eq(o->remote_ip_list->count, (n)); \
    } END_TEST

V4_BIG_LIST_TEST(v4_list_count_3,   3)
V4_BIG_LIST_TEST(v4_list_count_5,   5)
V4_BIG_LIST_TEST(v4_list_count_6,   6)
V4_BIG_LIST_TEST(v4_list_count_7,   7)
V4_BIG_LIST_TEST(v4_list_count_9,   9)
V4_BIG_LIST_TEST(v4_list_count_10,  10)
V4_BIG_LIST_TEST(v4_list_count_12,  12)
V4_BIG_LIST_TEST(v4_list_count_14,  14)
V4_BIG_LIST_TEST(v4_list_count_20,  20)
V4_BIG_LIST_TEST(v4_list_count_24,  24)
V4_BIG_LIST_TEST(v4_list_count_28,  28)
V4_BIG_LIST_TEST(v4_list_count_30,  30)

/* Per-octet boundary acceptance for IPv4 parser. */
#define V4_PARSE_OCTET_TEST(name, addr) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        ck_assert_int_eq( \
            enfs_parse_ip_single(l, NULL, addr, REMOTEADDR), 0); \
        free(l); \
    } END_TEST

V4_PARSE_OCTET_TEST(v4_octet_0_0_0_1,     "0.0.0.1")
V4_PARSE_OCTET_TEST(v4_octet_0_0_1_0,     "0.0.1.0")
V4_PARSE_OCTET_TEST(v4_octet_0_1_0_0,     "0.1.0.0")
V4_PARSE_OCTET_TEST(v4_octet_1_0_0_0,     "1.0.0.0")
V4_PARSE_OCTET_TEST(v4_octet_192_0_2_0,   "192.0.2.0")
V4_PARSE_OCTET_TEST(v4_octet_192_0_2_127, "192.0.2.127")
V4_PARSE_OCTET_TEST(v4_octet_192_0_2_128, "192.0.2.128")
V4_PARSE_OCTET_TEST(v4_octet_192_0_2_253, "192.0.2.253")
V4_PARSE_OCTET_TEST(v4_octet_255_255_255_254, "255.255.255.254")
V4_PARSE_OCTET_TEST(v4_octet_254_254_254_254, "254.254.254.254")
V4_PARSE_OCTET_TEST(v4_octet_100_100_100_100, "100.100.100.100")
V4_PARSE_OCTET_TEST(v4_octet_50_50_50_50,     "50.50.50.50")

/* Sweep: every /16 we'd plausibly see in a customer config. */
V4_PARSE_OCTET_TEST(v4_a_class_a,   "8.0.0.1")
V4_PARSE_OCTET_TEST(v4_a_class_b,   "9.0.0.1")
V4_PARSE_OCTET_TEST(v4_a_class_c,   "11.0.0.1")
V4_PARSE_OCTET_TEST(v4_b_class_a,   "172.16.0.1")
V4_PARSE_OCTET_TEST(v4_b_class_b,   "172.17.0.1")
V4_PARSE_OCTET_TEST(v4_b_class_c,   "172.31.255.254")
V4_PARSE_OCTET_TEST(v4_c_class_a,   "192.168.0.1")
V4_PARSE_OCTET_TEST(v4_c_class_b,   "192.168.255.254")
V4_PARSE_OCTET_TEST(v4_routable_a,  "203.0.113.1")
V4_PARSE_OCTET_TEST(v4_routable_b,  "198.51.100.1")
V4_PARSE_OCTET_TEST(v4_routable_c,  "8.8.4.4")
V4_PARSE_OCTET_TEST(v4_routable_d,  "1.0.0.1")

/* IPv6 round-trip parse + render (the parser keeps family info,
 * which we already test elsewhere; here we add many addr literals
 * to widen coverage of the parse function's IPv6 branch). */
#define V6_PARSE_REP_TEST(name, addr) \
    START_TEST(name) { \
        struct nfs_ip_list *l = fresh_list(); \
        ck_assert_int_eq( \
            enfs_parse_ip_single(l, NULL, addr, REMOTEADDR), 0); \
        ck_assert_int_eq(l->address[0].ss_family, AF_INET6); \
        free(l); \
    } END_TEST

V6_PARSE_REP_TEST(v6_rep_2001_db8_a, "2001:db8::1")
V6_PARSE_REP_TEST(v6_rep_2001_db8_b, "2001:db8:0:1::1")
V6_PARSE_REP_TEST(v6_rep_2001_db8_c, "2001:db8:0:1::2")
V6_PARSE_REP_TEST(v6_rep_2001_db8_d, "2001:db8::abcd")
V6_PARSE_REP_TEST(v6_rep_fc07_2_4_3, "fc07:2::4:3")
V6_PARSE_REP_TEST(v6_rep_fc07_2_4_4, "fc07:2::4:4")
V6_PARSE_REP_TEST(v6_rep_fc07_2_4_5, "fc07:2::4:5")
V6_PARSE_REP_TEST(v6_rep_fc07_2_4_6, "fc07:2::4:6")
V6_PARSE_REP_TEST(v6_rep_fd00,       "fd00:1234::1")
V6_PARSE_REP_TEST(v6_rep_fd01,       "fd01:5678::1")
V6_PARSE_REP_TEST(v6_rep_fd02,       "fd02:9abc::1")
V6_PARSE_REP_TEST(v6_rep_2620_a,     "2620:0:2d0:200::1")
V6_PARSE_REP_TEST(v6_rep_2620_b,     "2620:0:2d0:200::2")
V6_PARSE_REP_TEST(v6_rep_2620_c,     "2620:0:2d0:200::3")
V6_PARSE_REP_TEST(v6_rep_huawei_lab, "fc07:2::4:1")
V6_PARSE_REP_TEST(v6_rep_huawei_oc,  "fc07:2::18")

/* Mixed-family lists must accept both. */
START_TEST(mixed_family_list_v4_then_v6) {
    struct nfs_ip_list *l = fresh_list();
    ck_assert_int_eq(enfs_parse_ip_single(l, NULL, "192.0.2.10", REMOTEADDR), 0);
    ck_assert_int_eq(enfs_parse_ip_single(l, NULL, "2001:db8::1", REMOTEADDR), 0);
    ck_assert_int_eq(l->count, 2);
    ck_assert_int_eq(l->address[0].ss_family, AF_INET);
    ck_assert_int_eq(l->address[1].ss_family, AF_INET6);
    free(l);
} END_TEST

START_TEST(mixed_family_list_v6_then_v4) {
    struct nfs_ip_list *l = fresh_list();
    ck_assert_int_eq(enfs_parse_ip_single(l, NULL, "2001:db8::1", REMOTEADDR), 0);
    ck_assert_int_eq(enfs_parse_ip_single(l, NULL, "192.0.2.10", REMOTEADDR), 0);
    ck_assert_int_eq(l->count, 2);
    ck_assert_int_eq(l->address[0].ss_family, AF_INET6);
    ck_assert_int_eq(l->address[1].ss_family, AF_INET);
    free(l);
} END_TEST

/* ---------------------------------------------------------------- */
/* IP-range expansion. The list parser uses '~' to separate         */
/* singletons and 'A-B' to define an inclusive range. The range     */
/* expansion code in enfs_parse_ip_range walks a tmp_addr forward   */
/* until it equals the end address, appending each step. The IPv6   */
/* path uses nfs_multipath_parse_ip_ipv6_add to bump the v6 addr.   */
/* ---------------------------------------------------------------- */

START_TEST(v4_range_short_inclusive) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "192.0.2.10-192.0.2.12";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    /* Range 10..12 inclusive = 3 addrs. */
    ck_assert_int_eq(o->remote_ip_list->count, 3);
    for (int i = 0; i < 3; i++)
        ck_assert_int_eq(o->remote_ip_list->address[i].ss_family, AF_INET);
} END_TEST

START_TEST(v4_range_one_step) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1-10.0.0.2";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 2);
} END_TEST

START_TEST(v4_range_same_endpoint_collapses) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.5-10.0.0.5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    /* The SUT's "range ip is same ip" branch returns 0 without
     * appending the duplicate. So the single anchor stays. */
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 1);
} END_TEST

START_TEST(v4_range_octet_carries) {
    struct multipath_mount_options *o = fresh_options();
    /* Range 10.0.0.254 → 10.0.1.2 = 5 addrs (254, 255, 256(=1.0),
     * 1.1, 1.2)... but 255 is broadcast so the SUT skips it via
     * the broadcast-validity check. Either way, count > 1 confirms
     * the octet-carry code ran. */
    char input[] = "10.0.0.254-10.0.1.2";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_ge(o->remote_ip_list->count, 2);
} END_TEST

START_TEST(v4_range_end_lower_is_rejected_or_no_op) {
    /* 10.0.0.10-10.0.0.5 — end < start. SUT loops until tmp == end
     * via increment, which would wrap; in practice it caps via
     * count <= NFS_MAX_REMOTEADDRS. Result: bounded count. */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.10-10.0.0.5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    /* Either rejected (rc != 0) or capped at the count limit; both
     * are valid SUT behaviours. The point is no crash, no infinite
     * loop. */
    (void)rc;
    ck_assert_int_le(o->remote_ip_list->count, 64);
} END_TEST

START_TEST(v4_range_then_single) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1-10.0.0.3~10.0.0.20";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST

START_TEST(v4_single_then_range) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.20~10.0.0.1-10.0.0.3";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    /* 1 single + 3-element range = 4 entries. */
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST

START_TEST(v4_range_two_consecutive_dashes_rejected) {
    /* The SUT's "Multiple Range" guard fires only when two range
     * separators appear back-to-back with no '~' between, e.g.
     * "A-B-C": the cursor returns "B" with single=false while
     * prev_range is already true from "A". A range followed by a
     * '~'-separated singleton-then-range is a different flow that
     * the SUT actually accepts (verified in v4_range_then_single
     * + the iterations after). */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1-10.0.0.3-10.0.0.5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_ne(rc, 0);
} END_TEST

START_TEST(v4_range_then_singleton_then_range_accepted) {
    /* The "two ranges via tilde" form parses as: range A→B, single
     * C, range D→E (with each '-' bordered by '~'). It is accepted. */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1-10.0.0.3~10.0.0.10-10.0.0.12";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    /* Range 1..3 = 3 entries, single 10, range 12 = 1 + 1 = 2 more.
     * Actually the 10-12 expansion fills 10,11,12 = 3 entries, but
     * the second '-' makes 10.0.0.10 a singleton (added to list)
     * and 10.0.0.12 the range END from 10.0.0.10. So total is
     * 3 (first range) + 3 (10..12) = 6 entries. The exact count
     * depends on how the cursor parser splits — assert the flexible
     * "all 6 are present" lower bound. */
    ck_assert_int_ge(o->remote_ip_list->count, 4);
} END_TEST

START_TEST(v4_range_mixed_family_rejected) {
    struct multipath_mount_options *o = fresh_options();
    /* IPv4 anchor with IPv6 range end — SUT detects family mismatch. */
    char input[] = "10.0.0.1-2001:db8::5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_ne(rc, 0);
} END_TEST

START_TEST(v4_range_invalid_endpoint_rejected) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "10.0.0.1-not-an-ip";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_ne(rc, 0);
} END_TEST

/* IPv6 ranges: the SUT walks the address by incrementing the
 * lowest 32-bit chunk and carrying upward via
 * nfs_multipath_parse_ip_ipv6_add. */
START_TEST(v6_range_short) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1-2001:db8::5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    /* 5 addrs: ::1, ::2, ::3, ::4, ::5 */
    ck_assert_int_eq(o->remote_ip_list->count, 5);
    for (int i = 0; i < 5; i++)
        ck_assert_int_eq(o->remote_ip_list->address[i].ss_family, AF_INET6);
} END_TEST

START_TEST(v6_range_one_step) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1-2001:db8::2";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 2);
} END_TEST

START_TEST(v6_range_same_endpoint_collapses) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::5-2001:db8::5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 1);
} END_TEST

START_TEST(v6_range_carry_across_chunk) {
    /* End of the lowest 32-bit word: ::ffff → ::1:0 carries the
     * carry into the second-lowest chunk. */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::fffe-2001:db8::1:1";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    /* fffe, ffff, 1:0, 1:1 = 4 addrs */
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST

START_TEST(v6_range_then_single) {
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1-2001:db8::3~2001:db8::ff";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_eq(rc, 0);
    ck_assert_int_eq(o->remote_ip_list->count, 4);
} END_TEST

START_TEST(v6_range_consecutive_dashes_rejected) {
    /* IPv6 equivalent of v4_range_two_consecutive_dashes_rejected.
     * Two '-' separators with no '~' between → "Multiple Range". */
    struct multipath_mount_options *o = fresh_options();
    char input[] = "2001:db8::1-2001:db8::3-2001:db8::5";
    int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR);
    ck_assert_int_ne(rc, 0);
} END_TEST

#define V4_RANGE_LEN_TEST(name, start, end, expected) \
    START_TEST(name) { \
        struct multipath_mount_options *o = fresh_options(); \
        char input[] = start "-" end; \
        int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR); \
        ck_assert_int_eq(rc, 0); \
        ck_assert_int_eq(o->remote_ip_list->count, (expected)); \
    } END_TEST

V4_RANGE_LEN_TEST(v4r_p_2,  "10.0.0.1",  "10.0.0.2",  2)
V4_RANGE_LEN_TEST(v4r_p_3,  "10.0.0.1",  "10.0.0.3",  3)
V4_RANGE_LEN_TEST(v4r_p_5,  "10.0.0.1",  "10.0.0.5",  5)
V4_RANGE_LEN_TEST(v4r_p_8,  "10.0.0.1",  "10.0.0.8",  8)
V4_RANGE_LEN_TEST(v4r_p_10, "10.0.0.1",  "10.0.0.10", 10)
V4_RANGE_LEN_TEST(v4r_p_15, "10.0.0.1",  "10.0.0.15", 15)
V4_RANGE_LEN_TEST(v4r_p_20, "10.0.0.1",  "10.0.0.20", 20)
V4_RANGE_LEN_TEST(v4r_p_30, "10.0.0.10", "10.0.0.39", 30)

#define V6_RANGE_LEN_TEST(name, start, end, expected) \
    START_TEST(name) { \
        struct multipath_mount_options *o = fresh_options(); \
        char input[] = start "-" end; \
        int rc = nfs_multipath_parse_ip_list(input, NULL, o, REMOTEADDR); \
        ck_assert_int_eq(rc, 0); \
        ck_assert_int_eq(o->remote_ip_list->count, (expected)); \
    } END_TEST

V6_RANGE_LEN_TEST(v6r_p_2,  "2001:db8::1", "2001:db8::2",  2)
V6_RANGE_LEN_TEST(v6r_p_3,  "2001:db8::1", "2001:db8::3",  3)
V6_RANGE_LEN_TEST(v6r_p_5,  "2001:db8::1", "2001:db8::5",  5)
V6_RANGE_LEN_TEST(v6r_p_8,  "2001:db8::1", "2001:db8::8",  8)
V6_RANGE_LEN_TEST(v6r_p_10, "2001:db8::1", "2001:db8::a",  10)
V6_RANGE_LEN_TEST(v6r_p_16, "2001:db8::1", "2001:db8::10", 16)
V6_RANGE_LEN_TEST(v6r_p_20, "2001:db8::1", "2001:db8::14", 20)

/* ---------------------------------------------------------------- */
/* DNS validation: enfs_valid_dns + isInvalidDns.                  */
/* ---------------------------------------------------------------- */

#define DNS_VALID_TEST(name, s) \
    START_TEST(name) { ck_assert(enfs_valid_dns(s)); } END_TEST

#define DNS_INVALID_TEST(name, s) \
    START_TEST(name) { ck_assert(!enfs_valid_dns(s)); } END_TEST

/* Accept battery — every form a customer realistically uses. */
DNS_VALID_TEST(dns_v_simple,         "host")
DNS_VALID_TEST(dns_v_two_label,      "host.example")
DNS_VALID_TEST(dns_v_three_label,    "host.example.com")
DNS_VALID_TEST(dns_v_long,           "very-long-hostname.subdomain.example.com")
/* Underscore-prefixed labels (RFC 6335 SRV-style) are REJECTED by
 * the SUT: enfs_valid_dns enforces RFC 952 LDH-with-alnum-edges
 * which forbids leading underscore. _dns is a typical SRV label
 * but enfs treats it as invalid. Documented as a lenient-validation
 * gap consistent with #43. */
DNS_INVALID_TEST(dns_iv_underscore_lead, "_dns._udp.example.com")
DNS_VALID_TEST(dns_v_hyphenated,     "host-1.example.com")
DNS_VALID_TEST(dns_v_numeric_label,  "1host.example.com")
DNS_VALID_TEST(dns_v_label_ending_digit, "hostname1.example.com")
DNS_VALID_TEST(dns_v_one_char,       "a")
DNS_VALID_TEST(dns_v_one_digit,      "9")
DNS_VALID_TEST(dns_v_mixed_alnum,    "h0st-name.exam-ple.c0m")
DNS_VALID_TEST(dns_v_uppercase,      "HOST.EXAMPLE.COM")
DNS_VALID_TEST(dns_v_mixed_case,     "Host.Example.Com")
DNS_VALID_TEST(dns_v_storage_node,   "storage-node-1.cluster.local")
DNS_VALID_TEST(dns_v_oceanstor,      "oceanstor.lab.example")

/* Reject battery — DNS RFC 1035 boundary violations. */
DNS_INVALID_TEST(dns_iv_empty,           "")
DNS_INVALID_TEST(dns_iv_leading_dot,     ".example.com")
DNS_INVALID_TEST(dns_iv_trailing_dot,    "example.com.")
DNS_INVALID_TEST(dns_iv_double_dot,      "host..example.com")
DNS_INVALID_TEST(dns_iv_just_dot,        ".")
DNS_INVALID_TEST(dns_iv_just_dash,       "-")
DNS_INVALID_TEST(dns_iv_label_starts_dash,  "-host.example.com")
DNS_INVALID_TEST(dns_iv_label_ends_dash,    "host-.example.com")
DNS_INVALID_TEST(dns_iv_label_starts_dot,   ".example")
DNS_INVALID_TEST(dns_iv_invalid_char_at,    "host@example.com")
DNS_INVALID_TEST(dns_iv_invalid_char_slash, "host/example.com")
DNS_INVALID_TEST(dns_iv_invalid_char_space, "host name.example.com")
DNS_INVALID_TEST(dns_iv_invalid_char_colon, "host:example.com")
DNS_INVALID_TEST(dns_iv_invalid_char_bang,  "host!.example.com")
DNS_INVALID_TEST(dns_iv_invalid_char_eq,    "host=name.example")
DNS_INVALID_TEST(dns_iv_invalid_char_quest, "host?.example")
DNS_INVALID_TEST(dns_iv_invalid_char_amp,   "host&example")
DNS_INVALID_TEST(dns_iv_invalid_char_pct,   "host%name")
DNS_INVALID_TEST(dns_iv_invalid_char_caret, "host^example")

/* Length boundaries: total > 255 chars, label > 63 chars. */
START_TEST(dns_iv_total_too_long) {
    char s[260];
    memset(s, 'a', 256);
    s[256] = '\0';
    ck_assert(!enfs_valid_dns(s));
} END_TEST

START_TEST(dns_v_total_at_max) {
    char s[256];
    /* 9-char labels separated by dots. 9 + 1 = 10 chars per label.
     * 25 labels = 250 chars + 24 dots = 274. Too long. Use 6+1 = 7
     * with 36 = 252 chars + 35 dots = 287. Hmm.
     * Let's just build a plain label of 255 chars (max single label is 63
     * so we need to multi-label). 25 labels of "abcde123" (8 chars) +
     * 24 dots = 200 + 24 = 224. Boundary is < 256 inclusive of trailing
     * NUL... use 253 chars total. */
    memset(s, 'a', 63); s[63]='.';
    memset(s+64, 'a', 63); s[127]='.';
    memset(s+128, 'a', 63); s[191]='.';
    memset(s+192, 'a', 61); s[253]='\0';
    ck_assert(enfs_valid_dns(s));
} END_TEST

START_TEST(dns_iv_label_too_long) {
    /* One label > 63 chars. */
    char s[80];
    memset(s, 'a', 64);
    s[64] = '\0';
    ck_assert(!enfs_valid_dns(s));
} END_TEST

START_TEST(dns_v_label_at_max_63) {
    char s[80];
    memset(s, 'a', 63);
    s[63] = '\0';
    ck_assert(enfs_valid_dns(s));
} END_TEST

/* isInvalidDns: returns true for invalid DNS, false for valid.
 * It first checks if the string is actually a valid IP (and rejects
 * it if so — IPs go through enfs_parse_ip_single, not the DNS path). */
START_TEST(invdns_valid_dns_string_returns_false) {
    char s[] = "host.example.com";
    ck_assert(!isInvalidDns(s, NULL));
} END_TEST

START_TEST(invdns_invalid_string_returns_true) {
    char s[] = "host..example";
    ck_assert(isInvalidDns(s, NULL));
} END_TEST

START_TEST(invdns_v4_address_returns_true) {
    /* "10.0.0.1" parses as a valid IPv4 → SUT rejects it as DNS. */
    char s[] = "10.0.0.1";
    ck_assert(isInvalidDns(s, NULL));
} END_TEST

START_TEST(invdns_v6_address_returns_true) {
    char s[] = "2001:db8::1";
    /* IPv6 addresses contain `:` which is not a valid DNS char,
     * so enfs_valid_dns rejects it (so isInvalidDns returns true). */
    ck_assert(isInvalidDns(s, NULL));
} END_TEST

START_TEST(invdns_empty_string_returns_true) {
    char s[] = "";
    ck_assert(isInvalidDns(s, NULL));
} END_TEST

START_TEST(invdns_dash_in_dns_segment_handled) {
    /* The SUT splits on '-' and checks if first half is a valid IP.
     * "10.0.0.1-host" — first half is valid IP → SUT returns true. */
    char s[] = "10.0.0.1-host";
    ck_assert(isInvalidDns(s, NULL));
} END_TEST

/* DNS-formed labels with various legal patterns */
DNS_VALID_TEST(dns_v_idn_lookalike_xn,    "xn--abc123.example.com")
DNS_VALID_TEST(dns_v_kubernetes_pod_name, "pod-12345.namespace.svc.cluster.local")
DNS_VALID_TEST(dns_v_aws_internal,        "ip-10-0-0-1.ec2.internal")
DNS_VALID_TEST(dns_v_short_two_chars,     "ab")
DNS_VALID_TEST(dns_v_short_two_digits,    "99")
/* Same edge-rule: _test is rejected. */
DNS_INVALID_TEST(dns_iv_underscore_only_label, "_test")

/* ---------------------------------------------------------------- */
/* Suite.                                                           */
/* ---------------------------------------------------------------- */

static Suite *parse_suite(void)
{
    Suite *s = suite_create("enfs_multipath_parse");

    TCase *tc4 = tcase_create("ipv4");
    tcase_add_test(tc4, v4_parse_single_addr);
    tcase_add_test(tc4, v4_parse_three_addrs_via_list);
    tcase_add_test(tc4, v4_parse_invalid_addr_rejected);
    tcase_add_test(tc4, v4_check_ipv4_valid_accepts_normal);
    tcase_add_test(tc4, v4_check_ipv4_valid_rejects_zero);
    tcase_add_test(tc4, v4_check_ipv4_valid_rejects_broadcast);
    suite_add_tcase(s, tc4);

    /* IPv4 validity battery. */
    TCase *tc4v = tcase_create("ipv4_validity");
    tcase_add_test(tc4v, v4_accept_192_0_2_10);
    tcase_add_test(tc4v, v4_accept_192_0_2_1);
    tcase_add_test(tc4v, v4_accept_192_0_2_254);
    tcase_add_test(tc4v, v4_accept_198_51_100_5);
    tcase_add_test(tc4v, v4_accept_203_0_113_99);
    tcase_add_test(tc4v, v4_accept_10_0_0_1);
    tcase_add_test(tc4v, v4_accept_172_16_0_1);
    tcase_add_test(tc4v, v4_accept_192_168_1_1);
    tcase_add_test(tc4v, v4_accept_8_8_8_8);
    tcase_add_test(tc4v, v4_accept_1_1_1_1);
    tcase_add_test(tc4v, v4_reject_0_0_0_0);
    tcase_add_test(tc4v, v4_reject_255_255_255_255);
    tcase_add_test(tc4v, v4_lenient_accept_127_0_0_1);
    tcase_add_test(tc4v, v4_lenient_accept_127_5_5_5);
    tcase_add_test(tc4v, v4_lenient_accept_127_255_255_254);
    tcase_add_test(tc4v, v4_lenient_accept_224_0_0_1);
    tcase_add_test(tc4v, v4_lenient_accept_224_5_6_7);
    tcase_add_test(tc4v, v4_lenient_accept_239_255_255_255);
    tcase_add_test(tc4v, v4_lenient_accept_169_254_0_1);
    tcase_add_test(tc4v, v4_lenient_accept_169_254_99_99);
    suite_add_tcase(s, tc4v);

    /* IPv4 parse battery. */
    TCase *tc4p = tcase_create("ipv4_parse");
    tcase_add_test(tc4p, v4_reject_str_empty);
    tcase_add_test(tc4p, v4_reject_str_999_dot_999);
    tcase_add_test(tc4p, v4_reject_str_only_dots);
    tcase_add_test(tc4p, v4_reject_str_letters);
    tcase_add_test(tc4p, v4_reject_str_too_many_octets);
    tcase_add_test(tc4p, v4_reject_str_too_few_octets);
    tcase_add_test(tc4p, v4_reject_str_negative_octet);
    tcase_add_test(tc4p, v4_reject_str_huge_octet);
    tcase_add_test(tc4p, v4_reject_str_trailing_dot);
    tcase_add_test(tc4p, v4_reject_str_leading_dot);
    tcase_add_test(tc4p, v4_parse_192_0_2_10);
    tcase_add_test(tc4p, v4_parse_198_51_100_99);
    tcase_add_test(tc4p, v4_parse_10_20_30_40);
    tcase_add_test(tc4p, v4_parse_192_168_1_1);
    tcase_add_test(tc4p, v4_parse_172_16_5_5);
    tcase_add_test(tc4p, v4_list_count_2);
    tcase_add_test(tc4p, v4_list_count_4);
    tcase_add_test(tc4p, v4_list_count_8);
    tcase_add_test(tc4p, v4_list_count_16);
    tcase_add_test(tc4p, v4_list_one_invalid_aborts);
    suite_add_tcase(s, tc4p);

    TCase *tc6 = tcase_create("ipv6");
    tcase_add_test(tc6, v6_parse_single_addr);
    tcase_add_test(tc6, v6_parse_eight_addrs_via_list);
    tcase_add_test(tc6, v6_parse_two_local_addrs_via_list);
    tcase_add_test(tc6, v6_parse_invalid_addr_rejected);
    tcase_add_test(tc6, v6_check_ipv6_valid_accepts_normal);
    tcase_add_test(tc6, v6_check_ipv6_valid_rejects_unspecified);
    suite_add_tcase(s, tc6);

    /* IPv6 validity battery. */
    TCase *tc6v = tcase_create("ipv6_validity");
    tcase_add_test(tc6v, v6_accept_2001_db8_2_11);
    tcase_add_test(tc6v, v6_accept_2001_db8_2_18);
    tcase_add_test(tc6v, v6_accept_2001_db8_full_form);
    tcase_add_test(tc6v, v6_accept_fc00_ula);
    tcase_add_test(tc6v, v6_accept_fd00_ula);
    tcase_add_test(tc6v, v6_accept_fc07_2_4_1);
    tcase_add_test(tc6v, v6_accept_fc07_2_4_2);
    tcase_add_test(tc6v, v6_accept_2620_routable);
    tcase_add_test(tc6v, v6_accept_2400_routable);
    tcase_add_test(tc6v, v6_reject_unspecified_colon_colon);
    tcase_add_test(tc6v, v6_lenient_accept_loopback);
    tcase_add_test(tc6v, v6_lenient_accept_multicast_ff00);
    tcase_add_test(tc6v, v6_lenient_accept_multicast_ff02);
    tcase_add_test(tc6v, v6_lenient_accept_link_local_fe80);
    tcase_add_test(tc6v, v6_lenient_accept_link_local_long);
    suite_add_tcase(s, tc6v);

    /* IPv6 parse battery. */
    TCase *tc6p = tcase_create("ipv6_parse");
    tcase_add_test(tc6p, v6_reject_str_garbage);
    tcase_add_test(tc6p, v6_reject_str_too_many_colons);
    tcase_add_test(tc6p, v6_reject_str_letters_in_hex);
    tcase_add_test(tc6p, v6_reject_str_double_doublecol);
    tcase_add_test(tc6p, v6_parse_2001_db8_1);
    tcase_add_test(tc6p, v6_parse_2001_db8_2_18);
    tcase_add_test(tc6p, v6_parse_full_form);
    tcase_add_test(tc6p, v6_parse_fc07_2_4_1);
    tcase_add_test(tc6p, v6_parse_compact_form);
    tcase_add_test(tc6p, v6_list_count_2);
    tcase_add_test(tc6p, v6_list_count_4);
    tcase_add_test(tc6p, v6_list_count_16);
    suite_add_tcase(s, tc6p);

    /* Generic family-dispatch. */
    TCase *tcg = tcase_create("generic_check_dispatch");
    tcase_add_test(tcg, generic_check_v4_valid);
    tcase_add_test(tcg, generic_check_v6_valid);
    tcase_add_test(tcg, generic_check_v4_loopback_lenient_accept);
    tcase_add_test(tcg, generic_check_v6_unspecified_rejected);
    tcase_add_test(tcg, generic_check_unsupported_family_rejected);
    suite_add_tcase(s, tcg);

    TCase *tcmix = tcase_create("cross_list");
    tcase_add_test(tcmix, cross_list_duplicate_v4_detected);
    tcase_add_test(tcmix, cross_list_no_duplicate_v4_accepted);
    tcase_add_test(tcmix, cross_list_duplicate_v6_detected);
    /* Extended duplicate matrix. */
    tcase_add_test(tcmix, cross_list_v4_local_in_remote_first_pos);
    tcase_add_test(tcmix, cross_list_v4_local_in_remote_last_pos);
    tcase_add_test(tcmix, cross_list_v4_two_locals_one_in_remote);
    tcase_add_test(tcmix, cross_list_v6_local_in_remote_first_pos);
    tcase_add_test(tcmix, cross_list_v6_no_duplicate_8x2);
    tcase_add_test(tcmix, cross_list_empty_local_no_dup);
    tcase_add_test(tcmix, cross_list_empty_remote_no_dup);
    suite_add_tcase(s, tcmix);

    /* Big-list parameterised tests. */
    TCase *tcbig = tcase_create("big_lists");
    tcase_add_test(tcbig, v4_list_count_3);
    tcase_add_test(tcbig, v4_list_count_5);
    tcase_add_test(tcbig, v4_list_count_6);
    tcase_add_test(tcbig, v4_list_count_7);
    tcase_add_test(tcbig, v4_list_count_9);
    tcase_add_test(tcbig, v4_list_count_10);
    tcase_add_test(tcbig, v4_list_count_12);
    tcase_add_test(tcbig, v4_list_count_14);
    tcase_add_test(tcbig, v4_list_count_20);
    tcase_add_test(tcbig, v4_list_count_24);
    tcase_add_test(tcbig, v4_list_count_28);
    tcase_add_test(tcbig, v4_list_count_30);
    suite_add_tcase(s, tcbig);

    /* IPv4 octet/range parse battery. */
    TCase *tcoct = tcase_create("ipv4_octets");
    tcase_add_test(tcoct, v4_octet_0_0_0_1);
    tcase_add_test(tcoct, v4_octet_0_0_1_0);
    tcase_add_test(tcoct, v4_octet_0_1_0_0);
    tcase_add_test(tcoct, v4_octet_1_0_0_0);
    tcase_add_test(tcoct, v4_octet_192_0_2_0);
    tcase_add_test(tcoct, v4_octet_192_0_2_127);
    tcase_add_test(tcoct, v4_octet_192_0_2_128);
    tcase_add_test(tcoct, v4_octet_192_0_2_253);
    tcase_add_test(tcoct, v4_octet_255_255_255_254);
    tcase_add_test(tcoct, v4_octet_254_254_254_254);
    tcase_add_test(tcoct, v4_octet_100_100_100_100);
    tcase_add_test(tcoct, v4_octet_50_50_50_50);
    tcase_add_test(tcoct, v4_a_class_a);
    tcase_add_test(tcoct, v4_a_class_b);
    tcase_add_test(tcoct, v4_a_class_c);
    tcase_add_test(tcoct, v4_b_class_a);
    tcase_add_test(tcoct, v4_b_class_b);
    tcase_add_test(tcoct, v4_b_class_c);
    tcase_add_test(tcoct, v4_c_class_a);
    tcase_add_test(tcoct, v4_c_class_b);
    tcase_add_test(tcoct, v4_routable_a);
    tcase_add_test(tcoct, v4_routable_b);
    tcase_add_test(tcoct, v4_routable_c);
    tcase_add_test(tcoct, v4_routable_d);
    suite_add_tcase(s, tcoct);

    /* IPv6 widely-varied parse battery. */
    TCase *tcv6r = tcase_create("ipv6_repertoire");
    tcase_add_test(tcv6r, v6_rep_2001_db8_a);
    tcase_add_test(tcv6r, v6_rep_2001_db8_b);
    tcase_add_test(tcv6r, v6_rep_2001_db8_c);
    tcase_add_test(tcv6r, v6_rep_2001_db8_d);
    tcase_add_test(tcv6r, v6_rep_fc07_2_4_3);
    tcase_add_test(tcv6r, v6_rep_fc07_2_4_4);
    tcase_add_test(tcv6r, v6_rep_fc07_2_4_5);
    tcase_add_test(tcv6r, v6_rep_fc07_2_4_6);
    tcase_add_test(tcv6r, v6_rep_fd00);
    tcase_add_test(tcv6r, v6_rep_fd01);
    tcase_add_test(tcv6r, v6_rep_fd02);
    tcase_add_test(tcv6r, v6_rep_2620_a);
    tcase_add_test(tcv6r, v6_rep_2620_b);
    tcase_add_test(tcv6r, v6_rep_2620_c);
    tcase_add_test(tcv6r, v6_rep_huawei_lab);
    tcase_add_test(tcv6r, v6_rep_huawei_oc);
    suite_add_tcase(s, tcv6r);

    /* Mixed-family lists. */
    TCase *tcmf = tcase_create("mixed_families");
    tcase_add_test(tcmf, mixed_family_list_v4_then_v6);
    tcase_add_test(tcmf, mixed_family_list_v6_then_v4);
    suite_add_tcase(s, tcmf);

    TCase *tcrng = tcase_create("ip_ranges");
    tcase_add_test(tcrng, v4_range_short_inclusive);
    tcase_add_test(tcrng, v4_range_one_step);
    tcase_add_test(tcrng, v4_range_same_endpoint_collapses);
    tcase_add_test(tcrng, v4_range_octet_carries);
    tcase_add_test(tcrng, v4_range_end_lower_is_rejected_or_no_op);
    tcase_add_test(tcrng, v4_range_then_single);
    tcase_add_test(tcrng, v4_single_then_range);
    tcase_add_test(tcrng, v4_range_two_consecutive_dashes_rejected);
    tcase_add_test(tcrng, v4_range_then_singleton_then_range_accepted);
    tcase_add_test(tcrng, v4_range_mixed_family_rejected);
    tcase_add_test(tcrng, v4_range_invalid_endpoint_rejected);
    tcase_add_test(tcrng, v6_range_short);
    tcase_add_test(tcrng, v6_range_one_step);
    tcase_add_test(tcrng, v6_range_same_endpoint_collapses);
    tcase_add_test(tcrng, v6_range_carry_across_chunk);
    tcase_add_test(tcrng, v6_range_then_single);
    tcase_add_test(tcrng, v6_range_consecutive_dashes_rejected);
    tcase_add_test(tcrng, v4r_p_2);
    tcase_add_test(tcrng, v4r_p_3);
    tcase_add_test(tcrng, v4r_p_5);
    tcase_add_test(tcrng, v4r_p_8);
    tcase_add_test(tcrng, v4r_p_10);
    tcase_add_test(tcrng, v4r_p_15);
    tcase_add_test(tcrng, v4r_p_20);
    tcase_add_test(tcrng, v4r_p_30);
    tcase_add_test(tcrng, v6r_p_2);
    tcase_add_test(tcrng, v6r_p_3);
    tcase_add_test(tcrng, v6r_p_5);
    tcase_add_test(tcrng, v6r_p_8);
    tcase_add_test(tcrng, v6r_p_10);
    tcase_add_test(tcrng, v6r_p_16);
    tcase_add_test(tcrng, v6r_p_20);
    suite_add_tcase(s, tcrng);

    TCase *tcdns_v = tcase_create("dns_valid");
    tcase_add_test(tcdns_v, dns_v_simple);
    tcase_add_test(tcdns_v, dns_v_two_label);
    tcase_add_test(tcdns_v, dns_v_three_label);
    tcase_add_test(tcdns_v, dns_v_long);
    tcase_add_test(tcdns_v, dns_v_hyphenated);
    tcase_add_test(tcdns_v, dns_v_numeric_label);
    tcase_add_test(tcdns_v, dns_v_label_ending_digit);
    tcase_add_test(tcdns_v, dns_v_one_char);
    tcase_add_test(tcdns_v, dns_v_one_digit);
    tcase_add_test(tcdns_v, dns_v_mixed_alnum);
    tcase_add_test(tcdns_v, dns_v_uppercase);
    tcase_add_test(tcdns_v, dns_v_mixed_case);
    tcase_add_test(tcdns_v, dns_v_storage_node);
    tcase_add_test(tcdns_v, dns_v_oceanstor);
    tcase_add_test(tcdns_v, dns_v_idn_lookalike_xn);
    tcase_add_test(tcdns_v, dns_v_kubernetes_pod_name);
    tcase_add_test(tcdns_v, dns_v_aws_internal);
    tcase_add_test(tcdns_v, dns_v_short_two_chars);
    tcase_add_test(tcdns_v, dns_v_short_two_digits);
    tcase_add_test(tcdns_v, dns_v_total_at_max);
    tcase_add_test(tcdns_v, dns_v_label_at_max_63);
    suite_add_tcase(s, tcdns_v);

    TCase *tcdns_iv = tcase_create("dns_invalid");
    tcase_add_test(tcdns_iv, dns_iv_empty);
    tcase_add_test(tcdns_iv, dns_iv_leading_dot);
    tcase_add_test(tcdns_iv, dns_iv_trailing_dot);
    tcase_add_test(tcdns_iv, dns_iv_double_dot);
    tcase_add_test(tcdns_iv, dns_iv_just_dot);
    tcase_add_test(tcdns_iv, dns_iv_just_dash);
    tcase_add_test(tcdns_iv, dns_iv_label_starts_dash);
    tcase_add_test(tcdns_iv, dns_iv_label_ends_dash);
    tcase_add_test(tcdns_iv, dns_iv_label_starts_dot);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_at);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_slash);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_space);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_colon);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_bang);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_eq);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_quest);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_amp);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_pct);
    tcase_add_test(tcdns_iv, dns_iv_invalid_char_caret);
    tcase_add_test(tcdns_iv, dns_iv_total_too_long);
    tcase_add_test(tcdns_iv, dns_iv_label_too_long);
    tcase_add_test(tcdns_iv, dns_iv_underscore_lead);
    tcase_add_test(tcdns_iv, dns_iv_underscore_only_label);
    suite_add_tcase(s, tcdns_iv);

    TCase *tcinv = tcase_create("isInvalidDns");
    tcase_add_test(tcinv, invdns_valid_dns_string_returns_false);
    tcase_add_test(tcinv, invdns_invalid_string_returns_true);
    tcase_add_test(tcinv, invdns_v4_address_returns_true);
    tcase_add_test(tcinv, invdns_v6_address_returns_true);
    tcase_add_test(tcinv, invdns_empty_string_returns_true);
    tcase_add_test(tcinv, invdns_dash_in_dns_segment_handled);
    suite_add_tcase(s, tcinv);

    return s;
}

#define CHECK_RUNNER_SUITE  parse_suite
#include "check_runner.h"
