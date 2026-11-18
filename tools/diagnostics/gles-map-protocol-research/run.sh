#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rme-host-map-test.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
cp -- "$src"/pm.c "$src"/pm.h "$src"/gm.c "$src"/gm.h "$src"/wire.h "$src"/egl_probe.py "$src"/test_pm.py "$src"/test_socket.py "$src"/test_wire_faults.py "$src"/test_faults.c "$src"/test_pm_safety.c "$src"/test_group_retire.c "$src"/guard.c "$src"/guard.h "$src"/test_decoder_guard.c "$tmp/"
cd "$tmp"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fPIC -shared -pthread pm.c -o libpm.so
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fPIC -shared gm.c -o libgm.so
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pthread pm.c test_faults.c -o test_faults
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pthread pm.c test_pm_safety.c -o test_pm_safety
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pthread pm.c test_group_retire.c -o test_group_retire
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -pthread pm.c guard.c test_decoder_guard.c -o test_decoder_guard
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address -fno-omit-frame-pointer -pthread pm.c test_pm_safety.c -o test_pm_safety_asan
python3 test_pm.py
python3 test_socket.py
python3 test_wire_faults.py
./test_faults host_map_rollback
./test_faults flush_error
./test_faults unmap_error
./test_group_retire
./test_decoder_guard
printf '%s\n' 'HOST_MAP_SUCCESS_AND_FAULT_REPORT_TESTS_OK'
safety_failed=0
for case_name in aba race lifecycle errors; do
  if ./test_pm_safety "$case_name"; then
    printf 'SAFETY %s: passed\n' "$case_name"
  else
    printf 'SAFETY %s: FAILED\n' "$case_name" >&2
    safety_failed=1
  fi
done
if ./test_pm_safety_asan uaf; then
  printf '%s\n' 'SAFETY uaf: passed'
else
  printf '%s\n' 'SAFETY uaf: FAILED' >&2
  safety_failed=1
fi
if [ "$safety_failed" -ne 0 ]; then
  printf '%s\n' 'SAFETY REGRESSIONS FAILED: research prototype is not safe to integrate' >&2
  exit 1
fi
