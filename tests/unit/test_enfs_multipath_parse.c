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

    TCase *tc6 = tcase_create("ipv6");
    tcase_add_test(tc6, v6_parse_single_addr);
    tcase_add_test(tc6, v6_parse_eight_addrs_via_list);
    tcase_add_test(tc6, v6_parse_two_local_addrs_via_list);
    tcase_add_test(tc6, v6_parse_invalid_addr_rejected);
    tcase_add_test(tc6, v6_check_ipv6_valid_accepts_normal);
    tcase_add_test(tc6, v6_check_ipv6_valid_rejects_unspecified);
    suite_add_tcase(s, tc6);

    TCase *tcmix = tcase_create("cross_list");
    tcase_add_test(tcmix, cross_list_duplicate_v4_detected);
    tcase_add_test(tcmix, cross_list_no_duplicate_v4_accepted);
    tcase_add_test(tcmix, cross_list_duplicate_v6_detected);
    suite_add_tcase(s, tcmix);

    return s;
}

#define CHECK_RUNNER_SUITE  parse_suite
#include "check_runner.h"
