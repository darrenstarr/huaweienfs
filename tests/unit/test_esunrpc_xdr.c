/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_esunrpc_xdr.c — unit tests for vendor/esunrpc/net/esunrpc/
 * xdr.c (RFC 4506 XDR encoding/decoding helpers).
 *
 * XDR is pure byte manipulation — every function is testable in
 * userspace without kernel state. The exported surface is large
 * (~80 functions); this suite covers the most-used encode/decode
 * primitives + boundary conditions.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <linux/types.h>
#include <esunrpc/xdr.h>

#define BUFSZ 1024

static __be32 *fresh_buf(void) {
    __be32 *b = calloc(BUFSZ, sizeof(__be32));
    ck_assert_ptr_nonnull(b);
    return b;
}

/* ============================================================ */
/* esunrpc_xdr_encode_opaque_fixed                              */
/* ============================================================ */

START_TEST(encode_opaque_fixed_4_bytes) {
    __be32 *p = fresh_buf();
    const char *src = "1234";
    __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, src, 4);
    /* 4 bytes → exactly one 4-byte word, no padding. */
    ck_assert_ptr_eq(out, p + 1);
    ck_assert_int_eq(memcmp(p, "1234", 4), 0);
    free(p);
} END_TEST

START_TEST(encode_opaque_fixed_unaligned_pads) {
    __be32 *p = fresh_buf();
    /* 7 bytes → padded to 8 (two words). */
    __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, "1234567", 7);
    ck_assert_ptr_eq(out, p + 2);
    ck_assert_int_eq(memcmp(p, "1234567\0", 8), 0);
    free(p);
} END_TEST

START_TEST(encode_opaque_fixed_one_byte) {
    __be32 *p = fresh_buf();
    __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, "X", 1);
    ck_assert_ptr_eq(out, p + 1);
    ck_assert_int_eq(((const char *)p)[0], 'X');
    /* Padding bytes 1-3 should be zero. */
    ck_assert_int_eq(((const char *)p)[1], 0);
    ck_assert_int_eq(((const char *)p)[2], 0);
    ck_assert_int_eq(((const char *)p)[3], 0);
    free(p);
} END_TEST

START_TEST(encode_opaque_fixed_zero_bytes) {
    __be32 *p = fresh_buf();
    __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, "", 0);
    /* Zero bytes → no words. */
    ck_assert_ptr_eq(out, p);
    free(p);
} END_TEST

#define ENCODE_FIXED_TEST(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        char src[n]; for (int i = 0; i < n; i++) src[i] = 'a' + (i % 26); \
        unsigned int words = (n + 3) >> 2; \
        __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, src, n); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_int_eq(memcmp(p, src, n), 0); \
        free(p); \
    } END_TEST

ENCODE_FIXED_TEST(encode_fixed_2,    2)
ENCODE_FIXED_TEST(encode_fixed_3,    3)
ENCODE_FIXED_TEST(encode_fixed_5,    5)
ENCODE_FIXED_TEST(encode_fixed_8,    8)
ENCODE_FIXED_TEST(encode_fixed_15,   15)
ENCODE_FIXED_TEST(encode_fixed_16,   16)
ENCODE_FIXED_TEST(encode_fixed_17,   17)
ENCODE_FIXED_TEST(encode_fixed_31,   31)
ENCODE_FIXED_TEST(encode_fixed_32,   32)
ENCODE_FIXED_TEST(encode_fixed_64,   64)
ENCODE_FIXED_TEST(encode_fixed_100,  100)
ENCODE_FIXED_TEST(encode_fixed_255,  255)

/* ============================================================ */
/* esunrpc_xdr_encode_opaque                                    */
/* ============================================================ */

START_TEST(encode_opaque_writes_length_prefix) {
    __be32 *p = fresh_buf();
    __be32 *out = esunrpc_xdr_encode_opaque(p, "ABCD", 4);
    /* First word: length (4); next word: "ABCD". */
    ck_assert_uint_eq(ntohl(p[0]), 4);
    ck_assert_int_eq(memcmp(&p[1], "ABCD", 4), 0);
    ck_assert_ptr_eq(out, p + 2);
    free(p);
} END_TEST

START_TEST(encode_opaque_pads_payload) {
    __be32 *p = fresh_buf();
    esunrpc_xdr_encode_opaque(p, "AB", 2);
    ck_assert_uint_eq(ntohl(p[0]), 2);
    /* Payload "AB\0\0". */
    ck_assert_int_eq(((const char *)&p[1])[0], 'A');
    ck_assert_int_eq(((const char *)&p[1])[1], 'B');
    free(p);
} END_TEST

START_TEST(encode_opaque_zero_length) {
    __be32 *p = fresh_buf();
    __be32 *out = esunrpc_xdr_encode_opaque(p, "", 0);
    ck_assert_uint_eq(ntohl(p[0]), 0);
    ck_assert_ptr_eq(out, p + 1);
    free(p);
} END_TEST

#define ENCODE_OPAQUE_TEST(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        char src[n]; for (int i = 0; i < n; i++) src[i] = (char)i; \
        unsigned int words = 1 + ((n + 3) >> 2); \
        __be32 *out = esunrpc_xdr_encode_opaque(p, src, n); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_uint_eq(ntohl(p[0]), (uint32_t)n); \
        ck_assert_int_eq(memcmp(&p[1], src, n), 0); \
        free(p); \
    } END_TEST

ENCODE_OPAQUE_TEST(encode_opaque_1,   1)
ENCODE_OPAQUE_TEST(encode_opaque_2,   2)
ENCODE_OPAQUE_TEST(encode_opaque_3,   3)
ENCODE_OPAQUE_TEST(encode_opaque_5,   5)
ENCODE_OPAQUE_TEST(encode_opaque_7,   7)
ENCODE_OPAQUE_TEST(encode_opaque_8,   8)
ENCODE_OPAQUE_TEST(encode_opaque_16,  16)
ENCODE_OPAQUE_TEST(encode_opaque_31,  31)
ENCODE_OPAQUE_TEST(encode_opaque_32,  32)
ENCODE_OPAQUE_TEST(encode_opaque_64,  64)
ENCODE_OPAQUE_TEST(encode_opaque_127, 127)
ENCODE_OPAQUE_TEST(encode_opaque_128, 128)
ENCODE_OPAQUE_TEST(encode_opaque_255, 255)

/* ============================================================ */
/* esunrpc_xdr_encode_string                                    */
/* ============================================================ */

#define ENCODE_STRING_TEST(name, str) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        size_t n = strlen(str); \
        unsigned int words = 1 + ((n + 3) >> 2); \
        __be32 *out = esunrpc_xdr_encode_string(p, str); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_uint_eq(ntohl(p[0]), (uint32_t)n); \
        ck_assert_int_eq(memcmp(&p[1], str, n), 0); \
        free(p); \
    } END_TEST

ENCODE_STRING_TEST(encode_string_empty,         "")
ENCODE_STRING_TEST(encode_string_one,           "x")
ENCODE_STRING_TEST(encode_string_word_aligned,  "abcd")
ENCODE_STRING_TEST(encode_string_short,         "hello")
ENCODE_STRING_TEST(encode_string_eight,         "12345678")
ENCODE_STRING_TEST(encode_string_nine,          "123456789")
ENCODE_STRING_TEST(encode_string_long,
    "the quick brown fox jumps over the lazy dog")
ENCODE_STRING_TEST(encode_string_with_punct,
    "hello, world! @#$%")
ENCODE_STRING_TEST(encode_string_127,
    "12345678901234567890123456789012345678901234567890"
    "12345678901234567890123456789012345678901234567890"
    "1234567890123456")
ENCODE_STRING_TEST(encode_string_128,
    "12345678901234567890123456789012345678901234567890"
    "12345678901234567890123456789012345678901234567890"
    "12345678901234567890123456789")  /* 129 — close to 128 */

/* ============================================================ */
/* esunrpc_xdr_encode_netobj                                    */
/* ============================================================ */

START_TEST(encode_netobj_basic) {
    __be32 *p = fresh_buf();
    struct xdr_netobj obj = { .len = 5, .data = (unsigned char *)"hello" };
    __be32 *out = esunrpc_xdr_encode_netobj(p, &obj);
    ck_assert_uint_eq(ntohl(p[0]), 5);
    ck_assert_int_eq(memcmp(&p[1], "hello", 5), 0);
    /* 5 bytes → 1 length word + 2 payload words. */
    ck_assert_ptr_eq(out, p + 3);
    free(p);
} END_TEST

START_TEST(encode_netobj_empty) {
    __be32 *p = fresh_buf();
    struct xdr_netobj obj = { .len = 0, .data = NULL };
    __be32 *out = esunrpc_xdr_encode_netobj(p, &obj);
    ck_assert_uint_eq(ntohl(p[0]), 0);
    ck_assert_ptr_eq(out, p + 1);
    free(p);
} END_TEST

START_TEST(encode_netobj_aligned_4) {
    __be32 *p = fresh_buf();
    struct xdr_netobj obj = { .len = 4, .data = (unsigned char *)"ABCD" };
    __be32 *out = esunrpc_xdr_encode_netobj(p, &obj);
    ck_assert_uint_eq(ntohl(p[0]), 4);
    ck_assert_int_eq(memcmp(&p[1], "ABCD", 4), 0);
    ck_assert_ptr_eq(out, p + 2);
    free(p);
} END_TEST

/* ============================================================ */
/* xdr_init_encode + xdr_reserve_space                           */
/* ============================================================ */

static struct xdr_buf *fresh_xdr_buf(size_t len)
{
    struct xdr_buf *b = calloc(1, sizeof(*b));
    b->head[0].iov_base = calloc(1, len);
    b->head[0].iov_len = len;
    b->buflen = len;
    b->len = 0;
    return b;
}

START_TEST(init_encode_sets_pointers) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    __be32 *start = buf->head[0].iov_base;
    esunrpc_xdr_init_encode(&xdr, buf, start, NULL);
    ck_assert_ptr_eq(xdr.p, start);
    ck_assert_ptr_eq(xdr.buf, buf);
} END_TEST

START_TEST(reserve_space_advances) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    __be32 *r = esunrpc_xdr_reserve_space(&xdr, 8);
    ck_assert_ptr_nonnull(r);
} END_TEST

START_TEST(reserve_space_zero_returns_pointer) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    __be32 *r = esunrpc_xdr_reserve_space(&xdr, 0);
    ck_assert_ptr_nonnull(r);
} END_TEST

START_TEST(stream_pos_is_zero_after_init) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_uint_eq(esunrpc_xdr_stream_pos(&xdr), 0);
} END_TEST

START_TEST(stream_pos_advances_with_reserve) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    esunrpc_xdr_reserve_space(&xdr, 16);
    ck_assert_uint_eq(esunrpc_xdr_stream_pos(&xdr), 16);
} END_TEST

/* ============================================================ */
/* xdr_init_decode + xdr_inline_decode                           */
/* ============================================================ */

START_TEST(init_decode_sets_pointers) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    /* Pretend the buf has 64 bytes of valid content. */
    buf->len = 64;
    buf->head[0].iov_len = 64;
    __be32 *start = buf->head[0].iov_base;
    esunrpc_xdr_init_decode(&xdr, buf, start, NULL);
    ck_assert_ptr_eq(xdr.buf, buf);
} END_TEST

START_TEST(inline_decode_reads_word) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 16;
    buf->head[0].iov_len = 16;
    __be32 *start = buf->head[0].iov_base;
    /* Plant 0xdeadbeef in the first word. */
    start[0] = htonl(0xdeadbeef);
    esunrpc_xdr_init_decode(&xdr, buf, start, NULL);
    __be32 *r = esunrpc_xdr_inline_decode(&xdr, 4);
    ck_assert_ptr_nonnull(r);
    ck_assert_uint_eq(ntohl(*r), 0xdeadbeef);
} END_TEST

START_TEST(inline_decode_past_end_returns_NULL) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(8);
    buf->len = 8;
    buf->head[0].iov_len = 8;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* Reading 8 bytes is fine. */
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 8));
    /* Reading another byte is not. */
    ck_assert_ptr_null(esunrpc_xdr_inline_decode(&xdr, 4));
} END_TEST

/* ============================================================ */
/* Encode → decode round-trip via xdr_buf.                       */
/* ============================================================ */

START_TEST(roundtrip_word_sequence) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    /* Encode three known words. */
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    __be32 *r = esunrpc_xdr_reserve_space(&xdr, 12);
    ck_assert_ptr_nonnull(r);
    r[0] = htonl(0x11111111);
    r[1] = htonl(0x22222222);
    r[2] = htonl(0x33333333);
    /* Mark buf as having 12 bytes of content + decode. */
    buf->len = 12;
    buf->head[0].iov_len = 12;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    __be32 *d0 = esunrpc_xdr_inline_decode(&xdr, 4);
    __be32 *d1 = esunrpc_xdr_inline_decode(&xdr, 4);
    __be32 *d2 = esunrpc_xdr_inline_decode(&xdr, 4);
    ck_assert_uint_eq(ntohl(*d0), 0x11111111);
    ck_assert_uint_eq(ntohl(*d1), 0x22222222);
    ck_assert_uint_eq(ntohl(*d2), 0x33333333);
} END_TEST

/* ============================================================ */
/* Suite plumbing                                               */
/* ============================================================ */

static Suite *esunrpc_xdr_suite(void)
{
    Suite *s = suite_create("esunrpc_xdr");

    TCase *t1 = tcase_create("encode_opaque_fixed");
    tcase_add_test(t1, encode_opaque_fixed_4_bytes);
    tcase_add_test(t1, encode_opaque_fixed_unaligned_pads);
    tcase_add_test(t1, encode_opaque_fixed_one_byte);
    tcase_add_test(t1, encode_opaque_fixed_zero_bytes);
    tcase_add_test(t1, encode_fixed_2);
    tcase_add_test(t1, encode_fixed_3);
    tcase_add_test(t1, encode_fixed_5);
    tcase_add_test(t1, encode_fixed_8);
    tcase_add_test(t1, encode_fixed_15);
    tcase_add_test(t1, encode_fixed_16);
    tcase_add_test(t1, encode_fixed_17);
    tcase_add_test(t1, encode_fixed_31);
    tcase_add_test(t1, encode_fixed_32);
    tcase_add_test(t1, encode_fixed_64);
    tcase_add_test(t1, encode_fixed_100);
    tcase_add_test(t1, encode_fixed_255);
    suite_add_tcase(s, t1);

    TCase *t2 = tcase_create("encode_opaque");
    tcase_add_test(t2, encode_opaque_writes_length_prefix);
    tcase_add_test(t2, encode_opaque_pads_payload);
    tcase_add_test(t2, encode_opaque_zero_length);
    tcase_add_test(t2, encode_opaque_1);
    tcase_add_test(t2, encode_opaque_2);
    tcase_add_test(t2, encode_opaque_3);
    tcase_add_test(t2, encode_opaque_5);
    tcase_add_test(t2, encode_opaque_7);
    tcase_add_test(t2, encode_opaque_8);
    tcase_add_test(t2, encode_opaque_16);
    tcase_add_test(t2, encode_opaque_31);
    tcase_add_test(t2, encode_opaque_32);
    tcase_add_test(t2, encode_opaque_64);
    tcase_add_test(t2, encode_opaque_127);
    tcase_add_test(t2, encode_opaque_128);
    tcase_add_test(t2, encode_opaque_255);
    suite_add_tcase(s, t2);

    TCase *t3 = tcase_create("encode_string");
    tcase_add_test(t3, encode_string_empty);
    tcase_add_test(t3, encode_string_one);
    tcase_add_test(t3, encode_string_word_aligned);
    tcase_add_test(t3, encode_string_short);
    tcase_add_test(t3, encode_string_eight);
    tcase_add_test(t3, encode_string_nine);
    tcase_add_test(t3, encode_string_long);
    tcase_add_test(t3, encode_string_with_punct);
    tcase_add_test(t3, encode_string_127);
    tcase_add_test(t3, encode_string_128);
    suite_add_tcase(s, t3);

    TCase *t4 = tcase_create("encode_netobj");
    tcase_add_test(t4, encode_netobj_basic);
    tcase_add_test(t4, encode_netobj_empty);
    tcase_add_test(t4, encode_netobj_aligned_4);
    suite_add_tcase(s, t4);

    TCase *t5 = tcase_create("init_encode");
    tcase_add_test(t5, init_encode_sets_pointers);
    tcase_add_test(t5, reserve_space_advances);
    tcase_add_test(t5, reserve_space_zero_returns_pointer);
    tcase_add_test(t5, stream_pos_is_zero_after_init);
    tcase_add_test(t5, stream_pos_advances_with_reserve);
    suite_add_tcase(s, t5);

    TCase *t6 = tcase_create("init_decode");
    tcase_add_test(t6, init_decode_sets_pointers);
    tcase_add_test(t6, inline_decode_reads_word);
    tcase_add_test(t6, inline_decode_past_end_returns_NULL);
    suite_add_tcase(s, t6);

    TCase *t7 = tcase_create("roundtrip");
    tcase_add_test(t7, roundtrip_word_sequence);
    suite_add_tcase(s, t7);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_xdr_suite
#include "check_runner.h"
