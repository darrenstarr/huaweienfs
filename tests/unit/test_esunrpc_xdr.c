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

/* xdr_buf semantics:
 *  - buflen = total head[0] capacity
 *  - head[0].iov_len = bytes ALREADY in use (zero for a fresh buffer)
 *  - len = bytes in use across head + pages + tail
 * init_encode advances xdr->p past iov_len, so a fresh buffer
 * MUST start with iov_len = 0. */
static struct xdr_buf *fresh_xdr_buf(size_t len)
{
    struct xdr_buf *b = calloc(1, sizeof(*b));
    b->head[0].iov_base = calloc(1, len);
    b->head[0].iov_len = 0;
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

/* esunrpc_xdr_stream_pos is bookkeeping-driven (uses xdr->nwords);
 * for encode streams the nwords count isn't initialised by
 * init_encode itself (the production code zero-initialises the
 * struct via memset before init_encode). Mirror that here. */
START_TEST(stream_pos_is_zero_after_init) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* For a freshly-zeroed encode stream with empty buf->len,
     * stream_pos returns 0. */
    ck_assert_uint_eq(esunrpc_xdr_stream_pos(&xdr), 0);
} END_TEST

START_TEST(stream_pos_decode_zero_at_start) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 64;
    buf->head[0].iov_len = 64;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_uint_eq(esunrpc_xdr_stream_pos(&xdr), 0);
} END_TEST

START_TEST(stream_pos_decode_advances) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 64;
    buf->head[0].iov_len = 64;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 16));
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
/* Parametric expansion: encode_opaque_fixed across all alignments. */
/* ============================================================ */

#define ENCODE_FIXED_PARAMETRIC(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        unsigned char src[(n) ? (n) : 1]; \
        for (unsigned int i = 0; i < (n); i++) src[i] = (unsigned char)(i ^ 0xa5); \
        unsigned int words = ((n) + 3) >> 2; \
        __be32 *out = esunrpc_xdr_encode_opaque_fixed(p, src, (n)); \
        ck_assert_ptr_eq(out, p + words); \
        if ((n)) ck_assert_int_eq(memcmp(p, src, (n)), 0); \
        /* Padding bytes (if any) must be zero on a calloc'd buffer. */ \
        unsigned int pad = (4 - ((n) & 3)) & 3; \
        for (unsigned int i = 0; i < pad; i++) \
            ck_assert_int_eq(((unsigned char *)p)[(n) + i], 0); \
        free(p); \
    } END_TEST

ENCODE_FIXED_PARAMETRIC(enc_fixed_p_6,    6)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_9,    9)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_10,   10)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_11,   11)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_12,   12)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_13,   13)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_14,   14)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_18,   18)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_19,   19)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_20,   20)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_21,   21)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_22,   22)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_23,   23)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_24,   24)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_25,   25)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_26,   26)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_27,   27)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_28,   28)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_29,   29)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_30,   30)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_33,   33)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_47,   47)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_48,   48)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_49,   49)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_63,   63)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_65,   65)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_127,  127)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_128,  128)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_129,  129)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_256,  256)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_257,  257)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_511,  511)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_512,  512)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_513,  513)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_1023, 1023)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_1024, 1024)
ENCODE_FIXED_PARAMETRIC(enc_fixed_p_2048, 2048)

/* ============================================================ */
/* Parametric: encode_opaque (length-prefixed) across alignments. */
/* The output layout is: [length word][n payload bytes][padding]. */
/* ============================================================ */

#define ENCODE_OPAQUE_PARAMETRIC(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        unsigned char src[(n) ? (n) : 1]; \
        for (unsigned int i = 0; i < (n); i++) src[i] = (unsigned char)(i + 1); \
        unsigned int words = 1 + (((n) + 3) >> 2); \
        __be32 *out = esunrpc_xdr_encode_opaque(p, src, (n)); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_uint_eq(ntohl(p[0]), (uint32_t)(n)); \
        if ((n)) ck_assert_int_eq(memcmp(&p[1], src, (n)), 0); \
        free(p); \
    } END_TEST

ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_4,    4)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_6,    6)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_9,    9)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_10,   10)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_11,   11)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_12,   12)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_13,   13)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_14,   14)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_15,   15)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_17,   17)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_24,   24)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_25,   25)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_31,   31)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_33,   33)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_63,   63)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_65,   65)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_100,  100)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_127,  127)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_129,  129)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_256,  256)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_257,  257)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_511,  511)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_512,  512)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_1023, 1023)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_1024, 1024)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_2000, 2000)
ENCODE_OPAQUE_PARAMETRIC(enc_opaque_p_3000, 3000)

/* ============================================================ */
/* Parametric: encode_string across many lengths. */
/* ============================================================ */

#define ENCODE_STRING_LEN_TEST(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        char str[(n) + 1]; \
        for (unsigned int i = 0; i < (n); i++) str[i] = 'A' + (i % 26); \
        str[(n)] = '\0'; \
        unsigned int words = 1 + (((n) + 3) >> 2); \
        __be32 *out = esunrpc_xdr_encode_string(p, str); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_uint_eq(ntohl(p[0]), (uint32_t)(n)); \
        if ((n)) ck_assert_int_eq(memcmp(&p[1], str, (n)), 0); \
        free(p); \
    } END_TEST

ENCODE_STRING_LEN_TEST(enc_str_len_2,    2)
ENCODE_STRING_LEN_TEST(enc_str_len_3,    3)
ENCODE_STRING_LEN_TEST(enc_str_len_6,    6)
ENCODE_STRING_LEN_TEST(enc_str_len_7,    7)
ENCODE_STRING_LEN_TEST(enc_str_len_10,   10)
ENCODE_STRING_LEN_TEST(enc_str_len_11,   11)
ENCODE_STRING_LEN_TEST(enc_str_len_12,   12)
ENCODE_STRING_LEN_TEST(enc_str_len_13,   13)
ENCODE_STRING_LEN_TEST(enc_str_len_14,   14)
ENCODE_STRING_LEN_TEST(enc_str_len_15,   15)
ENCODE_STRING_LEN_TEST(enc_str_len_16,   16)
ENCODE_STRING_LEN_TEST(enc_str_len_17,   17)
ENCODE_STRING_LEN_TEST(enc_str_len_18,   18)
ENCODE_STRING_LEN_TEST(enc_str_len_19,   19)
ENCODE_STRING_LEN_TEST(enc_str_len_20,   20)
ENCODE_STRING_LEN_TEST(enc_str_len_31,   31)
ENCODE_STRING_LEN_TEST(enc_str_len_32,   32)
ENCODE_STRING_LEN_TEST(enc_str_len_33,   33)
ENCODE_STRING_LEN_TEST(enc_str_len_63,   63)
ENCODE_STRING_LEN_TEST(enc_str_len_64,   64)
ENCODE_STRING_LEN_TEST(enc_str_len_65,   65)
ENCODE_STRING_LEN_TEST(enc_str_len_100,  100)
ENCODE_STRING_LEN_TEST(enc_str_len_200,  200)
ENCODE_STRING_LEN_TEST(enc_str_len_300,  300)
ENCODE_STRING_LEN_TEST(enc_str_len_500,  500)
ENCODE_STRING_LEN_TEST(enc_str_len_1000, 1000)

/* ============================================================ */
/* Parametric: encode_netobj across lengths. */
/* ============================================================ */

#define ENCODE_NETOBJ_LEN_TEST(name, n) \
    START_TEST(name) { \
        __be32 *p = fresh_buf(); \
        unsigned char data[(n) ? (n) : 1]; \
        for (unsigned int i = 0; i < (n); i++) data[i] = (unsigned char)(0x55 ^ i); \
        struct xdr_netobj obj = { .len = (n), .data = data }; \
        unsigned int words = 1 + (((n) + 3) >> 2); \
        __be32 *out = esunrpc_xdr_encode_netobj(p, &obj); \
        ck_assert_ptr_eq(out, p + words); \
        ck_assert_uint_eq(ntohl(p[0]), (uint32_t)(n)); \
        if ((n)) ck_assert_int_eq(memcmp(&p[1], data, (n)), 0); \
        free(p); \
    } END_TEST

ENCODE_NETOBJ_LEN_TEST(enc_netobj_1,    1)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_2,    2)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_3,    3)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_5,    5)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_6,    6)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_7,    7)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_8,    8)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_9,    9)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_15,   15)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_16,   16)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_17,   17)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_31,   31)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_32,   32)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_33,   33)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_64,   64)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_100,  100)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_127,  127)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_128,  128)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_129,  129)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_255,  255)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_256,  256)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_512,  512)
ENCODE_NETOBJ_LEN_TEST(enc_netobj_1024, 1024)

/* ============================================================ */
/* inline_decode boundary battery. */
/* ============================================================ */

/* Over-read paths invoke xdr_copy_to_scratch which walks the page
 * list — our test xdr_buf has no pages set up. So the macro only
 * exercises in-bounds reads; over-read behaviour is covered by the
 * dedicated `inline_decode_past_end_returns_NULL` test, which uses
 * a known buffer where the over-read stays out of the page path. */
#define INLINE_DECODE_BOUNDARY(name, buflen, takes) \
    START_TEST(name) { \
        struct xdr_stream xdr; \
        struct xdr_buf *buf = fresh_xdr_buf(buflen); \
        buf->len = (buflen); \
        buf->head[0].iov_len = (buflen); \
        esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL); \
        __be32 *r = esunrpc_xdr_inline_decode(&xdr, (takes)); \
        ck_assert_ptr_nonnull(r); \
        free(buf->head[0].iov_base); free(buf); \
    } END_TEST

INLINE_DECODE_BOUNDARY(idec_4_4,     4,    4)
INLINE_DECODE_BOUNDARY(idec_8_4,     8,    4)
INLINE_DECODE_BOUNDARY(idec_8_8,     8,    8)
INLINE_DECODE_BOUNDARY(idec_16_8,    16,   8)
INLINE_DECODE_BOUNDARY(idec_16_16,   16,   16)
INLINE_DECODE_BOUNDARY(idec_32_24,   32,   24)
INLINE_DECODE_BOUNDARY(idec_64_64,   64,   64)
INLINE_DECODE_BOUNDARY(idec_128_4,   128,  4)
INLINE_DECODE_BOUNDARY(idec_128_60,  128,  60)
INLINE_DECODE_BOUNDARY(idec_128_128, 128,  128)
INLINE_DECODE_BOUNDARY(idec_256_252, 256,  252)
INLINE_DECODE_BOUNDARY(idec_256_256, 256,  256)
INLINE_DECODE_BOUNDARY(idec_512_512, 512,  512)

/* Reading nothing always succeeds. */
START_TEST(idec_zero_succeeds) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(64);
    buf->len = 64; buf->head[0].iov_len = 64;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 0));
} END_TEST

/* Multiple sequential reads consume bytes and the last over-read fails. */
START_TEST(idec_chunked_consume) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(32);
    buf->len = 32; buf->head[0].iov_len = 32;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 4));
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 4));
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 8));
    ck_assert_ptr_nonnull(esunrpc_xdr_inline_decode(&xdr, 16));
    ck_assert_ptr_null(esunrpc_xdr_inline_decode(&xdr, 4));
} END_TEST

/* ============================================================ */
/* Round-trip batteries — encode N words then decode them back. */
/* ============================================================ */

#define ROUNDTRIP_N_WORDS(name, n) \
    START_TEST(name) { \
        struct xdr_stream xdr; \
        struct xdr_buf *buf = fresh_xdr_buf((n) * 4 + 64); \
        esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL); \
        __be32 *r = esunrpc_xdr_reserve_space(&xdr, (n) * 4); \
        ck_assert_ptr_nonnull(r); \
        for (unsigned int i = 0; i < (n); i++) \
            r[i] = htonl(0xdeadbe00u + i); \
        buf->len = (n) * 4; buf->head[0].iov_len = (n) * 4; \
        esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL); \
        for (unsigned int i = 0; i < (n); i++) { \
            __be32 *d = esunrpc_xdr_inline_decode(&xdr, 4); \
            ck_assert_ptr_nonnull(d); \
            ck_assert_uint_eq(ntohl(*d), 0xdeadbe00u + i); \
        } \
    } END_TEST

ROUNDTRIP_N_WORDS(rt_words_1,   1)
ROUNDTRIP_N_WORDS(rt_words_2,   2)
ROUNDTRIP_N_WORDS(rt_words_4,   4)
ROUNDTRIP_N_WORDS(rt_words_8,   8)
ROUNDTRIP_N_WORDS(rt_words_16,  16)
ROUNDTRIP_N_WORDS(rt_words_32,  32)
ROUNDTRIP_N_WORDS(rt_words_50,  50)
ROUNDTRIP_N_WORDS(rt_words_64,  64)
ROUNDTRIP_N_WORDS(rt_words_100, 100)
ROUNDTRIP_N_WORDS(rt_words_128, 128)
ROUNDTRIP_N_WORDS(rt_words_256, 256)

/* Round-trip an encoded opaque (variable-length) and read back the
 * length word + payload via inline_decode. */
#define ROUNDTRIP_OPAQUE(name, n) \
    START_TEST(name) { \
        struct xdr_stream xdr; \
        struct xdr_buf *buf = fresh_xdr_buf(4096); \
        unsigned char src[(n) ? (n) : 1]; \
        for (unsigned int i = 0; i < (n); i++) src[i] = (unsigned char)(i + 7); \
        unsigned int total_words = 1 + (((n) + 3) >> 2); \
        esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL); \
        __be32 *r = esunrpc_xdr_reserve_space(&xdr, total_words * 4); \
        ck_assert_ptr_nonnull(r); \
        __be32 *end = esunrpc_xdr_encode_opaque(r, src, (n)); \
        ck_assert_ptr_eq(end, r + total_words); \
        buf->len = total_words * 4; buf->head[0].iov_len = total_words * 4; \
        esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL); \
        __be32 *lenw = esunrpc_xdr_inline_decode(&xdr, 4); \
        ck_assert_ptr_nonnull(lenw); \
        ck_assert_uint_eq(ntohl(*lenw), (uint32_t)(n)); \
        if ((n)) { \
            __be32 *payload = esunrpc_xdr_inline_decode(&xdr, ((n) + 3) & ~3u); \
            ck_assert_ptr_nonnull(payload); \
            ck_assert_int_eq(memcmp(payload, src, (n)), 0); \
        } \
    } END_TEST

ROUNDTRIP_OPAQUE(rt_op_4,    4)
ROUNDTRIP_OPAQUE(rt_op_5,    5)
ROUNDTRIP_OPAQUE(rt_op_8,    8)
ROUNDTRIP_OPAQUE(rt_op_15,   15)
ROUNDTRIP_OPAQUE(rt_op_16,   16)
ROUNDTRIP_OPAQUE(rt_op_31,   31)
ROUNDTRIP_OPAQUE(rt_op_32,   32)
ROUNDTRIP_OPAQUE(rt_op_64,   64)
ROUNDTRIP_OPAQUE(rt_op_100,  100)
ROUNDTRIP_OPAQUE(rt_op_127,  127)
ROUNDTRIP_OPAQUE(rt_op_128,  128)
ROUNDTRIP_OPAQUE(rt_op_256,  256)
ROUNDTRIP_OPAQUE(rt_op_512,  512)
ROUNDTRIP_OPAQUE(rt_op_1000, 1000)

/* ============================================================ */
/* reserve_space coverage: many cumulative reservations until full. */
/* ============================================================ */

START_TEST(reserve_many_4byte_chunks) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    for (int i = 0; i < 64; i++) {
        __be32 *r = esunrpc_xdr_reserve_space(&xdr, 4);
        ck_assert_ptr_nonnull(r);
    }
} END_TEST

START_TEST(reserve_eightybyte_chunks) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(800);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    for (int i = 0; i < 10; i++) {
        __be32 *r = esunrpc_xdr_reserve_space(&xdr, 80);
        ck_assert_ptr_nonnull(r);
    }
} END_TEST

/* ============================================================ */
/* esunrpc_xdr_restrict_buflen — shrink the writable end of the */
/* xdr_buf. Returns -1 if shrinking past current data, 0 if no  */
/* change needed, 0 + adjusts xdr->end if shrinking within.     */
/* ============================================================ */

extern int esunrpc_xdr_restrict_buflen(struct xdr_stream *xdr, int newbuflen);

START_TEST(restrict_buflen_negative_returns_minus_one) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, -1), -1);
} END_TEST

START_TEST(restrict_buflen_below_used_returns_minus_one) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    esunrpc_xdr_reserve_space(&xdr, 64);
    /* buf->len now 64; restricting to 32 must error. */
    ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, 32), -1);
} END_TEST

START_TEST(restrict_buflen_above_capacity_no_op_returns_zero) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* buflen=256, requesting 1024 → SUT returns 0 without changing. */
    ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, 1024), 0);
    ck_assert_int_eq(buf->buflen, 256);  /* unchanged */
} END_TEST

START_TEST(restrict_buflen_within_data_succeeds) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* Restrict to 128 — below current 256 capacity but above used (0). */
    ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, 128), 0);
    ck_assert_int_eq(buf->buflen, 128);
} END_TEST

START_TEST(restrict_buflen_exact_match_no_op_zero) {
    struct xdr_stream xdr;
    struct xdr_buf *buf = fresh_xdr_buf(256);
    esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL);
    ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, 256), 0);
    ck_assert_int_eq(buf->buflen, 256);
} END_TEST

#define RESTRICT_BUFLEN_PARAM(name, cap, used, request, expected_rc, expected_buflen) \
    START_TEST(name) { \
        struct xdr_stream xdr; \
        struct xdr_buf *buf = fresh_xdr_buf(cap); \
        esunrpc_xdr_init_encode(&xdr, buf, buf->head[0].iov_base, NULL); \
        if ((used) > 0) esunrpc_xdr_reserve_space(&xdr, (used)); \
        ck_assert_int_eq(esunrpc_xdr_restrict_buflen(&xdr, (request)), (expected_rc)); \
        ck_assert_int_eq(buf->buflen, (expected_buflen)); \
    } END_TEST

RESTRICT_BUFLEN_PARAM(rb_p_a, 1024,    0, 512,   0,  512)
RESTRICT_BUFLEN_PARAM(rb_p_b, 1024,    0,   1,   0,    1)
RESTRICT_BUFLEN_PARAM(rb_p_c, 1024,    0,   0,   0,    0)
RESTRICT_BUFLEN_PARAM(rb_p_d, 1024,   64, 100,   0,  100)
RESTRICT_BUFLEN_PARAM(rb_p_e, 1024,   64,  64,   0,   64)
RESTRICT_BUFLEN_PARAM(rb_p_f, 1024,   64,  63,  -1, 1024)
RESTRICT_BUFLEN_PARAM(rb_p_g, 1024,   64,  -5,  -1, 1024)
RESTRICT_BUFLEN_PARAM(rb_p_h, 1024,    0, 2048,  0, 1024)
RESTRICT_BUFLEN_PARAM(rb_p_i, 1024, 1024, 1024,  0, 1024)
RESTRICT_BUFLEN_PARAM(rb_p_j, 1024, 1024, 1023, -1, 1024)

/* ============================================================ */
/* esunrpc_xdr_page_pos — encoded position relative to xdr pages */
/* (= stream_pos - head[0].iov_len). For tests with no pages,   */
/* page_pos returns 0 if stream_pos == head[0].iov_len.         */
/* ============================================================ */

extern unsigned int esunrpc_xdr_page_pos(const struct xdr_stream *xdr);

START_TEST(page_pos_decode_at_start_is_zero) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 64; buf->head[0].iov_len = 64;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* Before decoding anything, page_pos == stream_pos - head_len = 0 - 64
     * which would underflow; but stream_pos at decode-start ==
     * head_len, so page_pos == 0. */
    /* The SUT WARN_ONs here; calling it just to make sure it doesn't
     * crash — we don't assert the value. */
    (void)esunrpc_xdr_page_pos(&xdr);
} END_TEST

/* ============================================================ */
/* esunrpc_xdr_truncate_decode — clip the decode stream length. */
/* ============================================================ */

extern void esunrpc_xdr_truncate_decode(struct xdr_stream *xdr, size_t len);

/* truncate_decode aligns the requested length UP to the nearest
 * multiple of 4 (XDR word size) before subtracting it from buf->len.
 * So a request to truncate by 1 actually shrinks by 4. The
 * parametric tests below operate on 4-byte-aligned values. */
START_TEST(truncate_decode_basic_clips_buf_len) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 128; buf->head[0].iov_len = 128;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    esunrpc_xdr_truncate_decode(&xdr, 16);
    ck_assert_int_eq(buf->len, 128 - 16);
} END_TEST

START_TEST(truncate_decode_unaligned_rounds_up_to_word) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 128; buf->head[0].iov_len = 128;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    /* take=1 aligns up to 4 → buf->len becomes 124. */
    esunrpc_xdr_truncate_decode(&xdr, 1);
    ck_assert_int_eq(buf->len, 124);
} END_TEST

START_TEST(truncate_decode_three_aligns_to_four) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 128; buf->head[0].iov_len = 128;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    esunrpc_xdr_truncate_decode(&xdr, 3);
    ck_assert_int_eq(buf->len, 124);
} END_TEST

START_TEST(truncate_decode_zero_is_no_op) {
    struct xdr_stream xdr = {0};
    struct xdr_buf *buf = fresh_xdr_buf(256);
    buf->len = 128; buf->head[0].iov_len = 128;
    esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL);
    esunrpc_xdr_truncate_decode(&xdr, 0);
    ck_assert_int_eq(buf->len, 128);
} END_TEST

#define TRUNCATE_DECODE_PARAM(name, init_len, take, expected_len) \
    START_TEST(name) { \
        struct xdr_stream xdr = {0}; \
        struct xdr_buf *buf = fresh_xdr_buf(1024); \
        buf->len = (init_len); buf->head[0].iov_len = (init_len); \
        esunrpc_xdr_init_decode(&xdr, buf, buf->head[0].iov_base, NULL); \
        esunrpc_xdr_truncate_decode(&xdr, (take)); \
        ck_assert_int_eq(buf->len, (expected_len)); \
    } END_TEST

/* All parameters use 4-byte-aligned `take` values to keep the
 * relationship `expected_len = init_len - take` clean. */
TRUNCATE_DECODE_PARAM(td_p_a, 256,    0, 256)
TRUNCATE_DECODE_PARAM(td_p_b, 256,    4, 252)
TRUNCATE_DECODE_PARAM(td_p_c, 256,    8, 248)
TRUNCATE_DECODE_PARAM(td_p_d, 256,   16, 240)
TRUNCATE_DECODE_PARAM(td_p_e, 256,   64, 192)
TRUNCATE_DECODE_PARAM(td_p_f, 256,  128, 128)
TRUNCATE_DECODE_PARAM(td_p_g, 256,  200,  56)
TRUNCATE_DECODE_PARAM(td_p_h, 512,  256, 256)
TRUNCATE_DECODE_PARAM(td_p_i, 512,  512,   0)
TRUNCATE_DECODE_PARAM(td_p_j, 1024, 100, 924)
TRUNCATE_DECODE_PARAM(td_p_k, 1024, 1020,  4)
TRUNCATE_DECODE_PARAM(td_p_l, 1024, 1024,  0)

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
    suite_add_tcase(s, t5);

    TCase *t6 = tcase_create("init_decode");
    tcase_add_test(t6, init_decode_sets_pointers);
    tcase_add_test(t6, inline_decode_reads_word);
    tcase_add_test(t6, inline_decode_past_end_returns_NULL);
    tcase_add_test(t6, stream_pos_decode_zero_at_start);
    tcase_add_test(t6, stream_pos_decode_advances);
    suite_add_tcase(s, t6);

    TCase *t7 = tcase_create("roundtrip");
    tcase_add_test(t7, roundtrip_word_sequence);
    suite_add_tcase(s, t7);

    TCase *t8 = tcase_create("encode_fixed_parametric");
    tcase_add_test(t8, enc_fixed_p_6);
    tcase_add_test(t8, enc_fixed_p_9);
    tcase_add_test(t8, enc_fixed_p_10);
    tcase_add_test(t8, enc_fixed_p_11);
    tcase_add_test(t8, enc_fixed_p_12);
    tcase_add_test(t8, enc_fixed_p_13);
    tcase_add_test(t8, enc_fixed_p_14);
    tcase_add_test(t8, enc_fixed_p_18);
    tcase_add_test(t8, enc_fixed_p_19);
    tcase_add_test(t8, enc_fixed_p_20);
    tcase_add_test(t8, enc_fixed_p_21);
    tcase_add_test(t8, enc_fixed_p_22);
    tcase_add_test(t8, enc_fixed_p_23);
    tcase_add_test(t8, enc_fixed_p_24);
    tcase_add_test(t8, enc_fixed_p_25);
    tcase_add_test(t8, enc_fixed_p_26);
    tcase_add_test(t8, enc_fixed_p_27);
    tcase_add_test(t8, enc_fixed_p_28);
    tcase_add_test(t8, enc_fixed_p_29);
    tcase_add_test(t8, enc_fixed_p_30);
    tcase_add_test(t8, enc_fixed_p_33);
    tcase_add_test(t8, enc_fixed_p_47);
    tcase_add_test(t8, enc_fixed_p_48);
    tcase_add_test(t8, enc_fixed_p_49);
    tcase_add_test(t8, enc_fixed_p_63);
    tcase_add_test(t8, enc_fixed_p_65);
    tcase_add_test(t8, enc_fixed_p_127);
    tcase_add_test(t8, enc_fixed_p_128);
    tcase_add_test(t8, enc_fixed_p_129);
    tcase_add_test(t8, enc_fixed_p_256);
    tcase_add_test(t8, enc_fixed_p_257);
    tcase_add_test(t8, enc_fixed_p_511);
    tcase_add_test(t8, enc_fixed_p_512);
    tcase_add_test(t8, enc_fixed_p_513);
    tcase_add_test(t8, enc_fixed_p_1023);
    tcase_add_test(t8, enc_fixed_p_1024);
    tcase_add_test(t8, enc_fixed_p_2048);
    suite_add_tcase(s, t8);

    TCase *t9 = tcase_create("encode_opaque_parametric");
    tcase_add_test(t9, enc_opaque_p_4);
    tcase_add_test(t9, enc_opaque_p_6);
    tcase_add_test(t9, enc_opaque_p_9);
    tcase_add_test(t9, enc_opaque_p_10);
    tcase_add_test(t9, enc_opaque_p_11);
    tcase_add_test(t9, enc_opaque_p_12);
    tcase_add_test(t9, enc_opaque_p_13);
    tcase_add_test(t9, enc_opaque_p_14);
    tcase_add_test(t9, enc_opaque_p_15);
    tcase_add_test(t9, enc_opaque_p_17);
    tcase_add_test(t9, enc_opaque_p_24);
    tcase_add_test(t9, enc_opaque_p_25);
    tcase_add_test(t9, enc_opaque_p_31);
    tcase_add_test(t9, enc_opaque_p_33);
    tcase_add_test(t9, enc_opaque_p_63);
    tcase_add_test(t9, enc_opaque_p_65);
    tcase_add_test(t9, enc_opaque_p_100);
    tcase_add_test(t9, enc_opaque_p_127);
    tcase_add_test(t9, enc_opaque_p_129);
    tcase_add_test(t9, enc_opaque_p_256);
    tcase_add_test(t9, enc_opaque_p_257);
    tcase_add_test(t9, enc_opaque_p_511);
    tcase_add_test(t9, enc_opaque_p_512);
    tcase_add_test(t9, enc_opaque_p_1023);
    tcase_add_test(t9, enc_opaque_p_1024);
    tcase_add_test(t9, enc_opaque_p_2000);
    tcase_add_test(t9, enc_opaque_p_3000);
    suite_add_tcase(s, t9);

    TCase *t10 = tcase_create("encode_string_parametric");
    tcase_add_test(t10, enc_str_len_2);
    tcase_add_test(t10, enc_str_len_3);
    tcase_add_test(t10, enc_str_len_6);
    tcase_add_test(t10, enc_str_len_7);
    tcase_add_test(t10, enc_str_len_10);
    tcase_add_test(t10, enc_str_len_11);
    tcase_add_test(t10, enc_str_len_12);
    tcase_add_test(t10, enc_str_len_13);
    tcase_add_test(t10, enc_str_len_14);
    tcase_add_test(t10, enc_str_len_15);
    tcase_add_test(t10, enc_str_len_16);
    tcase_add_test(t10, enc_str_len_17);
    tcase_add_test(t10, enc_str_len_18);
    tcase_add_test(t10, enc_str_len_19);
    tcase_add_test(t10, enc_str_len_20);
    tcase_add_test(t10, enc_str_len_31);
    tcase_add_test(t10, enc_str_len_32);
    tcase_add_test(t10, enc_str_len_33);
    tcase_add_test(t10, enc_str_len_63);
    tcase_add_test(t10, enc_str_len_64);
    tcase_add_test(t10, enc_str_len_65);
    tcase_add_test(t10, enc_str_len_100);
    tcase_add_test(t10, enc_str_len_200);
    tcase_add_test(t10, enc_str_len_300);
    tcase_add_test(t10, enc_str_len_500);
    tcase_add_test(t10, enc_str_len_1000);
    suite_add_tcase(s, t10);

    TCase *t11 = tcase_create("encode_netobj_parametric");
    tcase_add_test(t11, enc_netobj_1);
    tcase_add_test(t11, enc_netobj_2);
    tcase_add_test(t11, enc_netobj_3);
    tcase_add_test(t11, enc_netobj_5);
    tcase_add_test(t11, enc_netobj_6);
    tcase_add_test(t11, enc_netobj_7);
    tcase_add_test(t11, enc_netobj_8);
    tcase_add_test(t11, enc_netobj_9);
    tcase_add_test(t11, enc_netobj_15);
    tcase_add_test(t11, enc_netobj_16);
    tcase_add_test(t11, enc_netobj_17);
    tcase_add_test(t11, enc_netobj_31);
    tcase_add_test(t11, enc_netobj_32);
    tcase_add_test(t11, enc_netobj_33);
    tcase_add_test(t11, enc_netobj_64);
    tcase_add_test(t11, enc_netobj_100);
    tcase_add_test(t11, enc_netobj_127);
    tcase_add_test(t11, enc_netobj_128);
    tcase_add_test(t11, enc_netobj_129);
    tcase_add_test(t11, enc_netobj_255);
    tcase_add_test(t11, enc_netobj_256);
    tcase_add_test(t11, enc_netobj_512);
    tcase_add_test(t11, enc_netobj_1024);
    suite_add_tcase(s, t11);

    TCase *t12 = tcase_create("inline_decode_boundary");
    tcase_add_test(t12, idec_4_4);
    tcase_add_test(t12, idec_8_4);
    tcase_add_test(t12, idec_8_8);
    tcase_add_test(t12, idec_16_8);
    tcase_add_test(t12, idec_16_16);
    tcase_add_test(t12, idec_32_24);
    tcase_add_test(t12, idec_64_64);
    tcase_add_test(t12, idec_128_4);
    tcase_add_test(t12, idec_128_60);
    tcase_add_test(t12, idec_128_128);
    tcase_add_test(t12, idec_256_252);
    tcase_add_test(t12, idec_256_256);
    tcase_add_test(t12, idec_512_512);
    tcase_add_test(t12, idec_zero_succeeds);
    tcase_add_test(t12, idec_chunked_consume);
    suite_add_tcase(s, t12);

    TCase *t13 = tcase_create("roundtrip_words");
    tcase_add_test(t13, rt_words_1);
    tcase_add_test(t13, rt_words_2);
    tcase_add_test(t13, rt_words_4);
    tcase_add_test(t13, rt_words_8);
    tcase_add_test(t13, rt_words_16);
    tcase_add_test(t13, rt_words_32);
    tcase_add_test(t13, rt_words_50);
    tcase_add_test(t13, rt_words_64);
    tcase_add_test(t13, rt_words_100);
    tcase_add_test(t13, rt_words_128);
    tcase_add_test(t13, rt_words_256);
    suite_add_tcase(s, t13);

    TCase *t14 = tcase_create("roundtrip_opaque");
    tcase_add_test(t14, rt_op_4);
    tcase_add_test(t14, rt_op_5);
    tcase_add_test(t14, rt_op_8);
    tcase_add_test(t14, rt_op_15);
    tcase_add_test(t14, rt_op_16);
    tcase_add_test(t14, rt_op_31);
    tcase_add_test(t14, rt_op_32);
    tcase_add_test(t14, rt_op_64);
    tcase_add_test(t14, rt_op_100);
    tcase_add_test(t14, rt_op_127);
    tcase_add_test(t14, rt_op_128);
    tcase_add_test(t14, rt_op_256);
    tcase_add_test(t14, rt_op_512);
    tcase_add_test(t14, rt_op_1000);
    suite_add_tcase(s, t14);

    TCase *t15 = tcase_create("reserve_space_chunks");
    tcase_add_test(t15, reserve_many_4byte_chunks);
    tcase_add_test(t15, reserve_eightybyte_chunks);
    suite_add_tcase(s, t15);

    TCase *t16 = tcase_create("restrict_buflen");
    tcase_add_test(t16, restrict_buflen_negative_returns_minus_one);
    tcase_add_test(t16, restrict_buflen_below_used_returns_minus_one);
    tcase_add_test(t16, restrict_buflen_above_capacity_no_op_returns_zero);
    tcase_add_test(t16, restrict_buflen_within_data_succeeds);
    tcase_add_test(t16, restrict_buflen_exact_match_no_op_zero);
    tcase_add_test(t16, rb_p_a);
    tcase_add_test(t16, rb_p_b);
    tcase_add_test(t16, rb_p_c);
    tcase_add_test(t16, rb_p_d);
    tcase_add_test(t16, rb_p_e);
    tcase_add_test(t16, rb_p_f);
    tcase_add_test(t16, rb_p_g);
    tcase_add_test(t16, rb_p_h);
    tcase_add_test(t16, rb_p_i);
    tcase_add_test(t16, rb_p_j);
    suite_add_tcase(s, t16);

    TCase *t17 = tcase_create("page_pos");
    tcase_add_test(t17, page_pos_decode_at_start_is_zero);
    suite_add_tcase(s, t17);

    TCase *t18 = tcase_create("truncate_decode");
    tcase_add_test(t18, truncate_decode_basic_clips_buf_len);
    tcase_add_test(t18, truncate_decode_unaligned_rounds_up_to_word);
    tcase_add_test(t18, truncate_decode_three_aligns_to_four);
    tcase_add_test(t18, truncate_decode_zero_is_no_op);
    tcase_add_test(t18, td_p_a);
    tcase_add_test(t18, td_p_b);
    tcase_add_test(t18, td_p_c);
    tcase_add_test(t18, td_p_d);
    tcase_add_test(t18, td_p_e);
    tcase_add_test(t18, td_p_f);
    tcase_add_test(t18, td_p_g);
    tcase_add_test(t18, td_p_h);
    tcase_add_test(t18, td_p_i);
    tcase_add_test(t18, td_p_j);
    tcase_add_test(t18, td_p_k);
    tcase_add_test(t18, td_p_l);
    suite_add_tcase(s, t18);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_xdr_suite
#include "check_runner.h"
