// Emscripten entry point for chiff_sim.html.
//
// This is the ONLY DSP the sim runs: it includes the real yarns/envelope.cc
// (via tools/portable_envelope.py, which swaps nothing but the one ARM smull)
// and reproduces Part::VoiceNoteOn's parameter chain exactly. The sim has no
// model of its own to drift from the firmware.
//
// Every control is a front-panel SETTING, not a time. Milliseconds are an
// OUTPUT (reported in the meta block), never an input.
#define TEST 1
// Pin the envelope's PRNG seed below: Init hands out a fresh seed per call, so
// without this the same parameters render differently every time and nothing
// is reproducible.
#define private public

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include "yarns/drivers/dac.h"

// Same translation unit as the envelope so its PRNG can be seeded
// for reproducible renders (the sim's re-roll button).
#include "envelope_portable.cc"

#include <emscripten/emscripten.h>
#include <algorithm>
#include <cstring>

using namespace yarns;
using namespace stmlib;

namespace {

// Part::VoiceNoteOn, verbatim in structure. Velocity modulation is included
// because peak level is NOT a setting -- it falls out of AMPLITUDE MOD
// VELOCITY and the note's velocity.
void BuildAdsr(ADSR* adsr,
               int attack_setting, int decay_setting,
               int sustain_setting, int release_setting,
               int amplitude_mod_velocity, int velocity,
               int env_mod_attack, int env_mod_decay,
               int env_mod_sustain, int env_mod_release) {
  uint8_t vel = static_cast<uint8_t>(velocity);
  uint16_t vel_concave_up = UINT16_MAX - lut_env_expo[((127 - vel) << 1)];
  int32_t damping_22 = -amplitude_mod_velocity * vel_concave_up;
  if (amplitude_mod_velocity >= 0) {
    damping_22 += amplitude_mod_velocity << 16;
  }
  // Mirrors part.cc: floor the peak so a zero peak never collapses the attack
  // stage (see the comment there for the AMPLITUDE MOD VELOCITY -64 corner).
  const int32_t kMinPeak_u16 = 1;
  adsr->peak_u16 =
      std::max(kMinPeak_u16, UINT16_MAX - (damping_22 >> (22 - 16)));
  adsr->sustain_u16 = modulate_7_13(
      static_cast<uint8_t>(sustain_setting),
      static_cast<int8_t>(env_mod_sustain), vel) << (16 - 13);
  adsr->attack_u32 = Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(attack_setting),
                    static_cast<int8_t>(env_mod_attack), vel) << (15 - 13));
  adsr->decay_u32 = Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(decay_setting),
                    static_cast<int8_t>(env_mod_decay), vel) << (15 - 13));
  adsr->release_u32 = Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(release_setting),
                    static_cast<int8_t>(env_mod_release), vel) << (15 - 13));
}

Envelope envelope;
ADSR adsr;

}  // namespace

extern "C" {

// meta[] layout, all in SAMPLES except where noted. The sim converts to ms
// for display only.
enum MetaField {
  META_TOTAL_SAMPLES,
  META_GATE_SAMPLES,
  META_CHIFF_WINDOW_SAMPLES,
  META_ATTACK_SAMPLES,
  META_DECAY_SAMPLES,
  META_RELEASE_SAMPLES,
  META_PEAK_U16,
  META_SUSTAIN_U16,
  // The amount actually handed to NoteOn, i.e. after EXCITER AMT VEL MOD.
  META_CHIFF_AMOUNT,
  // The chiff's actual rails, straight from the envelope. Recomputing these
  // in JS was off by one: scale_s16 * peak_u16 falls just short of 2^31, so
  // the real ceiling is 32766, not INT16_MAX.
  META_CEILING,
  META_FLOOR,
  META_COUNT
};

EMSCRIPTEN_KEEPALIVE
int chiff_meta_count() { return META_COUNT; }

// Renders one note and returns the sample count written to `out`.
// Levels span 0..INT16_MAX; `out` must hold max_samples int16s.
EMSCRIPTEN_KEEPALIVE
int chiff_render(
    int attack_setting, int decay_setting,
    int sustain_setting, int release_setting,
    int amplitude_mod_velocity, int velocity,
    int env_mod_attack, int env_mod_decay,
    int env_mod_sustain, int env_mod_release,
    int chiff_amount, int chiff_duration, int chiff_amount_mod_velocity,
    int gate_samples, int tail_samples, int max_target,
    // The note's OTHER bound. Together with max_target these are NoteOn's two
    // rails, so the sim can render the ranges the firmware actually asks for:
    // inverted (min > max, a CV DAC where codes fall as volts rise) and
    // NEGATIVE (min 0, max < 0 -- a negative TIMBRE MOD ENV, whose warped
    // target is below zero). Default 0 is the ordinary 0..max note.
    int min_target,
    // BIAS, driven exactly as tools/hosttest/driver.cc drives it, so the sim
    // and the host battery agree. Both default to 0, which is bias-free and
    // reproduces every earlier render bit-for-bit.
    //   tremolo   -- target sampled per block from the envelope's own value,
    //                the way Oscillator::Render does it. Negative feedback
    //                scaled to the envelope, so the sum stays near range and
    //                the clip path is never exercised.
    //   bias_lfo  -- an INDEPENDENT bias, the way a timbre LFO drives it, in
    //                s16. This is the one that can push envelope + bias out of
    //                the DAC range and make the saturate bite. Sign flips
    //                every bias_lfo_blocks blocks so the ramp is always live.
    int tremolo, int bias_lfo, int bias_lfo_blocks,
    unsigned int seed,
    int16_t* out, int max_samples, int32_t* meta) {
  BuildAdsr(&adsr, attack_setting, decay_setting, sustain_setting,
            release_setting, amplitude_mod_velocity, velocity,
            env_mod_attack, env_mod_decay, env_mod_sustain, env_mod_release);

  next_chiff_seed = seed ? seed : 0xCAFEBABEu;

  // EXCITER AMT VEL MOD, mirroring Part::VoiceNoteOn: modulate_7_13 works in
  // 13 bits, so shift back to the 7-bit 0..127 the amount is.
  uint8_t modulated_chiff_amount = modulate_7_13(
      static_cast<uint8_t>(chiff_amount),
      static_cast<int8_t>(chiff_amount_mod_velocity),
      static_cast<uint8_t>(velocity)) >> 6;

  // THE SPAN MUST FIT int16. NoteOn forms `int16_t scale_s16 = max - min`, so a
  // span outside [-32768, 32767] wraps and then min_target_q31 + scale * peak
  // overflows int32 on top of it. No firmware caller can ask for that -- timbre
  // is 0..warped with warped in int16, and the DC and gain pairs are both
  // non-negative and under 32767 -- but the sim exposes the two rails as free
  // controls, so it can. Clamped here rather than only in the UI so no caller,
  // including the check scripts, can drive the engine into that.
  if (max_target - min_target > INT16_MAX) max_target = min_target + INT16_MAX;
  if (max_target - min_target < INT16_MIN) max_target = min_target + INT16_MIN;

  // Rest level = the note's own min, so an inverted or negative range starts
  // where it ends rather than at a zero that is outside it.
  envelope.Init(static_cast<int16_t>(min_target));
  // NOT re-seeded here: next_chiff_seed is set above, BEFORE Init, and Init
  // takes its stride off that. Overriding afterwards gave the page a different
  // stream from the native harness, which is exactly what simparity exists to
  // catch.
  // CHIFF DURATION names a time on its own table; NoteOn takes the increment,
  // not the setting. Converted once, because both are integers and passing the
  // wrong one converts silently.
  const uint32_t chiff_increment = Interpolate88(
      lut_chiff_phase_increments,
      static_cast<uint16_t>(chiff_duration) << (15 - 7));
  envelope.NoteOn(adsr, min_target, max_target,
                  modulated_chiff_amount, chiff_increment);
  // The NOMINAL duration, for the sim's marker. Computed once in NoteOn and a
  // sizing reference only -- nothing counts it down and nothing happens when
  // it elapses -- so reading it once here is the whole story.
  int32_t chiff_window_samples =
      static_cast<int32_t>(ChiffWindowSamples(chiff_increment));

  int total = gate_samples + tail_samples;
  if (total > max_samples) total = max_samples;

  int written = 0;
  bool released = false;
  int block_counter = 0;
  if (bias_lfo_blocks <= 0) bias_lfo_blocks = 8;
  int16_t block[kAudioBlockSize];
  while (written < total) {
    if (!released && written >= gate_samples) {
      envelope.NoteOff();
      released = true;
    }
    // Same construction as the host driver, so a scenario dialled here and a
    // scenario run there produce the same bias.
    int32_t bias_target_q31 = tremolo
        ? static_cast<int32_t>(envelope.tremolo(
            static_cast<uint16_t>(tremolo))) << 16
        : 0;
    if (bias_lfo) {
      const bool high = ((block_counter / bias_lfo_blocks) & 1) == 0;
      bias_target_q31 += (high ? bias_lfo : -bias_lfo) << 16;
    }
    ++block_counter;
    envelope.RenderSamples(block, bias_target_q31);
    int n = total - written;
    if (n > static_cast<int>(kAudioBlockSize)) n = kAudioBlockSize;
    memcpy(out + written, block, n * sizeof(int16_t));
    written += n;
  }

  meta[META_TOTAL_SAMPLES] = written;
  meta[META_GATE_SAMPLES] = gate_samples;
  meta[META_CHIFF_WINDOW_SAMPLES] = chiff_window_samples;
  meta[META_ATTACK_SAMPLES] =
      adsr.attack_u32 ? static_cast<int32_t>(UINT32_MAX / adsr.attack_u32) : 0;
  meta[META_DECAY_SAMPLES] =
      adsr.decay_u32 ? static_cast<int32_t>(UINT32_MAX / adsr.decay_u32) : 0;
  meta[META_RELEASE_SAMPLES] =
      adsr.release_u32 ? static_cast<int32_t>(UINT32_MAX / adsr.release_u32) : 0;
  meta[META_PEAK_U16] = adsr.peak_u16;
  meta[META_SUSTAIN_U16] = adsr.sustain_u16;
  meta[META_CHIFF_AMOUNT] = modulated_chiff_amount;
  // The Q30 rails in the rendered samples' units. SIGNED, not clipped to the
  // DAC range: a negative range's rails are genuinely below zero, and clipping
  // them reported 0 and drew the wrong line. Identical for any rail that is
  // already inside the range.
  // The note's ceiling, computed here rather than stored: the firmware never
  // read it, so it is no longer a member.
  meta[META_CEILING] = std::max(envelope.stage_target_q30_[ENV_STAGE_RELEASE],
      std::max(envelope.stage_target_q30_[ENV_STAGE_ATTACK],
               envelope.stage_target_q30_[ENV_STAGE_SUSTAIN])) >> 15;
  meta[META_FLOOR] = std::min(envelope.stage_target_q30_[ENV_STAGE_RELEASE],
      std::min(envelope.stage_target_q30_[ENV_STAGE_ATTACK],
               envelope.stage_target_q30_[ENV_STAGE_SUSTAIN])) >> 15;
  return written;
}

// The audio rate the firmware runs at, so the sim never hardcodes it.
EMSCRIPTEN_KEEPALIVE
int chiff_frame_hz() { return kFrameHz; }

// CHIFF DURATION setting -> window samples, through its own table and its own
// velocity modulation. Independent of the attack.
EMSCRIPTEN_KEEPALIVE
int chiff_duration_samples(int setting, int mod_velocity, int velocity) {
  return static_cast<int32_t>(ChiffWindowSamples(Interpolate88(
      lut_chiff_phase_increments,
      modulate_7_13(static_cast<uint8_t>(setting),
                    static_cast<int8_t>(mod_velocity),
                    static_cast<uint8_t>(velocity)) << (15 - 13))));
}

// ENV stage setting -> stage length in samples, via the real LUT chain.
EMSCRIPTEN_KEEPALIVE
int chiff_stage_samples(int setting) {
  uint32_t increment = Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(setting), 0, 0) << (15 - 13));
  return increment ? static_cast<int32_t>(UINT32_MAX / increment) : 0;
}

}  // extern "C"
