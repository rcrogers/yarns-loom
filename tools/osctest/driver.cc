// Host driver for the OSCILLATOR SHAPES. Renders each shape's own function
// with a CONTROLLED timbre and gain buffer, not through Envelope::RenderSamples
// -- so what is pinned is the shape's arithmetic and nothing else. That is the
// point: the per-sample timbre is 15 bits today, and widening it means ten
// rescaled sites, each reading that value as a different quantity (a
// multiplier, a cutoff, a phase increment, a zone index). Feeding the buffer
// directly is what makes "same audio from the equivalent wider value" a
// checkable claim.
//
// Nothing rendered an oscillator sample off target before this.
//
// Usage: ./osctest <mode> [KEY=VALUE ...]
//   hash              one FNV-1a per shape, over every rendered sample
//   dump shape=<n>    the samples themselves
#define TEST 1
#define private public
#include "yarns/oscillator.h"
#include "yarns/drivers/dac.h"
#include "stmlib/utils/random.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using namespace yarns;

namespace {

Oscillator osc;

// The scale Init is given, and the pitches and timbre span every case walks.
// Three pitches so a shape whose warp tracks pitch is exercised at more than
// one, and a timbre RAMP so the per-sample path moves rather than sitting.
const uint16_t kScale = 32767;
// THE WHOLE KEYBOARD, because a shape whose warp tracks pitch renders different
// arithmetic at each end of it, and three pitches left most of that unpinned.
// EVERY FOURTH SEMITONE, which crosses all fifteen bandlimited zones and both
// ends of the keyboard. The whole run costs milliseconds, so the grid is set by
// what is worth examining rather than by what is affordable.
const int kPitches[] = {
   12 << 7,  16 << 7,  20 << 7,  24 << 7,  28 << 7,  32 << 7,  36 << 7,
   40 << 7,  44 << 7,  48 << 7,  52 << 7,  56 << 7,  60 << 7,  64 << 7,
   68 << 7,  72 << 7,  76 << 7,  80 << 7,  84 << 7,  88 << 7,  92 << 7,
   96 << 7, 100 << 7, 104 << 7, 108 << 7, 112 << 7, 116 << 7, 120 << 7,
  124 << 7,
};
// BOTH DIRECTIONS, because the per-sample timbre feeds interpolators whose
// slope has a sign, and a rising ramp exercises one of the two.
// A SWEPT timbre and a HELD one are different arithmetic: the interpolators
// carry a slope only while it moves, and a shape's steady state is what a held
// note sounds like.
enum TimbreSweep {
  kRising, kFalling, kRisingFromNegative, kHeldLow, kHeldHigh, kNumSweeps
};
const int kBlocks = 16;

// DUMP-ONLY OVERRIDES. The `hash` mode never sets them, so the goldens see the
// same 290 cases they always did. They exist because two open questions --
// WHISTLE's hum at TIMBRE 0, and its settling time against Q -- both need ONE
// pitch rendered LONG ENOUGH TO HAVE A SPECTRUM, and 16 blocks is 23 ms.
//   - narrowband noise has no second-scale steady state at high Q. Read the
//     whistle plan's "HOW TO MEASURE THIS SHAPE" before quoting a level off
//     anything this renders.
int g_blocks = kBlocks;
int g_pitch_only = -1;   // -1 = every pitch in kPitches
// Pitch in the oscillator's OWN units, 128 per semitone, so a glide can be
// walked finer than `pitch=` can name. A stepped artifact lives BETWEEN the
// semitones, and a semitone grid cannot see whether it steps or glides.
int g_pitch_raw = -1;
// A GLIDE. The pitch ramps from pitch_raw to pitch_raw2 across the whole run, so
// a stepped artifact shows up as a discontinuity in TIME rather than having to
// be inferred from a row of separate renders.
int g_pitch_raw2 = -1;
// A VIBRATO, which is the control the CZ stepping is provoked with and the one
// motion a glide cannot stand in for: it visits every pitch between its ends
// several times a second. Depth is the LFO's peak in pitch units -- VB=10 with
// VR=1 is 4 -- and rate is its frequency.
int g_vibrato_depth = 0;
int g_vibrato_rate_hz = 5;
// A 16-bit fraction of one pitch unit, matching what Voice hands the oscillator
// from the pitch LFO's interpolator. VB=10 moves the note by only +-4 WHOLE
// units, so the fraction is what makes a vibrato glide instead of step.
int g_pitch_frac = 0;
// The smallest step the pitch is delivered in, in 1/65536 pitch units. 65536 is
// what Voice delivered before it read the pitch LFO's interpolator to sixteen
// bits. 2048 is what it delivers NOW: the interpolator's slope is
// `(target - value()) << 16 >> 5`, so every value it can hold is a multiple of
// 1/32 of a unit. 1 is the harness's own, which no setting can produce.
int g_pitch_quantum = 1;
int g_sweep_only = -1;   // -1 = every sweep
// PANEL SEMANTICS. The timbre buffer a shape reads is the WARPED value, and
// several warps INVERT -- WHISTLE's TIMBRE 0 is the WIDEST damp, which is the
// LOWEST Q. A sweep indexed by the raw buffer is therefore indexed by damp and
// not by the knob, and reads backwards. With warp=1 the held value is put
// through the shape's OWN WarpTimbre first, so `timbre=` means the knob.
//   - the shape's own function, never a copy of its arithmetic here.
bool g_warp_timbre = false;

// Full-scale timbre at the CURRENT width. A wider one must render the same
// audio from the proportionally larger value, which is what the check asserts.
int g_timbre_max = 32767;
int g_gain = 32767;

// When set, the timbre buffer is HELD at this value instead of ramping, so a
// shape can be rendered at one point of its map.
bool g_hold_timbre = false;
int g_held_timbre = 0;

// Three renders of one shape, to compare which end a negative timbre lands at.
enum { kAtZero, kAtNegative, kAtTop, kNumCollected };
const size_t kCollected = 3 * kBlocks * kAudioBlockSize;
int16_t g_samples[kNumCollected][kCollected];
size_t g_collect_index = 0;
int g_collect_slot = -1;

uint32_t Fnv(uint32_t h, int16_t v) {
  return (h ^ static_cast<uint16_t>(v)) * 16777619u;
}

// Where the timbre ramp starts and ends, per sweep. The third runs from below
// zero, which a negative TIMBRE MOD ENVELOPE reaches and which every shape now
// has to hold -- see the `negative` mode for the property that pins it.
void SweepRange(int sweep, int* from, int* to) {
  switch (sweep) {
    case kFalling: *from = g_timbre_max; *to = 0; break;
    case kRisingFromNegative: *from = -g_timbre_max - 1; *to = g_timbre_max; break;
    case kHeldLow: *from = *to = g_timbre_max / 8; break;
    case kHeldHigh: *from = *to = g_timbre_max; break;
    default: *from = 0; *to = g_timbre_max; break;
  }
}

// THE GAIN HALF MOVES TOO. A shape the envelope EXCITES spends this going into
// what rings, so a decaying gain is a different render from a held one -- and
// the excitation shapes are exactly the ones whose arithmetic is being changed.
enum GainProfile { kGainFull, kGainDecaying, kNumGainProfiles };
int16_t GainAt(int profile, long step, long total) {
  if (profile == kGainFull) return static_cast<int16_t>(g_gain);
  return static_cast<int16_t>(g_gain - g_gain * step / total);
}

// Where the note sits at this block, in 16.16 pitch units: the static pitch and
// its fraction, plus whichever motion was asked for.
int32_t PitchAt(int16_t base, int block) {
  int32_t pitch_q16 = (static_cast<int32_t>(base) << 16) + g_pitch_frac;
  if (g_pitch_raw2 >= 0 && g_blocks > 1) {
    pitch_q16 += static_cast<int32_t>(
        (static_cast<int64_t>(g_pitch_raw2 - base) << 16) * block /
        (g_blocks - 1));
  }
  // yarns/resources/waveforms.py sets the rate the increments are built for.
  const double kAudioRate = 45000.0;
  const double seconds = static_cast<double>(block) * kAudioBlockSize / kAudioRate;
  pitch_q16 += static_cast<int32_t>(
      g_vibrato_depth * 65536.0 * sin(2 * M_PI * g_vibrato_rate_hz * seconds));
  return pitch_q16;
}

uint32_t HashShape(int shape, bool dump) {
  uint32_t hash = 2166136261u;
  // Per shape, so a noise shape's hash does not depend on how many draws the
  // shapes before it took.
  stmlib::Random::Seed(0x21);
  for (int gain_profile = 0; gain_profile < kNumGainProfiles; ++gain_profile)
  for (int sweep = 0; sweep < kNumSweeps; ++sweep) {
  if (g_sweep_only >= 0 && sweep != g_sweep_only) continue;
  const size_t pitch_cases =
      (g_pitch_only >= 0 || g_pitch_raw >= 0)
          ? 1 : sizeof(kPitches) / sizeof(kPitches[0]);
  for (size_t p = 0; p < pitch_cases; ++p) {
    // `pitch=` names the note to render, not a grid entry to select: asking for
    // one off the grid used to walk every case and match none, printing nothing
    // and exiting 0.
    const int16_t pitch = g_pitch_raw >= 0
        ? static_cast<int16_t>(g_pitch_raw)
        : (g_pitch_only >= 0
            ? static_cast<int16_t>(g_pitch_only << 7) : kPitches[p]);
    // One voice, so its share of the output budget is the whole of it and the
    // two shares coincide.
    osc.Init(kScale, kScale);
    osc.set_shape(static_cast<OscillatorShape>(shape));
    for (int b = 0; b < g_blocks; ++b) {
      // Once a block, which is where the render reads the increment: Voice
      // writes it at 4 kHz and RENDER_PERIODIC takes whatever stands.
      const int32_t pitch_q16 = PitchAt(pitch, b);
      const int32_t quantized =
          pitch_q16 / g_pitch_quantum * g_pitch_quantum;
      osc.Refresh(static_cast<int16_t>(quantized >> 16),
                  static_cast<uint16_t>(quantized), 0, 0);
      int16_t timbre_gain[2 * kAudioBlockSize];
      int16_t mix[kAudioBlockSize];
      memset(mix, 0, sizeof(mix));
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        // A ramp across the whole run, so every shape sees its timbre move.
        const long step = b * kAudioBlockSize + i;
        int from, to;
        SweepRange(sweep, &from, &to);
        timbre_gain[i] = g_hold_timbre
            ? (g_warp_timbre
                 ? osc.WarpTimbre(static_cast<int16_t>(g_held_timbre),
                                  static_cast<OscillatorShape>(shape))
                 : static_cast<int16_t>(g_held_timbre))
            : static_cast<int16_t>(
                from + (to - from) * step / (g_blocks * kAudioBlockSize));
        timbre_gain[i + kAudioBlockSize] =
            GainAt(gain_profile, step, g_blocks * kAudioBlockSize);
      }
      (osc.*Oscillator::fn_table_[shape])(timbre_gain, mix);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        hash = Fnv(hash, mix[i]);
        if (g_collect_slot >= 0 && g_collect_index < kCollected) {
          g_samples[g_collect_slot][g_collect_index++] = mix[i];
        }
        if (dump) printf("%d\n", mix[i]);
      }
    }
  }
  }
  return hash;
}

void CollectShape(int shape, int slot) {
  g_collect_slot = slot;
  g_collect_index = 0;
  HashShape(shape, false);
  g_collect_slot = -1;
}

int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "hash";
  g_timbre_max = OptInt(argc, argv, "timbre_max", 32767);
  g_gain = OptInt(argc, argv, "gain", 32767);

  if (!strcmp(mode, "dump")) {
    g_hold_timbre = OptInt(argc, argv, "hold", 0) != 0;
    g_held_timbre = OptInt(argc, argv, "timbre", 0);
    g_blocks = OptInt(argc, argv, "blocks", kBlocks);
    g_pitch_only = OptInt(argc, argv, "pitch", -1);   // a MIDI note, not an index
    g_pitch_raw = OptInt(argc, argv, "pitch_raw", -1);
    g_pitch_raw2 = OptInt(argc, argv, "pitch_raw2", -1);
    g_pitch_frac = OptInt(argc, argv, "pitch_frac", 0);
    g_vibrato_depth = OptInt(argc, argv, "vibrato", 0);
    g_vibrato_rate_hz = OptInt(argc, argv, "vibrato_hz", 5);
    g_pitch_quantum = OptInt(argc, argv, "pitch_quantum", 1);
    g_sweep_only = OptInt(argc, argv, "sweep", -1);
    g_warp_timbre = OptInt(argc, argv, "warp", 0) != 0;
    HashShape(OptInt(argc, argv, "shape", 0), true);
    return 0;
  }

  // BELOW THE BOTTOM OF THE MAP IS THE BOTTOM OF IT. The per-sample timbre is
  // signed and reaches negative values in the field: NoteOn warps the
  // DESTINATION, so a negative TIMBRE MOD ENVELOPE puts one in the buffer. A
  // shape's map is an ABSOLUTE POSITION -- a width, a cutoff, a damp -- so
  // below its bottom it must answer near its bottom. A shape that casts the
  // value unsigned instead WRAPS to the top of its range: the narrowest or
  // loudest thing it can do, at the moment the player asked for the least.
  //
  // Equality with timbre 0 is the WRONG test -- a continuous map moves a count
  // or two there and that is correct -- and so is asking where -32768 lands: a
  // map that simply CONTINUES below zero goes a long way without ever being
  // wrong. What a wrap is, is a DISCONTINUITY: one count below zero, the output
  // jumps across the range. So the probe is timbre -1, measured against the
  // map's whole span. A continuous map barely moves; a wrapped one leaps.
  if (!strcmp(mode, "negative")) {
    g_hold_timbre = true;
    int failures = 0;
    for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
      // ONLY THE SHAPES THAT CAN ACTUALLY SEE ONE. A warp that maps or clamps
      // negatives keeps them out of the buffer entirely, and feeding one to
      // such a shape's render tests a value the firmware cannot produce --
      // which reads as a failure and is not one.
      osc.set_shape(static_cast<OscillatorShape>(s));
      if (osc.WarpTimbre(-1, static_cast<OscillatorShape>(s), kPitches[0]) >= 0) {
        continue;
      }
      g_held_timbre = 0;       CollectShape(s, kAtZero);
      g_held_timbre = -1;      CollectShape(s, kAtNegative);
      g_held_timbre = 32767;   CollectShape(s, kAtTop);
      double to_negative = 0, to_top = 0;
      for (size_t i = 0; i < kCollected; ++i) {
        const double dn = g_samples[kAtZero][i] - g_samples[kAtNegative][i];
        const double dt = g_samples[kAtZero][i] - g_samples[kAtTop][i];
        to_negative += dn * dn;
        to_top += dt * dt;
      }
      const double jump = to_top > 0 ? sqrt(to_negative / to_top) : 0.0;
      // A continuous map moves by a count here; a wrapped one crosses its
      // range. MEASURED, the three that wrapped read 1.00 and the four that do
      // not read under 0.001, so anything above a hundredth is the wrap.
      const double kWrapped = 0.01;
      if (jump > kWrapped) {
        printf("FAIL shape %2d leaps %.2f of its map one count below zero\n",
               s, jump);
        ++failures;
      }
    }
    if (failures) {
      printf("\n%d shape(s) read the timbre unsigned, so a negative TIMBRE MOD\n"
             "ENVELOPE wraps them to the far end of their map.\n", failures);
      return 1;
    }
    printf("PASS no shape wraps when the timbre goes below zero\n");
    return 0;
  }
  for (int s = 0; s <= OSC_SHAPE_FM; ++s) {
    printf("%d %08x\n", s, HashShape(s, false));
  }
  return 0;
}
