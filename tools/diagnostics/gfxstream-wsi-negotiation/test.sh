#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d /var/tmp/rme-wsi-negotiate-test.XXXXXXXX)
/var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx -std=c++17 -Wall -Wextra -Werror \
    -I"$here" "$here/test.cpp" -o "$work/test"
"$work/test"
echo 'WSI_NEGOTIATION_LEGACY_AND_PER_CHANNEL_OK'
