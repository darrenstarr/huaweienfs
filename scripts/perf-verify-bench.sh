#!/bin/bash
# Cross-host data integrity verification for enfs multipath mounts.
#
# Run from a controller host that has SSH access to both the writer
# and the reader (the controller does not need to be either of them).
# All long-running fio + sha256 work happens on the writer/reader; the
# controller only orchestrates and aggregates results.
#
# For each block size in the matrix:
#   1. Write a deterministic file on the WRITER host (with fio's
#      verify_pattern + crc32c so per-RPC corruption shows up).
#   2. sha256sum the written file → manifest.
#   3. Drop client page caches on both hosts (forces wire reads).
#   4. Re-read the file on the WRITER host with fio --verify_only
#      (catches intra-host corruption: write returned bytes that the
#      same client can read back as the same bytes).
#   5. sha256sum the file on the READER host (catches cross-host
#      corruption: the data on the server matches what the writer
#      thinks it wrote, from a different client perspective).
#
# Args (positional):
#   WRITE_HOST   ssh target for the writer (e.g. user@<LAB_HOST_A>)
#   WRITE_DIR    absolute path inside the writer mount (must exist)
#   READ_HOST    ssh target for the reader (e.g. user@<LAB_HOST_B>)
#   READ_DIR     absolute path inside the reader mount of the same
#                export (the file written by writer must be visible
#                here)
#   LABEL        tag for output naming (e.g. v3rr, v41rr)
#   OUTDIR       directory ON THE CONTROLLER for fio JSON + manifests
#
# Exit codes:
#   0  all block sizes PASSED both intra-host and cross-host verify
#   1  CORRUPTION detected — see OUTDIR for manifests, fio JSONs,
#      and per-bs FAIL marker file
#   2  setup / tooling failure (fio not present, mount missing, etc.)
set -uo pipefail

WRITE_HOST="${1:?usage: $0 WRITE_HOST WRITE_DIR READ_HOST READ_DIR LABEL OUTDIR}"
WRITE_DIR="${2:?write dir, path inside the writer mount}"
READ_HOST="${3:?read host}"
READ_DIR="${4:?read dir, path inside the reader mount of the same export}"
LABEL="${5:?label, e.g. v3rr or v41rr}"
OUTDIR="${6:?output dir for json + manifest}"

mkdir -p "$OUTDIR"
MANIFEST="${OUTDIR}/sha256-${LABEL}.txt"
: > "$MANIFEST"

SIZES="4k 8k 16k 32k 64k 128k 256k 512k 1m"
FILE_SIZE="256M"
FAIL_COUNT=0
PASS_COUNT=0
declare -a FAILED_SIZES

drop_caches_writer() {
    ssh "$WRITE_HOST" 'sudo -n bash -c "echo 3 > /proc/sys/vm/drop_caches"' 2>/dev/null
}
drop_caches_reader() {
    ssh "$READ_HOST" 'sudo -n bash -c "echo 3 > /proc/sys/vm/drop_caches"' 2>/dev/null
}

for bs in $SIZES; do
    echo
    echo "====================  bs=${bs}  ===================="
    name="verify_${LABEL}_${bs}"
    file_w="${WRITE_DIR}/${name}.bin"
    file_r="${READ_DIR}/${name}.bin"

    # --- 1. write deterministic content on the writer ---------------
    echo "[1/5] write on writer ($WRITE_HOST)"
    ssh "$WRITE_HOST" "fio --name=$name --rw=write --bs=$bs \
        --filename=$file_w \
        --direct=1 --ioengine=psync --numjobs=1 --size=$FILE_SIZE \
        --verify=crc32c --verify_pattern=0xdeadbeef --do_verify=0 \
        --output-format=json" \
        > "${OUTDIR}/${name}_w.json" 2>/dev/null
    WRITE_RC=$?
    if [[ $WRITE_RC -ne 0 ]]; then
        echo "  ERROR: write failed (exit=$WRITE_RC)" >&2
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAILED_SIZES+=("$bs:write_failed")
        continue
    fi

    # --- 2. sha256 on writer ----------------------------------------
    echo "[2/5] sha256 on writer"
    SHA_W=$(ssh "$WRITE_HOST" "sha256sum $file_w" | awk '{print $1}')
    echo "writer:${bs} ${SHA_W}" >> "$MANIFEST"

    # --- 3. drop caches on both hosts -------------------------------
    echo "[3/5] drop caches on both hosts"
    drop_caches_writer
    drop_caches_reader

    # --- 4. intra-host verify with fio ------------------------------
    echo "[4/5] intra-host fio verify_only on writer"
    ssh "$WRITE_HOST" "fio --name=$name --rw=read --bs=$bs \
        --filename=$file_w \
        --direct=1 --ioengine=psync --numjobs=1 --size=$FILE_SIZE \
        --verify=crc32c --verify_pattern=0xdeadbeef \
        --do_verify=1 --verify_only \
        --output-format=json" \
        > "${OUTDIR}/${name}_v.json" 2>/dev/null
    VERIFY_RC=$?
    if [[ $VERIFY_RC -ne 0 ]]; then
        echo "  CORRUPTION (intra-host): fio verify failed for bs=$bs" >&2
        echo "FAIL ${bs}: intra-host fio verify_only exit=$VERIFY_RC" \
            >> "${OUTDIR}/FAIL_${LABEL}.txt"
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAILED_SIZES+=("$bs:intra_host")
        continue
    fi

    # --- 5. cross-host sha256 on the reader -------------------------
    echo "[5/5] cross-host sha256 on reader ($READ_HOST)"
    SHA_R=$(ssh "$READ_HOST" "sha256sum $file_r 2>/dev/null" | awk '{print $1}')
    echo "reader:${bs} ${SHA_R}" >> "$MANIFEST"

    if [[ -z "$SHA_R" ]]; then
        echo "  ERROR: reader could not sha256sum $file_r" >&2
        echo "FAIL ${bs}: reader sha256 failed (file missing?)" \
            >> "${OUTDIR}/FAIL_${LABEL}.txt"
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAILED_SIZES+=("$bs:reader_missing")
        continue
    fi

    if [[ "$SHA_W" != "$SHA_R" ]]; then
        echo "  CORRUPTION (cross-host): sha256 mismatch for bs=$bs" >&2
        echo "    writer: $SHA_W" >&2
        echo "    reader: $SHA_R" >&2
        echo "FAIL ${bs}: cross-host sha256 mismatch ($SHA_W vs $SHA_R)" \
            >> "${OUTDIR}/FAIL_${LABEL}.txt"
        FAIL_COUNT=$((FAIL_COUNT+1))
        FAILED_SIZES+=("$bs:cross_host")
        continue
    fi

    echo "  PASS  bs=$bs  sha=${SHA_W:0:16}..."
    PASS_COUNT=$((PASS_COUNT+1))
done

echo
echo "==============================================================="
echo "RESULT: $PASS_COUNT passed, $FAIL_COUNT failed"
if [[ $FAIL_COUNT -gt 0 ]]; then
    echo "Failed block sizes: ${FAILED_SIZES[*]}"
    echo "Manifest: $MANIFEST"
    echo "FAIL marker: ${OUTDIR}/FAIL_${LABEL}.txt"
    exit 1
fi
echo "All block sizes PASSED both intra-host fio verify and cross-host sha256."
echo "Manifest: $MANIFEST"
