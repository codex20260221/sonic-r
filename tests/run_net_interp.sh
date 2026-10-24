#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/sonicr-net-interp.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

# CFLAGS can add sanitizers or optimization; no SDL libraries or assets needed.
# Intentional word splitting allows the usual space-separated compiler flags.
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$repo_dir/source/sdl/src" \
    "$repo_dir/tests/test_net_interp.c" \
    "$repo_dir/source/sdl/src/net/net_interp.c" \
    ${LDFLAGS:-} -o "$test_dir/test_net_interp"
"$test_dir/test_net_interp"
