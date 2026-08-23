// Host/QEMU driver for the REAL yarns envelope (dart-model port). Renders
// scenarios and streams per-sample int16 outputs as text (one per line).
// Shared by the clang host harness (C path) and the bare-metal QEMU harness
// (asm path): identical scenarios, inputs, and PRNG, so the two dumps diff
// bit-for-bit. Streamed (not buffered into a full-take array) so it also fits
// the QEMU M3 machine's small RAM. Usage: ./test <scenario> ; one int/line.
#define private public
#define TEST 1
#include "yarns/envelope.h"
#include "yarns/drivers/dac.h"
#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace yarns;
using namespace stmlib;

static Envelope env;
static ADSR adsr;

// Verify mode ("hash=1" arg): fold the whole sample stream into one FNV-1a
// value and print only that, instead of one line per sample. The QEMU
// differential renders ~1M samples through double-emulated semihosting, where
// per-sample text I/O drowns the run; a single hash line does not, and a hash
// mismatch still flags any bit divergence (fall back to the default per-sample
// dump, below, to locate it).
static bool g_hash_mode = false;
static uint32_t g_hash = 2166136261u;  // FNV-1a offset basis

static uint32_t IncFromSamples(uint32_t samples) {
  return samples ? (UINT32_MAX / samples) : 0;
}

// Front-panel ENV ATTACK/DECAY/RELEASE setting (0..127) -> phase increment,
// via the exact chain Part::VoiceNoteOn uses. Milliseconds cannot express what
// the module actually does: the setting picks a LUT entry, and only 128 stage
// lengths exist.
static uint32_t IncFromSetting(int setting) {
  return stmlib::Interpolate88(
      lut_envelope_phase_increments,
      stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
}
static uint16_t SustainFromSetting(int setting) {
  return stmlib::modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (16 - 13);
}

// Stream each rendered sample as one line -- no full-take buffer, so the same
// driver fits the QEMU M3 machine's small RAM. Byte-identical output to the
// previous buffer-then-dump form (same samples, same order). In hash mode the
// samples fold into g_hash instead of printing.
// Tremolo depth, as Oscillator::Render drives it: the bias TARGET is sampled
// once per block from the envelope's own value, and RenderSamples then ramps
// the bias linearly toward that (already stale) target across the block. Zero
// -- the default -- reproduces the old bias-free behaviour exactly, so every
// existing scenario and every QEMU hash is untouched.
//
// This exists because bias was a BLIND SPOT: every check here, in the sim and
// in the QEMU differential rendered with bias == 0, so nothing could see an
// artifact that needs a moving bias to appear. A per-block-sampled,
// value-dependent input is exactly the shape of thing that can look smooth
// per sample and rough per block.
static uint16_t g_tremolo = 0;

// An INDEPENDENT bias, the way a timbre LFO drives it: not scaled to the
// envelope, so envelope + bias can leave the DAC range and the clamp actually
// has to bite. Tremolo cannot do this -- it is negative feedback proportional
// to the envelope's own value, so the sum stays near range however deep it is
// set, and a bias test built only on tremolo exercises the folding but never
// the CLIP path. Amplitude in s16; the sign alternates every `g_bias_lfo_blocks`
// blocks so the ramp is always live.
static int32_t g_bias_lfo = 0;
static int g_bias_lfo_blocks = 8;
static int g_block_counter = 0;

// Report the range of the RECOVERED envelope value instead of samples. The
// render state carries envelope + bias and is clamped as a whole, so after a
// clamp the envelope is recovered as state - bias and NOTHING bounds it
// directly any more: state pinned at 0 under a positive bias recovers a
// NEGATIVE value, and pinned at full under a negative bias recovers one above
// the note's peak. The output samples cannot show this -- they are clamped and
// look fine either way -- so this reads value_q30_ between blocks, which is
// exactly where it is written.
static int g_value_range = 0;
// Print the envelope value per block instead of its extremes. If bias behaves
// as a saturating add at the OUTPUT, the envelope's own trajectory must be
// identical whatever the bias is; two traces that diverge are the clamp
// feeding back into the envelope, i.e. accumulated damage to its state.
static int g_value_trace = 0;
// The chiff's slew time per block, Q5.27. ChiffScaledRmsPerInput is an
// approximation whose error depends only on the slew time, so this is what
// says how much of a real chiff's life is spent where the error is material.
static int g_slew_trace = 0;
// THE CHIFF TERM ITSELF, unclamped, with nothing subtracted. Every other way
// of reading it is contaminated: the output carries the envelope and the bias,
// and a difference against an AMOUNT 0 run carries both runs' rounding.
static int g_chiff_state_trace = 0;
// THE CHIFF'S STATE, as the model defines it: the drive it is being pushed
// with, the slew time its filter is running at, and the input it is chasing.
// Those three ARE the chiff -- everything audible follows from them. The model
// says a note started at amount A must PASS THROUGH the triple that amount B
// holds at its onset, so this is what has to be compared, not two summary
// statistics of the output. Level and centroid cannot tell a clipped quiet
// signal from an unclipped loud one; this can.
static int g_chiff_trace = 0;
static int32_t g_value_min = INT32_MAX;
static int32_t g_value_max = INT32_MIN;

static void RenderMs(double ms) {
  size_t n = (size_t)(ms * 45.0);
  for (size_t i = 0; i < n; i += kAudioBlockSize) {
    int16_t buffer[kAudioBlockSize];
    int32_t bias_target_q31 = g_tremolo
        ? static_cast<int32_t>(env.tremolo(g_tremolo)) << 16 : 0;
    if (g_bias_lfo) {
      const bool high = ((g_block_counter / g_bias_lfo_blocks) & 1) == 0;
      bias_target_q31 += (high ? g_bias_lfo : -g_bias_lfo) << 16;
    }
    ++g_block_counter;
    env.RenderSamples(buffer, bias_target_q31);
    if (g_chiff_trace) {
      printf("%d %u %d\n", env.chiff_drive_q30_,
             env.chiff_slew_time_log2_q5_27_, env.ChiffInput_q30());
      continue;
    }
    if (g_slew_trace) {
      // Third field is the END OF THE AXIS -- the slowest slew this note's
      // duration allows. The engine's own value, never re-derived here: a
      // second derivation would be a second source of truth.
      printf("%u %d %u\n", env.chiff_slew_time_log2_q5_27_,
             env.chiff_input_fraction_q30_, env.chiff_slew_time_at_amount_zero_q5_27_);
      continue;
    }
    if (g_chiff_state_trace) {
      // scaled back up out of the loop's shifted domain, so it reads as s16
      printf("%d\n", env.chiff_state_q30_ << 4 >> 15);
      continue;
    }
    if (g_value_trace) { printf("%d\n", env.value_q30_); continue; }
    if (g_value_range) {
      if (env.value_q30_ < g_value_min) g_value_min = env.value_q30_;
      if (env.value_q30_ > g_value_max) g_value_max = env.value_q30_;
      continue;
    }
    for (size_t j = 0; j < kAudioBlockSize; ++j) {
      if (g_hash_mode) g_hash = (g_hash ^ (uint16_t)buffer[j]) * 16777619u;
      else printf("%d\n", buffer[j]);
    }
  }
}

// Optional named overrides (KEY=VALUE argv entries) so a sweep can reproduce
// the user's actual front-panel knobs instead of the harness's stock ADSR.
// Positional args stay as they were: scenario amount duration [attack_ms].
static int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

int main(int argc, char** argv) {
  const char* scenario = argc > 1 ? argv[1] : "basic";
  uint8_t amount = argc > 2 ? atoi(argv[2]) : 96;
  uint8_t duration = argc > 3 ? atoi(argv[3]) : 67;
  // CHIFF DURATION names a time on its own table now; NoteOn takes the
  // increment, not the setting. Converted once here because the two are both
  // integers and passing the wrong one converts silently.
  const uint32_t chiff_audible_samples = ChiffAudibleSamples(Interpolate88(
    lut_chiff_phase_increments, static_cast<uint16_t>(duration) << (15 - 7)));
  // KEY=VALUE flag so it never lands in the positional attack_ms slot.
  g_hash_mode = OptInt(argc, argv, "hash", 0) != 0;
  g_tremolo = static_cast<uint16_t>(OptInt(argc, argv, "tremolo", 0));
  g_bias_lfo = OptInt(argc, argv, "bias_lfo", 0);
  g_value_range = OptInt(argc, argv, "value_range", 0);
  g_value_trace = OptInt(argc, argv, "value_trace", 0);
  g_slew_trace = OptInt(argc, argv, "slew_trace", 0);
  g_chiff_state_trace = OptInt(argc, argv, "chiff_state_trace", 0);
  g_chiff_trace = OptInt(argc, argv, "chiff_trace", 0);
  g_bias_lfo_blocks = OptInt(argc, argv, "bias_lfo_blocks", 8);

  int peak_pct = OptInt(argc, argv, "peak", 100);
  int sustain_pct = OptInt(argc, argv, "sustain", 60);
  adsr.peak_u16 = static_cast<uint16_t>(65535L * peak_pct / 100);
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * sustain_pct / 100);
  int atkms = argc > 4 && strchr(argv[4], '=') == NULL ? atoi(argv[4]) : 1200;
  atkms = OptInt(argc, argv, "attack", atkms);
  int decms = OptInt(argc, argv, "decay", 300);
  int relms = OptInt(argc, argv, "release", 400);
  int gatems = OptInt(argc, argv, "gate", 2000);
  int max_target = OptInt(argc, argv, "range", 16383);
  adsr.attack_u32 = IncFromSamples(atkms * 45);
  adsr.decay_u32 = IncFromSamples(decms * 45);
  adsr.release_u32 = IncFromSamples(relms * 45);
  // Front-panel SETTINGS (0..127) override the millisecond forms. These are
  // what the module actually exposes; prefer them.
  int atk_set = OptInt(argc, argv, "attack_setting", -1);
  int dec_set = OptInt(argc, argv, "decay_setting", -1);
  int rel_set = OptInt(argc, argv, "release_setting", -1);
  int sus_set = OptInt(argc, argv, "sustain_setting", -1);
  if (atk_set >= 0) adsr.attack_u32 = IncFromSetting(atk_set);
  if (dec_set >= 0) adsr.decay_u32 = IncFromSetting(dec_set);
  if (rel_set >= 0) adsr.release_u32 = IncFromSetting(rel_set);
  if (sus_set >= 0) adsr.sustain_u16 = SustainFromSetting(sus_set);
  if (OptInt(argc, argv, "report", 0)) {
    // Diagnostic: the chiff window is now attack-relative, so read it back
    // from the envelope after a NoteOn rather than any absolute table.
    env.Init(0);
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    uint32_t attack_smp = adsr.attack_u32 ? UINT32_MAX / adsr.attack_u32 : 0;
    // The ratio to the attack is now only a diagnostic, not the definition.
    fprintf(stderr,
      "rate %u Hz, attack %u smp, chiff %u smp (%.3fx attack), "
      "decay %u smp, release %u smp\n",
      kFrameHz, attack_smp, chiff_audible_samples,
      attack_smp ? double(chiff_audible_samples) / attack_smp : 0.0,
      adsr.decay_u32 ? UINT32_MAX / adsr.decay_u32 : 0,
      adsr.release_u32 ? UINT32_MAX / adsr.release_u32 : 0);
    return 0;
  }

  // seed=N SELECTS A REALIZATION. The envelope's PRNG seed lives in an
  // anonymous namespace, so this TU cannot set it -- but Init hands out the
  // next one on every call, advancing by a stride chosen to put successive
  // seeds far apart in xorshift32's single orbit. So N spare Inits select the
  // Nth sequence, which is the same mechanism a second voice would get.
  //
  // WITHOUT THIS THE HARNESS HAS EXACTLY ONE REALIZATION: every process starts
  // fresh, so every run of every sweep in this directory was the same noise.
  // One realization dips wherever it likes; the plan records a single seed
  // inventing a 7.6 dB failure that twelve others put at 0.0.
  const int seed = OptInt(argc, argv, "seed", 0);
  for (int i = 0; i < seed; ++i) env.Init(0);
  env.Init(strcmp(scenario, "inverted") == 0 ? 16383 : 0);  // rest = release level

  if (strcmp(scenario, "basic") == 0) {
    // gate, then release to the end
    env.NoteOn(adsr, 0, max_target, amount, chiff_audible_samples);
    RenderMs(gatems);
    env.NoteOff();
    RenderMs(OptInt(argc, argv, "tail", relms > 1000 ? relms + 200 : 1000));
  } else if (strcmp(scenario, "early_release") == 0) {
    // release 60ms into the attack
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(60);
    env.NoteOff();
    RenderMs(1000);
  } else if (strcmp(scenario, "retrigger") == 0) {
    // note, release, retrigger mid-release
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(500);
    env.NoteOff();
    RenderMs(100);
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(1500);
  } else if (strcmp(scenario, "chiff_then_off") == 0) {
    // A chiff note, then a note with AMOUNT 0 on the same envelope. The only
    // path on which chiff state -- the walked slew time above all -- can enter
    // a note that has no chiff. Every other case either has a chiff throughout
    // or has none at all, so nothing else can catch state carried across.
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(300);
    env.NoteOff();
    RenderMs(100);
    env.NoteOn(adsr, 0, 16383, 0, chiff_audible_samples);
    RenderMs(1500);
  } else if (strcmp(scenario, "inverted") == 0) {
    // Numerically inverted range (CV DAC / negative timbre): min > max
    env.NoteOn(adsr, 16383, 0, amount, chiff_audible_samples);
    RenderMs(2000);
    env.NoteOff();
    RenderMs(1000);
  } else if (strcmp(scenario, "latehang") == 0) {
    // Early release near the dark end of a long window: 100ms release at 7s
    // into an 8s chiff. Must fall with the release, not hang.
    adsr.attack_u32 = IncFromSamples(200 * 45);
    adsr.decay_u32 = IncFromSamples(200 * 45);
    adsr.release_u32 = IncFromSamples(100 * 45);
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(7000);
    env.NoteOff();
    RenderMs(400);
  } else if (strcmp(scenario, "held") == 0) {
    // long hold: chiff through attack into sustain
    env.NoteOn(adsr, 0, 16383, amount, chiff_audible_samples);
    RenderMs(9000);
    env.NoteOff();
    RenderMs(600);
  }
  if (g_value_range) {
    // s16 terms, the domain the note's range is dialled in.
    printf("%d %d\n", g_value_min >> 15, g_value_max >> 15);
  }
  if (g_hash_mode) printf("%08x\n", g_hash);
  return 0;
}
