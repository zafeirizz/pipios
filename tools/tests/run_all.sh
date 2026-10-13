#!/usr/bin/env bash
# AuroraOS test suite (driven by `make test`).
#
#   usage: run_all.sh [M1 M2 ...]          # default: M1..M6
#   AURORA_TEST_SUITES=boot,input,gui      # pick suites (default: all three)
#
# The FAT32 tests mutate the raw disk image, so every boot test starts from a
# freshly built build/disk.img. Nothing under kernel/, boot/ or tools/ is
# modified by the tests.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

ALL=(M1 M2 M3 M4 M5 M6)
[ $# -gt 0 ] && ALL=("$@")
SUITES="${AURORA_TEST_SUITES:-boot,input,gui}"

has() { case ",$SUITES," in *",$1,"*) return 0 ;; esac; return 1; }

fresh_image() {
  rm -f build/disk.img
  make >/dev/null 2>&1
}

fail=0
results=()
report() {                       # report <label> <ok|bad>
  results+=("$1")
  [ "$2" = ok ] || fail=1
}

echo "=== build ==="
if ! make >/dev/null; then
  echo "BUILD FAILED"
  exit 1
fi

if has boot; then
  for m in "${ALL[@]}"; do
    fresh_image
    if bash tools/tests/test_boot.sh "AURORA-${m}-PASS" 120 >"build/test_${m}.log" 2>&1; then
      echo "${m}: PASS"
      report "${m} PASS" ok
    else
      echo "${m}: FAIL (build/test_${m}.log)"
      grep -E 'panic|PANIC|EXCEPTION|#GP|#PF' build/serial.log 2>/dev/null | head -3
      tail -n 5 "build/test_${m}.log" 2>/dev/null
      report "${m} FAIL" bad
    fi
  done
fi

if has input; then
  fresh_image
  if bash tools/tests/test_input.sh >build/test_input.log 2>&1; then
    echo "input: PASS (PS/2 keyboard + mouse via QEMU monitor)"
    report "input PASS" ok
  else
    echo "input: FAIL (build/test_input.log)"
    tail -n 15 build/test_input.log 2>/dev/null
    report "input FAIL" bad
  fi
fi

if has gui; then
  # Generous timeout: this boot runs after several others and is slow under
  # host load, which is what made the suite look flaky when this was 120s.
  if bash tools/tests/screendump.sh AURORA-M6-PASS build/gui.ppm 180 >build/test_gui.log 2>&1; then
    echo "gui: PASS (screendump -> build/gui.ppm)"
    report "gui PASS" ok
  else
    echo "gui: FAIL (build/test_gui.log)"
    tail -n 15 build/test_gui.log 2>/dev/null
    report "gui FAIL" bad
  fi
fi

echo
echo "=== summary ==="
for r in "${results[@]}"; do echo "  $r"; done
if [ "$fail" = 0 ]; then
  echo "ALL TESTS PASS"
else
  echo "SOME TESTS FAILED"
fi
exit "$fail"
