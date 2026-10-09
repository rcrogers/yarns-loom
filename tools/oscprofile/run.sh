#!/bin/bash
# Profile under QEMU, in the toolchain container from the repo root, after
# build.sh:
#   run.sh [shape ...]   oscillator shapes (default: every one), into shape<n>/
#   run.sh env           the envelope grid, split across the cores, into env<k>/
# Each run's trace goes through a FIFO straight into fold.py. QEMU logs only
# the code a measured call can reach (ranges.py says why that loses nothing):
# the rest is the harness preparing each call, most of an unfiltered trace.
set -e
OUT=build/oscprofile
filter() {
  python3 tools/oscprofile/ranges.py $OUT/profile.dis $OUT/profile.sym "$1"
}
# profile DIRECTORY FILTER SEMIHOSTING_ARGUMENTS [FUNCTION ...]: fold.py counts
# each FUNCTION's entries per measured call.
profile() {
  local dir=$OUT/$1
  rm -rf "$dir" && mkdir -p "$dir"
  mkfifo "$dir/trace"
  (cd "$dir" && qemu-system-arm -M lm3s6965evb -display none -serial null \
     -monitor none -semihosting -semihosting-config "arg=prof,$3" \
     -kernel ../profile.elf -d exec,in_asm,nochain -dfilter "$2" -D trace \
     < /dev/null > qemu.log 2>&1) &
  python3 tools/oscprofile/fold.py $OUT/profile.dis $OUT/profile.sym "${@:4}" \
    < "$dir/trace" > "$dir/fold.json"
  wait
  rm "$dir/trace"
}
export OUT
export -f profile
if [ "$1" = env ]; then
  FILTER=$(filter '^_ZN5yarns8Envelope(6NoteOn|7NoteOff|13RenderSamples)E')
  PARTS=$(nproc)
  rm -rf $OUT/env*
  seq 0 $((PARTS - 1)) | xargs -P "$PARTS" -I{} bash -c \
    "profile env{} '$FILTER' 'arg=env,arg=part={},arg=parts=$PARTS' \
       _ZN5yarns8Envelope18HandOffToNextStage"
else
  FILTER=$(filter '^_ZN5yarns10Oscillator(\d+Render|10RenderLoop)')
  SHAPES=${*:-$(seq 0 $(($(python3 tools/osc_profile.py --shape-count) - 1)))}
  printf '%s\n' $SHAPES | xargs -P "$(nproc)" -I{} bash -c \
    "profile shape{} '$FILTER' 'arg=shape={}'"
fi
