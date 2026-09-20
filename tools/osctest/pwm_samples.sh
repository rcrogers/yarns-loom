#!/bin/sh
# AUDIO for the audio-rate PWM run, and the shapes worth hearing it against.
#
#   sh tools/osctest/pwm_samples.sh sweep [outdir]   every ratio, TIMBRE swept
#   sh tools/osctest/pwm_samples.sh held  [outdir]   steady timbre, a selection
#
# SWEEP is the one that tells you what a ratio does. Held timbre is one depth
# out of the whole control, and every ratio sounds static at a fixed depth --
# what moves the sound is the depth opening up, which is what TIMBRE does in
# the field.
#
# Gain profile 0 is the constant one and `dump` renders both, so each case is
# cut to one profile's worth of samples. `warp=1` feeds the warped timbre,
# which is what Voice hands a render -- the raw value would be a tone the
# firmware cannot make.
set -e
cd "$(dirname "$0")/../.."
MODE="${1:-sweep}"
OUT="${2:-$HOME/Desktop/yarns-pwm-$MODE}"
mkdir -p "$OUT"

OSC=./tools/osctest/osctest
RATE=45000
# The run's first shape, read off the build so this cannot go stale against
# the enum.
PWM_BASE=$($OSC shapes | awk '/^audio_rate_pwm_base/ {print $2}')
RATIOS=$($OSC shapes | awk '/^ratios/ {print $2}')

wrap() {                        # name < raw samples
  node tools/osctest/wav.js rate="$RATE" > "$OUT/$1.wav"
  printf '  %-32s %8s bytes\n' "$1.wav" "$(wc -c < "$OUT/$1.wav" | tr -d ' ')"
}

# ---------------------------------------------------------------- sweep mode
# One pass of TIMBRE across its whole range, 10 s, every ratio in the run.
# sweep=0 is the rising case: raw 0 to timbre_max, warped per sample.
SWEEP_SECONDS=10
SWEEP_BLOCKS=$(( (RATE * SWEEP_SECONDS + 63) / 64 ))
SWEEP_SAMPLES=$((SWEEP_BLOCKS * 64))

# Index order matches lut_pwm_ratio_names.
NAMES="1over1 2over1 3over1 5over1 7over1 5over2 7over2 9over2 7over3 8over3
9over4 mink_4over9 mink_3over7 mink_2over9 mink_2over7 mink_2over5 mink_1over7
mink_1over5 mink_1over3 pi_over_4 pi_over_3 pi_over_2 pi two_pi three_pi
three_pi_over_2"

sweep_all() {
  echo "writing $RATIOS ratios x ${SWEEP_SECONDS}s to $OUT"
  i=0
  for name in $NAMES; do
    shape=$((PWM_BASE + i))
    label=$(printf 'pwm_%02d_%s' "$i" "$name")
    $OSC dump shape="$shape" pitch=48 sweep=0 warp=1 blocks="$SWEEP_BLOCKS" \
      | head -n "$SWEEP_SAMPLES" | wrap "$label"
    i=$((i + 1))
  done
  # The same sweep on the shapes the run is heard against.
  $OSC dump shape=18 pitch=48 sweep=0 warp=1 blocks="$SWEEP_BLOCKS" \
    | head -n "$SWEEP_SAMPLES" | wrap "ref_static_pulse_width_mod"
  $OSC dump shape=$((PWM_BASE - RATIOS + 5)) pitch=48 sweep=0 warp=1 \
    blocks="$SWEEP_BLOCKS" | head -n "$SWEEP_SAMPLES" | wrap "ref_FM_5over2"
}

# ----------------------------------------------------------------- held mode
HELD_BLOCKS=1100                # 70400 samples, 1.56 s
HELD_SAMPLES=$((HELD_BLOCKS * 64))
HELD_CASES="
18:48:0:ref-01_static_pulse_50pct
18:48:32767:ref-02_static_pulse_narrow
50:48:32767:ref-03_FM_5over2
72:48:32767:pwm-01_2over1_integer_static
76:48:32767:pwm-02_5over2
90:48:32767:pwm-03_pi_over_4
90:48:8192:depth-01_pi_over_4_timbre_8192
90:48:16384:depth-02_pi_over_4_timbre_16384
90:72:32767:pitch-01_pi_over_4_midi72
90:84:32767:pitch-02_pi_over_4_midi84
"

held_all() {
  echo "writing held-timbre cases to $OUT"
  for c in $HELD_CASES; do
    $OSC dump shape="$(echo "$c" | cut -d: -f1)" pitch="$(echo "$c" | cut -d: -f2)" \
      timbre="$(echo "$c" | cut -d: -f3)" hold=1 warp=1 sweep=4 \
      blocks="$HELD_BLOCKS" | head -n "$HELD_SAMPLES" | wrap "$(echo "$c" | cut -d: -f4)"
  done
}

case "$MODE" in
  sweep) sweep_all ;;
  held)  held_all ;;
  *) echo "usage: $0 [sweep|held] [outdir]" >&2; exit 1 ;;
esac
echo "done"
