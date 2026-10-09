#!/bin/sh
# THE OSCILLATOR'S ASM DIFFERENTIAL: every shape, rendered on an emulated
# Cortex-M3, against the same shapes rendered on the host.
#
# Both sides run tools/osctest/driver.cc with the same deterministic PRNG over
# the same grid, so the hashes must match exactly. What that catches is the one
# class no golden can: a hand-written asm path that does not agree with the C it
# replaces. Until asm exists it still earns its place -- it proves the C itself
# renders identically on the target, which nothing else here checks.
set -e
cd "$(dirname "$0")"
ROOT=../..

echo "== host reference (clang) =="
sh ../osctest/build.sh > /dev/null 2>&1 || true
( cd ../osctest && ./osctest hash ) > host_hash.txt
echo "   $(wc -l < host_hash.txt | tr -d ' ') shapes"

echo "== target under QEMU (arm-none-eabi + emulated Cortex-M3) =="
( cd "$ROOT" && SKIP_PROGRAMMING=true ./env/mutable-env.sh 'set -e
  sh tools/oscqemu/build.sh
  cd tools/oscqemu
  qemu-system-arm -M lm3s6965evb -nographic -semihosting \
    -semihosting-config arg=test,arg=hash \
    -kernel test.elf
  mv qemu_out.txt qemu_hash.txt' ) > /dev/null

if diff -q host_hash.txt qemu_hash.txt > /dev/null 2>&1; then
  echo "ALL PASS: every shape renders identically on host and target"
  exit 0
fi
echo "FAIL: host and target disagree"
diff host_hash.txt qemu_hash.txt | head -20
exit 1
