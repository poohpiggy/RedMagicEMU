#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
tmp=$(mktemp -d /tmp/rme-host-surface-test.XXXXXXXX)
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
  -I"$repo/tools/diagnostics/gfxstream-host-android-surface/include" \
  -I"$repo/native/vulkanbridge" \
  "$repo/tools/diagnostics/gfxstream-host-android-surface/test.c" \
  "$repo/native/vulkanbridge/host_surface.c" -o "$tmp/test"
"$tmp/test"
echo 'PASS: instance-specific dispatch, native window lifetime and errors'
