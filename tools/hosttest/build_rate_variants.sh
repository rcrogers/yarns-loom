#!/bin/sh
# Build host binaries that differ ONLY in how the chiff's slew rate moves
# across a run, so the schedule can be measured against the truth rather than
# argued about. Every variant is the REAL yarns/envelope.cc with one anchored
# patch; anchors are asserted, so a moved line kills this loudly rather than
# silently building four copies of the same thing.
#
#   exact    the rate recomputed from the slew time EVERY SAMPLE. Not shippable
#            -- an exp2 per sample -- and not meant to be: it is the reference
#            the other three are errors against.
#   taylor   the per-sample decay 160272de deleted. THE DECISION BASELINE: a
#            revert restores exactly this, at 6.0 points of CPU.
#   run      the rate held at the RUN'S START. What ships today (160272de).
#   mid      held at the run's MIDPOINT. The free re-centring.
#   word     refined at every draw word, which is what the tree now does.
#   wordmid  the same, started half a word in, so each word sits on its own
#            midpoint.
set -e
cd "$(dirname "$0")"

RATE_DECL='    int32_t chiff_slew_rate_q31 = static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_q5_27));'
WORD_CALL='          YARNS_CHIFF_REFINE_RATE_AT_WORD();'
SAMPLE_HEAD='#define YARNS_CHIFF_RENDER_SAMPLE(draw)                                       \
  do {                                                                        \'

build() {
  variant="$1"
  src="envelope_rate_$variant.cc"
  python3 ../portable_envelope.py ../.. "$src"
  VARIANT="$variant" RATE_DECL="$RATE_DECL" WORD_CALL="$WORD_CALL" \
    SAMPLE_HEAD="$SAMPLE_HEAD" SRC="$src" python3 - <<'PYEOF'
import os
path = os.environ['SRC']
variant = os.environ['VARIANT']
rate_decl = os.environ['RATE_DECL']
word_call = os.environ['WORD_CALL']
sample_head = os.environ['SAMPLE_HEAD']
src = open(path).read()

def sub(old, new, what):
    global src
    assert old in src, '%s anchor moved (%s)' % (what, variant)
    src = src.replace(old, new, 1)

def drop_word_refinement():
    sub(word_call, '          // VARIANT: no per-word refinement', 'word-call')

if variant == 'run':
    drop_word_refinement()
elif variant == 'mid':
    drop_word_refinement()
    sub(rate_decl,
        '''    int32_t chiff_slew_rate_q31 = static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_q5_27
        + chiff_decay.slew_time_step_q5_27 * run_samples / 2));''',
        'rate-decl')
elif variant == 'exact':
    drop_word_refinement()
    sub(rate_decl, rate_decl + '''
    uint32_t chiff_slew_time_running_q5_27 = chiff_slew_time_q5_27;
    const uint32_t chiff_slew_time_sample_step_q5_27 =
      chiff_decay.slew_time_step_q5_27;''', 'rate-decl')
    sub(sample_head, sample_head + '''
    chiff_slew_rate_q31 = static_cast<int32_t>(                               \\
      SlewRateFromTimeLog2_q31(chiff_slew_time_running_q5_27));               \\
    chiff_slew_time_running_q5_27 += chiff_slew_time_sample_step_q5_27;       \\''',
        'sample-head')
elif variant == 'taylor':
    # EXACTLY what 160272de deleted: a 2-term Taylor of 1 - 2^-x about x = 0,
    # applied per sample. This is what a REVERT restores, so it is the accuracy
    # any cheaper schedule has to be judged against -- not `exact`, which no
    # shippable build can afford.
    drop_word_refinement()
    sub(rate_decl, rate_decl + '''
    const uint32_t kLn2_q28 = static_cast<uint32_t>(
      __builtin_log(2.0) * 268435456.0 + 0.5);
    const int64_t taylor_u_q32 =
      (static_cast<int64_t>(chiff_decay.slew_time_step_q5_27) * kLn2_q28) >> 23;
    const int32_t chiff_slew_rate_decay_q32 = static_cast<int32_t>(
      taylor_u_q32 - ((taylor_u_q32 * taylor_u_q32) >> 33));''', 'rate-decl')
    sub(sample_head, sample_head + '''
    chiff_slew_rate_q31 -= static_cast<int32_t>(                              \\
      (static_cast<int64_t>(chiff_slew_rate_q31)                              \\
       * chiff_slew_rate_decay_q32) >> 32);                                   \\''',
        'sample-head')
elif variant == 'wordmid':
    sub(rate_decl,
        '''    int32_t chiff_slew_rate_q31 = static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_q5_27
        + chiff_decay.slew_time_step_q5_27 * (kChiffDrawsPerWord / 2)));''',
        'rate-decl')
elif variant == 'halfwordmid':
    # HOW ACCURACY SCALES WITH SEGMENT LENGTH. Refines twice a word, so the
    # asm would need three more instructions per word than `wordmid` -- this
    # exists to price that before anyone writes it.
    sub('''      SlewRateFromTimeLog2_q31(kChiffDrawsPerWord * std::min<uint32_t>(''',
        '''      SlewRateFromTimeLog2_q31((kChiffDrawsPerWord / 2) * std::min<uint32_t>(''',
        'retained-factor')
    sub(rate_decl,
        '''    int32_t chiff_slew_rate_q31 = static_cast<int32_t>(
      SlewRateFromTimeLog2_q31(chiff_slew_time_q5_27
        + chiff_decay.slew_time_step_q5_27 * (kChiffDrawsPerWord / 4)));''',
        'rate-decl')
    sub('''                                   & kChiffDrawValueMax));
          }''',
        '''                                   & kChiffDrawValueMax));
            if (i == kChiffDrawsPerWord / 2 - 1) YARNS_CHIFF_REFINE_RATE_AT_WORD();
          }''', 'half-word-call')
elif variant != 'word':
    raise SystemExit('unknown variant ' + variant)

open(path, 'w').write(src)
PYEOF
  clang++ -std=c++11 -O1 -w -DTEST -I shim -I ../.. \
    "$src" ../../yarns/resources.cc ../../yarns/utils.cc driver.cc \
    -o "test_rate_$variant"
  echo "built test_rate_$variant"
}

for v in exact taylor run mid word wordmid halfwordmid; do build "$v"; done
