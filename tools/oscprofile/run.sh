#!/bin/bash
# Profile shapes under QEMU: run.sh [shape ...] (default: every shape).
# Runs in the toolchain container from the repo root, after build.sh. Each
# shape's trace goes through a FIFO straight into fold.py: a whole-grid trace
# is gigabytes, and writing it through the host mount is what made it slow.
set -e
OUT=build/oscprofile
SHAPE_COUNT=$(python3 tools/osc_profile.py --shape-count)
SHAPES=${*:-$(seq 0 $((SHAPE_COUNT - 1)))}
profile_shape() {
  local dir=$OUT/shape$1
  rm -rf "$dir" && mkdir -p "$dir"
  mkfifo "$dir/trace"
  (cd "$dir" && qemu-system-arm -M lm3s6965evb -display none -serial null \
     -monitor none -semihosting -semihosting-config arg=prof,arg=shape=$1 \
     -kernel ../profile.elf -d exec,in_asm,nochain -D trace < /dev/null \
     > qemu.log 2>&1) &
  python3 tools/oscprofile/fold.py $OUT/profile.dis $OUT/profile.sym \
    < "$dir/trace" > "$dir/fold.json"
  wait
  rm "$dir/trace"
}
export OUT
export -f profile_shape
printf '%s\n' $SHAPES | xargs -P "$(nproc)" -I{} bash -c 'profile_shape {}'
