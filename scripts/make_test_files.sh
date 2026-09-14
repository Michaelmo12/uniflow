#!/usr/bin/env bash
# Creates test files of various sizes filled with random data.
#
# Usage:
#   ./scripts/make_test_files.sh [output_dir] [size_spec ...]
#
# size_spec entries look like "1M", "10M", "900M", "1G" (anything `dd`'s
# bs/count math understands). Defaults to the sizes uniflow's own tests
# tend to exercise: 1M 10M 100M 200M 500M 900M.
#
# Output files are named test_<size>.bin, e.g. test_1M.bin.

set -Eeuo pipefail

OUTPUT_DIR="${1:-./test_files}"
shift || true

if [[ $# -gt 0 ]]; then
    SIZES=("$@")
else
    SIZES=(1M 10M 100M 200M 500M 900M)
fi

mkdir -p "$OUTPUT_DIR"

out_files=()
for size in "${SIZES[@]}"; do
    out_file="$OUTPUT_DIR/test_${size}.bin"
    printf 'Creating %s (%s)...\n' "$out_file" "$size"
    dd if=/dev/urandom of="$out_file" bs="$size" count=1 iflag=fullblock status=none
    out_files+=("$out_file")
done

printf '\nDone. Files written to %s:\n' "$OUTPUT_DIR"
du -h "${out_files[@]}" | sed 's/^/  /'
