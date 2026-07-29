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
// Pin the round-robin PRNG offset below: Envelope::Init hands out a new offset
// per construction, so without this the same parameters render differently on
// every call and nothing is reproducible.
#define private public

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include "yarns/drivers/dac.h"

// Same translation unit as the envelope so the file-static PRNG can be seeded
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
    unsigned int seed,
    int16_t* out, int max_samples, int32_t* meta) {
  BuildAdsr(&adsr, attack_setting, decay_setting, sustain_setting,
            release_setting, amplitude_mod_velocity, velocity,
            env_mod_attack, env_mod_decay, env_mod_sustain, env_mod_release);

  shared_prng_state = seed ? seed : 0xCAFEBABEu;

  // EXCITER AMT VEL MOD, mirroring Part::VoiceNoteOn: modulate_7_13 works in
  // 13 bits, so shift back to the 7-bit 0..127 the amount is.
  uint8_t modulated_chiff_amount = modulate_7_13(
      static_cast<uint8_t>(chiff_amount),
      static_cast<int8_t>(chiff_amount_mod_velocity),
      static_cast<uint8_t>(velocity)) >> 6;

  envelope.Init(0);
  envelope.prng_offset_u32_ = 0;   // reproducible across calls
  envelope.NoteOn(adsr, 0, max_target,
                  modulated_chiff_amount,
                  static_cast<uint8_t>(chiff_duration));
  // The window is now attack-relative (computed in NoteOn); capture it before
  // the render loop below decrements it.
  // EXPERIMENT: report the TARGET duration, not the extended liveness count,
  // so the sim's "dialled duration" marker lands where the user dialled it.
  int32_t chiff_window_samples = static_cast<int32_t>(
      envelope.exp_target_samples_ ? envelope.exp_target_samples_
                                   : envelope.chiff_duration_samples_left_);

  int total = gate_samples + tail_samples;
  if (total > max_samples) total = max_samples;

  int written = 0;
  bool released = false;
  int16_t block[kAudioBlockSize];
  while (written < total) {
    if (!released && written >= gate_samples) {
      envelope.NoteOff();
      released = true;
    }
    Envelope::FillSharedPrngBuffer();
    envelope.RenderSamples(block, 0);
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
  // Convert the Q30 rails into the same units the rendered samples use, by
  // the same path EnvelopeSample takes (>>14 then the saturating >>1).
  meta[META_CEILING] = static_cast<int32_t>(
      ClipUShifted(envelope.chiff_top_q30_ >> (30 - 16), 15, 1));
  meta[META_FLOOR] = static_cast<int32_t>(
      ClipUShifted(envelope.chiff_floor_q30_ >> (30 - 16), 15, 1));
  return written;
}

// The audio rate the firmware runs at, so the sim never hardcodes it.
EMSCRIPTEN_KEEPALIVE
int chiff_frame_hz() { return kFrameHz; }

// CHIFF DURATION setting -> window samples. The window is now a multiple of the
// ATTACK duration, so this takes the attack settings too (the same chain
// BuildAdsr uses) -- there is no standalone duration table any more.
EMSCRIPTEN_KEEPALIVE
int chiff_duration_samples(int setting, int attack_setting, int env_mod_attack,
                           int velocity) {
  uint32_t attack_u32 = Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(attack_setting),
                    static_cast<int8_t>(env_mod_attack),
                    static_cast<uint8_t>(velocity)) << (15 - 13));
  return static_cast<int32_t>(
      ChiffWindowSamples(attack_u32, static_cast<uint8_t>(setting)));
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
