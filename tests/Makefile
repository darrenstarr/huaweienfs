# tests/Makefile — userspace unit-test build for enfs.
#
# Compiles enfs source files unmodified against the kernel-API shim
# layer, links them with libcheck-based test runners, and executes the
# suite. No kernel build, no VM, no root.
#
# Top-level entry: `make test` from the repo root delegates here.
#
# Per-test convention:
#   unit/test_<module>.c     test runner (Check suite)
#   stubs/<module>_stubs.c   optional fakes for that module's deps
#   ENFS_SRC_<module>        list of sources under test for that module
#
# To add a new test for module X:
#   1. Add ENFS_SRC_X = vendor/.../X.c
#   2. Add TESTS += test_X
#   3. Write unit/test_X.c
#   4. Add stubs in stubs/ for X's dependencies (if any)
#   5. `make`

ROOT          := $(abspath $(CURDIR)/..)
BUILD_DIR     := $(CURDIR)/build
HEADERS_DIR   := $(BUILD_DIR)/linux-headers
SHIM_DIR      := $(CURDIR)/kernel-shim
STUBS_DIR     := $(CURDIR)/stubs
UNIT_DIR      := $(CURDIR)/unit
ENFS_SRC_ROOT := $(ROOT)/vendor/openeuler/fs/nfs/enfs

# Compiler flags.
#   -Dstatic=         expose `static` functions for direct testing
#   -D__KERNEL__      let enfs source files take their kernel-context paths
#   -include          force-include a shim bootstrap before anything else,
#                     so kernel-only macros are defined before the source
#                     pulls in <linux/foo.h>
#   -fno-builtin      avoid clashes with shim definitions of mem* etc.
#   -Wno-...          suppress warnings that fire only because we're
#                     compiling kernel code in userspace (unused params
#                     in fakes, etc.). Real warnings still surface.
# CFLAGS that apply to ALL test-side .c files (test runner, stubs).
# Does NOT include -Dstatic= — that would strip `static` from the
# `static inline` functions in shim headers, leaving them as bare
# `inline` (= "external definition needed elsewhere") and producing
# undefined references at link time.
CFLAGS_COMMON := \
    -std=gnu11 \
    -O0 -g3 \
    -fno-strict-aliasing -fno-builtin \
    -D__KERNEL__ \
    -DENFS_USERSPACE_TESTS \
    -include $(SHIM_DIR)/linux/shim_bootstrap.h \
    -include $(SHIM_DIR)/enfs_preempt.h \
    -I $(SHIM_DIR) \
    -I $(HEADERS_DIR) \
    -I $(ENFS_SRC_ROOT) \
    -Wall -Wno-unused-parameter -Wno-unused-function \
    -Wno-pointer-arith -Wno-pragmas -Wno-comment

# For the source-under-test we want to expose `static` functions to
# the test runner. We can't use -Dstatic= because that would also
# strip `static` from the shim headers' `static inline` helpers
# included by the SUT, leaving them as bare `inline` (= "external
# definition needed elsewhere") and producing undefined references.
# Instead, sed the source file to blank out the `static` keyword
# (preserving column count by replacing with spaces, so line numbers
# in error output stay accurate).
SED_STRIP_STATIC := sed -E 's/\bstatic\b/      /g'

# Coverage instrumentation. Applied ONLY to the SUT compile/link
# (CFLAGS_SUT_EXTRA / LDLIBS_SUT_EXTRA) when COVERAGE=1 is set on
# the make command line, so test-runner / shim / stub objects don't
# get noisy .gcno files. Triggered by `make coverage`.
ifeq ($(COVERAGE),1)
    CFLAGS_SUT_EXTRA := --coverage -fprofile-arcs -ftest-coverage
    LDLIBS_SUT_EXTRA := --coverage
else
    CFLAGS_SUT_EXTRA :=
    LDLIBS_SUT_EXTRA :=
endif

# Check + libsubunit detected via pkg-config (keeps include paths
# correct on distros that put them in non-standard places).
CHECK_CFLAGS  := $(shell pkg-config --cflags check 2>/dev/null)
CHECK_LIBS    := $(shell pkg-config --libs check 2>/dev/null)
ifeq ($(CHECK_LIBS),)
    $(error libcheck not found via pkg-config; install with: apt install check libsubunit-dev pkg-config)
endif

CFLAGS := $(CFLAGS_COMMON) $(CHECK_CFLAGS)
LDLIBS := $(CHECK_LIBS) -lpthread

# ---------------------------------------------------------------------
# Per-module test definitions. Add a new test by appending to TESTS
# and defining ENFS_SRC_<name> and STUBS_<name>.
# ---------------------------------------------------------------------

TESTS := test_enfs_roundrobin

ENFS_SRC_test_enfs_roundrobin := $(ENFS_SRC_ROOT)/enfs_roundrobin.c
STUBS_test_enfs_roundrobin    := $(STUBS_DIR)/kernel_stubs.c $(STUBS_DIR)/enfs_deps_stubs.c

# ---------------------------------------------------------------------
# Build rules. Per-test we build a single binary that links the source
# under test, the test runner, and the relevant stubs.
# ---------------------------------------------------------------------

.PHONY: all check coverage coverage-clean fetch-headers clean help

all: $(addprefix $(BUILD_DIR)/, $(TESTS))

# `check` is the conventional GNU make name for "build + run tests".
check: all
	@set -e; \
	failed=0; \
	for t in $(TESTS); do \
	    echo "=== running $$t ==="; \
	    $(BUILD_DIR)/$$t || failed=$$((failed+1)); \
	done; \
	if [ $$failed -gt 0 ]; then \
	    echo "FAIL: $$failed test binary(ies) reported failures"; \
	    exit 1; \
	fi; \
	echo "OK: all $(words $(TESTS)) test binary(ies) passed"

# Headers fetch. Idempotent — script no-ops on cache hit.
fetch-headers: $(HEADERS_DIR)/.fetched

$(HEADERS_DIR)/.fetched: UPSTREAM-REVISION LINUX-HEADERS-MANIFEST fetch-linux-headers.sh
	@bash ./fetch-linux-headers.sh
	@touch $@

$(BUILD_DIR):
	@mkdir -p $@

# Per-test build rule. Three compile invocations + one link, so:
#   - source-under-test gets -Dstatic= (its `static` functions are
#     exposed to the test runner)
#   - test runner + stubs do NOT get -Dstatic= (so the shim's
#     `static inline` helpers retain internal linkage)
# All three .o files are then linked into one binary per test.
define TEST_RULE
# Pre-process the source-under-test: strip `static` so its functions
# become test-visible. Output goes into build/ with the original
# basename so #line-style error messages point at it; original lives
# in vendor/ untouched.
$$(BUILD_DIR)/$(1)_sut.c: $$(ENFS_SRC_$(1)) | $$(BUILD_DIR)
	@echo "  GEN   $(1) [sut.c]"
	@$$(SED_STRIP_STATIC) $$< > $$@

$$(BUILD_DIR)/$(1)_sut.o: $$(BUILD_DIR)/$(1)_sut.c | fetch-headers
	@echo "  CC    $(1) [sut]"
	@$$(CC) $$(CFLAGS) $$(CFLAGS_SUT_EXTRA) -c -o $$@ $$<

$$(BUILD_DIR)/$(1)_runner.o: $$(UNIT_DIR)/$(1).c | $$(BUILD_DIR) fetch-headers
	@echo "  CC    $(1) [runner]"
	@$$(CC) $$(CFLAGS) -c -o $$@ $$<

# One .o per stub source, named tests/build/<test>_<stub-basename>.o
# so multiple tests sharing a stub get separate objects (and never
# collide on parallel builds).
$(1)_STUBS_OBJS := $$(patsubst $$(STUBS_DIR)/%.c,$$(BUILD_DIR)/$(1)_stub_%.o,$$(STUBS_$(1)))

$$(BUILD_DIR)/$(1)_stub_%.o: $$(STUBS_DIR)/%.c | $$(BUILD_DIR) fetch-headers
	@echo "  CC    $(1) [stub: $$(notdir $$<)]"
	@$$(CC) $$(CFLAGS) -c -o $$@ $$<

$$(BUILD_DIR)/$(1): $$(BUILD_DIR)/$(1)_sut.o $$(BUILD_DIR)/$(1)_runner.o $$($(1)_STUBS_OBJS)
	@echo "  LD    $(1)"
	@$$(CC) -o $$@ $$^ $$(LDLIBS) $$(LDLIBS_SUT_EXTRA)
endef
$(foreach t,$(TESTS),$(eval $(call TEST_RULE,$(t))))

# Coverage report. Forces a clean build with COVERAGE=1 so the SUT
# .o files contain --coverage instrumentation, runs the suite, then
# uses lcov to produce build/coverage/index.html and prints a
# textual summary. The summary includes the per-function coverage
# table so it's actionable from the terminal.
COVERAGE_DIR := $(BUILD_DIR)/coverage
COVERAGE_INFO := $(COVERAGE_DIR)/lcov.info

coverage: coverage-clean
	@$(MAKE) COVERAGE=1 check
	@mkdir -p $(COVERAGE_DIR)
	@echo "=== capturing coverage ==="
	@lcov --quiet \
	    --directory $(BUILD_DIR) \
	    --base-directory $(ROOT) \
	    --capture \
	    --output-file $(COVERAGE_INFO) \
	    --rc geninfo_unexecuted_blocks=1 \
	    --rc geninfo_branch_coverage=1 \
	    --rc lcov_branch_coverage=1 \
	    --branch-coverage \
	    --ignore-errors mismatch,empty,unused,inconsistent,format,negative
	@echo "=== filtering to source-under-test ==="
	@lcov --quiet \
	    --extract $(COVERAGE_INFO) "$(ROOT)/tests/build/*_sut.c" \
	    --output-file $(COVERAGE_INFO) \
	    --branch-coverage \
	    --rc lcov_branch_coverage=1 \
	    --ignore-errors empty,unused
	@echo "=== generating HTML report ==="
	@genhtml --quiet --output-directory $(COVERAGE_DIR) $(COVERAGE_INFO) \
	    --branch-coverage \
	    --rc genhtml_branch_coverage=1 \
	    --ignore-errors inconsistent,corrupt
	@echo
	@echo "=== summary ==="
	@lcov --summary $(COVERAGE_INFO) \
	    --rc lcov_branch_coverage=1 2>&1 \
	    | grep -E '(lines|functions|branches)\.+:' || true
	@echo
	@echo "HTML report: $(COVERAGE_DIR)/index.html"
	@echo "Per-function detail:"
	@cd $(BUILD_DIR) && gcov --branch-counts --branch-probabilities \
	    test_enfs_roundrobin_sut.gcda 2>/dev/null \
	    | grep -A1 '^Function' | head -60 || true

coverage-clean:
	@rm -rf $(BUILD_DIR)
	@find . -name '*.gcda' -o -name '*.gcno' -o -name '*.gcov' 2>/dev/null \
	    | xargs -r rm -f

clean: coverage-clean

help:
	@echo "tests/ — userspace unit tests for enfs"
	@echo
	@echo "  make             build all test binaries"
	@echo "  make check       build + run them"
	@echo "  make coverage    rebuild with --coverage, run, generate lcov report"
	@echo "                   → output: build/coverage/index.html + textual summary"
	@echo "  make fetch-headers  download upstream Linux headers (idempotent)"
	@echo "  make clean       remove build/ artefacts (including .gcda/.gcno)"
	@echo
	@echo "Tests configured: $(TESTS)"
