/* SPDX-License-Identifier: GPL-2.0 */
/*
 * check_runner.h — common SRunner driver for all userspace test
 * binaries.
 *
 * Each test_*.c file defines:
 *
 *     static Suite *make_suite(void)  { ... return s; }
 *
 * and then includes this header at the bottom:
 *
 *     #define CHECK_RUNNER_SUITE  make_suite
 *     #include "check_runner.h"
 *
 * Behaviour:
 *
 *   - Always runs the suite via SRunner with CK_VERBOSE.
 *   - If the environment variable CK_XML_LOG_FILE is set, writes a
 *     JUnit-style XML test report to that path (libcheck native
 *     `srunner_set_xml` format — accepted by GitHub Actions
 *     test-reporters and by Jenkins out of the box).
 *   - If CK_TAP_LOG_FILE is set, writes a TAP report there too.
 *   - Returns 0 on full pass, 1 on any failure.
 *
 * Why a single header:
 *   - All test binaries get identical reporting behaviour.
 *   - Adding a new test file is a one-liner: drop in
 *     `#define CHECK_RUNNER_SUITE my_suite` and `#include "check_runner.h"`,
 *     no main() boilerplate.
 *   - CI workflows can `for f in tests/build/*.xml; do upload $f; done`
 *     without per-binary special-casing.
 */
#ifndef _ENFS_CHECK_RUNNER_H
#define _ENFS_CHECK_RUNNER_H

#include <check.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CHECK_RUNNER_SUITE
#error "Define CHECK_RUNNER_SUITE to your suite_*() function before including check_runner.h"
#endif

int main(void)
{
    Suite *s = CHECK_RUNNER_SUITE();
    SRunner *sr = srunner_create(s);

    /* JUnit-style XML for CI ingestion. */
    const char *xml = getenv("CK_XML_LOG_FILE");
    if (xml && *xml) {
        srunner_set_xml(sr, xml);
    }

    /* TAP for human-readable + tap-consumer integration. */
    const char *tap = getenv("CK_TAP_LOG_FILE");
    if (tap && *tap) {
        srunner_set_tap(sr, tap);
    }

    srunner_run_all(sr, CK_VERBOSE);

    int failed = srunner_ntests_failed(sr);
    int total  = srunner_ntests_run(sr);
    srunner_free(sr);

    /* Loud trailing summary — humans skim for this; CI greps it. */
    fprintf(stderr, "[runner] suite=%s total=%d failed=%d\n",
            xml ? xml : "(no-xml)", total, failed);

    return failed == 0 ? 0 : 1;
}

#endif /* _ENFS_CHECK_RUNNER_H */
