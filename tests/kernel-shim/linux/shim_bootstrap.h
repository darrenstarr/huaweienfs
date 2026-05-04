/* SPDX-License-Identifier: GPL-2.0 */
/*
 * shim_bootstrap.h — force-included before every test compile.
 *
 * Sets up the small set of macros and includes that have to be in
 * place before <linux/*.h> resolves. Kept minimal on purpose; most
 * surface lives in the per-header shims themselves.
 */
#ifndef ENFS_TESTS_SHIM_BOOTSTRAP_H
#define ENFS_TESTS_SHIM_BOOTSTRAP_H

/* Ensure kernel-context paths get taken in headers/source. The
 * Makefile also passes -D__KERNEL__ on the command line; this is
 * belt-and-braces. */
#ifndef __KERNEL__
#define __KERNEL__ 1
#endif

/* Standard userspace includes pulled in once, here, so individual
 * shim headers don't have to repeat them. */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>

/* glibc names the union inside in6_addr `__in6_u` with member
 * `__u6_addr32`; the kernel UAPI uses `in6_u` / `u6_addr32`. Source
 * files written against the kernel API can compile here unchanged
 * via these aliases. The defines are emitted before any source-under-
 * test parses, so .in6_u.u6_addr32 references rewrite correctly. */
#define in6_u       __in6_u
#define u6_addr8    __u6_addr8
#define u6_addr16   __u6_addr16
#define u6_addr32   __u6_addr32

/* Loud failure for shim functions that exist only to satisfy linkage
 * but have no meaningful userspace behavior. Test code should never
 * exercise a path that hits one of these; if it does, the test fails
 * with a clear message. */
#define __shim_unimplemented(name) do { \
    fprintf(stderr, "[shim] FATAL: %s called in userspace tests " \
                    "(no userspace implementation)\n", (name)); \
    abort(); \
} while (0)

#endif /* ENFS_TESTS_SHIM_BOOTSTRAP_H */
