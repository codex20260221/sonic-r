#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/sonicr-settings.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

# Link the production loader; discarded save.c functions need no game stubs.
# No SDL libraries, proprietary data, or new test framework are required.
case $(uname -s) in
    Darwin) dead_code_flag=-Wl,-dead_strip ;;
    *) dead_code_flag=-Wl,--gc-sections ;;
esac

# Intentional word splitting supports compiler wrappers and extra test flags.
# shellcheck disable=SC2086
${CC:-cc} -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-result ${CFLAGS:-} \
    -ffunction-sections -fdata-sections -DSONICR_SDL \
    -I"$repo_dir/source/sdl/src" \
    "$repo_dir/tests/test_settings.c" "$repo_dir/source/sdl/src/save.c" \
    "$dead_code_flag" ${LDFLAGS:-} -o "$test_dir/test_settings"

cd "$test_dir"
./test_settings
