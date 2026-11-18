#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d /var/tmp/rme-gles-model.XXXXXXXX)
echo "artifacts=$tmp"
# CPPFLAGS and LDLIBS are conventional, space-separated compiler argument lists.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CPPFLAGS:-} \
    "$here/model.c" "$here/mesa_lifecycle.c" ${LDLIBS:--lEGL -lGLESv2} -o "$tmp/lifecycle"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 "$tmp/lifecycle"
