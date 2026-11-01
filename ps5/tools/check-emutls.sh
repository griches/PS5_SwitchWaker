#!/usr/bin/env bash
# Check thread-exit TLS lifetime on a POSIX build host.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc=${CC:-cc}
"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
    -DEMUTLS_SKIP_DESTRUCTOR_ROUNDS=1 \
    "$root/ps5/src/emutls.c" "$root/ps5/tests/emutls-exit.c" \
    -o "$work/emutls-exit"
"$work/emutls-exit"
echo "Emutls thread-exit host check passed (128 worker threads)."
