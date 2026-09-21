// Copyright 2012 Emilie Gillet.
// Copyright 2021 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Oscillator.

#ifndef YARNS_OSCILLATOR_H_
#define YARNS_OSCILLATOR_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"
#include "stmlib/dsp/dsp.h"

#include "yarns/envelope.h"
#include "yarns/resources.h"
#include "yarns/utils.h"
#include "yarns/interpolator.h"
#include "yarns/svf.h"
#include "yarns/drivers/dac.h"

#include <cstring>
#include <cstdio>

namespace yarns {

static const uint16_t kHighestNote = 128 * 128;

// gain_envelope_ and timbre_envelope_.
const uint8_t kEnvelopesPerOscillator = 2;

class StateVariableFilter : public SVF {
 public:
  void Init();
  void RenderInitDamp(int16_t damp_u1_14);
  // For a shape whose cutoff comes from the PITCH rather than from the timbre
  // buffer. pitch_ moves once a block and the timbre buffer does not, so which
  // of the two needs interpolating is the other way round for such a shape.
  void RenderInitCutoff(int16_t cutoff_u15);

  // Cutoff per sample, damping interpolated from a resonance set once a block.
  // kKeepNotchAndHp is the caller's: only a shape that reads notch or hp pays
  // to store them.
  template<bool kKeepNotchAndHp>
  inline void RenderSample(int32_t in, int16_t cutoff_u15) {
    damp.Tick();
    ProcessInto<kKeepNotchAndHp, false>(in, cutoff_u15, damp.value());
  }
  // The mirror: damping per sample, cutoff interpolated toward the pitch's.
  // kMustReachSilence, because these shapes spend the gain envelope on the
  // excitation rather than on the output: a ring that stops short of zero is a
  // tone that never ends.
  template<bool kKeepNotchAndHp>
  inline void RenderSampleAtPitch(int32_t in, int16_t damp_u1_14) {
    cutoff.Tick();
    ProcessInto<kKeepNotchAndHp, true>(in, cutoff.value(), damp_u1_14);
  }

 private:
  Interpolator<kAudioBlockSizeBits> damp;
  Interpolator<kAudioBlockSizeBits> cutoff;
};

struct PhaseDistortionSquareModulator {
  int32_t integrator;
  bool polarity;
};

enum OscillatorShape {
  OSC_SHAPE_NOISE_NOTCH,
  OSC_SHAPE_NOISE_LP,
  OSC_SHAPE_NOISE_BP,
  OSC_SHAPE_NOISE_HP,
  OSC_SHAPE_WHISTLE,
  OSC_SHAPE_PING_NOTCH,
  OSC_SHAPE_PING_LP,
  OSC_SHAPE_PING_BP,
  OSC_SHAPE_PING_HP,
  OSC_SHAPE_LP_PULSE,
  OSC_SHAPE_LP_SAW,
  OSC_SHAPE_CZ_PULSE_LP,
  OSC_SHAPE_CZ_PULSE_PK,
  OSC_SHAPE_CZ_PULSE_BP,
  OSC_SHAPE_CZ_PULSE_HP,
  OSC_SHAPE_CZ_SAW_LP,
  OSC_SHAPE_CZ_SAW_PK,
  OSC_SHAPE_CZ_SAW_BP,
  OSC_SHAPE_CZ_SAW_HP,
  OSC_SHAPE_VARIABLE_SINE,
  OSC_SHAPE_VARIABLE_PULSE,
  OSC_SHAPE_VARIABLE_SAW,
  OSC_SHAPE_SAW_PULSE_MORPH,
  OSC_SHAPE_SYNC_SINE,
  // OSC_SHAPE_SYNC_TRIANGLE,
  OSC_SHAPE_SYNC_PULSE,
  OSC_SHAPE_SYNC_SAW,
  // OSC_SHAPE_FOLD_SINE,
  // OSC_SHAPE_FOLD_TRIANGLE,
  OSC_SHAPE_DIRAC_COMB,
  OSC_SHAPE_TANH_SINE,
  OSC_SHAPE_EXP_SINE,
  OSC_SHAPE_TRI_THRU_TRI,
  OSC_SHAPE_SINE_THRU_TRI,
  OSC_SHAPE_EXP_THRU_TRI,
  OSC_SHAPE_TRI_THRU_TRI_BIASED,
  OSC_SHAPE_SINE_THRU_TRI_BIASED,
  OSC_SHAPE_EXP_THRU_TRI_BIASED,
  OSC_SHAPE_TRI_THRU_SINE,
  OSC_SHAPE_SINE_THRU_SINE,
  OSC_SHAPE_EXP_THRU_SINE,
  OSC_SHAPE_TRI_THRU_SINE_BIASED,
  OSC_SHAPE_SINE_THRU_SINE_BIASED,
  OSC_SHAPE_EXP_THRU_SINE_BIASED,
  OSC_SHAPE_TRI_THRU_EXP,
  OSC_SHAPE_SINE_THRU_EXP,
  OSC_SHAPE_EXP_THRU_EXP,
  OSC_SHAPE_TRI_THRU_EXP_BIASED,
  OSC_SHAPE_SINE_THRU_EXP_BIASED,
  OSC_SHAPE_EXP_THRU_EXP_BIASED,
  OSC_SHAPE_FM,
};

class Oscillator {
 public:
  // Multiply-accumulates each sample (* gain >> 15) into audio_mix.
  typedef void (Oscillator::*RenderFn)(int16_t* input_samples, int16_t* audio_mix);

  Oscillator() { }
  ~Oscillator() { }

  inline void Init(uint16_t coherent_scale_codes_u16,
                   uint16_t incoherent_scale_codes_u16) {
    coherent_scale_codes_u16_ = coherent_scale_codes_u16;
    // The same scales as a fraction of the sample's own full scale, which is
    // what a gain multiplies by. The incoherent one is kept in this form only:
    // WHISTLE's level law is its one reader.
    coherent_scale_u15_ = static_cast<uint16_t>(
        (static_cast<uint32_t>(coherent_scale_codes_u16_) << 15) / INT16_MAX);
    incoherent_scale_u15_ = static_cast<uint16_t>(
        (static_cast<uint32_t>(incoherent_scale_codes_u16) << 15) / INT16_MAX);
    raw_gain_bias_ = raw_timbre_bias_ = 0;
    gain_envelope_.Init(0);
    timbre_envelope_.Init(0);
    svf_.Init();
    previous_damp_drive_u15_ = 0;
    // Its own stream, so that voices summed as independent noise are.
    noise_state_ = NextXorshift32Seed();
    pitch_ = 60 << 7;
    phase_ = 0;
    phase_increment_ = 1;
    // Every accumulator the render carries between blocks, not just the
    // carrier's: the modulator's phase and the phase-distortion square's
    // integrator survived Init and a re-Init inherited the old note's.
    modulator_phase_ = 0;
    pd_square_.integrator = 0;
    pd_square_.polarity = false;
    high_ = false;
    next_sample_ = 0;
    transfer_crest_factor_ = 1;
  }

  void Refresh(int16_t pitch, int16_t timbre_bias, uint16_t gain_bias) {
    Refresh(pitch, 0, timbre_bias, gain_bias);
  }
  // pitch_frac is a fraction of ONE pitch unit, all sixteen bits of it, which
  // is what the pitch LFO's interpolator already carries and value() drops.
  void Refresh(int16_t pitch, uint16_t pitch_frac,
               int16_t timbre_bias, uint16_t gain_bias);
  // Warps against the pitch given rather than the live carrier, for the
  // shapes whose map tracks pitch.
  int16_t WarpTimbre(int16_t timbre, OscillatorShape shape, int16_t pitch) const;
  int16_t WarpTimbre(int16_t timbre, OscillatorShape shape) const {
    return WarpTimbre(timbre, shape, pitch_);
  }
  int16_t WarpTimbre(int16_t timbre) const {
    return WarpTimbre(timbre, shape_);
  }

  // What a signed modulation of timbre is worth, warped. A warp is an
  // ABSOLUTE-POSITION map -- a filter cutoff, a phase increment -- so warping
  // a signed DELTA is meaningless: it asks where the position `delta` sits,
  // not how far `delta` moves you from where you are. Warp the DESTINATION and
  // difference it against the warped bias instead, which is the delta the map
  // actually implies and is signed correctly by construction.
  int16_t WarpTimbreDelta(
      int16_t bias, int16_t delta, OscillatorShape shape, int16_t pitch) const {
    int32_t destination = static_cast<int32_t>(bias) + delta;
    CONSTRAIN(destination, INT16_MIN, INT16_MAX);
    int32_t warped = WarpTimbre(static_cast<int16_t>(destination), shape, pitch)
      - WarpTimbre(bias, shape, pitch);
    CONSTRAIN(warped, INT16_MIN, INT16_MAX);
    return static_cast<int16_t>(warped);
  }

  void set_shape(OscillatorShape shape);

  // What the gain envelope runs at, which is not the same as what the shape is
  // worth. A shape that spends the gain BEFORE its filter drives an excitation
  // with it, so the envelope is a full-range drive and the share is applied to
  // the shape's output instead; every other shape spends it at the output, and
  // the envelope carries the share itself.
  //
  // One function because two callers need the same answer: NoteOn sets the
  // envelope to it, and set_shape rescales a held note between two of them.
  inline uint16_t gain_envelope_peak_codes_u16(OscillatorShape shape) const {
    const bool spends_gain_before_the_filter =
        shape >= OSC_SHAPE_WHISTLE && shape <= OSC_SHAPE_PING_HP;
    return spends_gain_before_the_filter
        ? kEnvelopeSampleMax : coherent_scale_codes_u16_;
  }

  // start_pitch is the new note's pitch at onset (the portamento glide's
  // start); target_pitch is its destination.
  inline void NoteOn(
      ADSR& adsr, bool drone,
      int16_t start_pitch, int16_t target_pitch, int16_t raw_max_timbre,
      uint32_t chiff_amount_q30, uint32_t chiff_audible_samples) {
    const uint16_t peak = gain_envelope_peak_codes_u16(shape_);
    gain_envelope_.NoteOn(
      adsr, drone ? peak : 0, peak, peak,
      chiff_amount_q30, chiff_audible_samples);

    // Snap the pitch-driven jump in timbre bias, so warped timbre tracks the
    // new pitch instantly and only LFO bias motion stays smoothed. start_pitch
    // ~= old pitch_ when portamento glides, so the bump is ~0 then and the
    // glide is left to slew normally.
    int16_t old_warped_bias = WarpTimbre(raw_timbre_bias_, shape_);
    pitch_ = start_pitch;
    CONSTRAIN(pitch_, 0, kHighestNote - 1);
    // Prime the carrier so the first audio block renders at the new pitch.
    phase_increment_ = ComputePhaseIncrement(pitch_);
    int16_t new_warped_bias = WarpTimbre(raw_timbre_bias_, shape_);
    // Two int16 warped biases differ by up to +/-65534, and shifting that by 16
    // leaves int32 -- twice over for the sign. The bias is an int16-scale
    // quantity everywhere else it is written, so the step is one too.
    int32_t warped_step = new_warped_bias - old_warped_bias;
    CONSTRAIN(warped_step, INT16_MIN, INT16_MAX);
    timbre_envelope_.AdjustBias(
        static_cast<int32_t>(static_cast<uint32_t>(warped_step) << 16));

    // The envelope's warped target is frozen at the destination pitch
    // (steady-state correct). It can't track the glide cheaply, so the bias
    // above is where pitch tracking is made accurate; the envelope's transient
    // pitch dependence during a glide is accepted as-is.
    // Against the bias, not on its own: raw_max_timbre is TIMBRE MOD ENVELOPE
    // plus its velocity term, a SIGNED offset from where the timbre control
    // sits. Warping it alone lost the sign on 17 of the 42 shapes -- every
    // NOISE, CZ, LP and SYNC shape, whose warps run through a cutoff table or
    // a phase increment and cannot be negative -- so a negative setting could
    // not modulate downward at all, and NOISE and CZ were not even monotone.
    int16_t warped_max_timbre = WarpTimbreDelta(
        raw_timbre_bias_, raw_max_timbre, shape_, target_pitch);
    // No bound tighter than the output range: a warped timbre is not a level
    // and nothing downstream sums it.
    timbre_envelope_.NoteOn(adsr, 0, warped_max_timbre, kEnvelopeSampleMax,
                            chiff_amount_q30, chiff_audible_samples);
  }
  inline void NoteOff() {
    gain_envelope_.NoteOff();
    timbre_envelope_.NoteOff();
  }
  inline bool sounding() const {
    return gain_envelope_.stage() != ENV_STAGE_DEAD;
  }

  void Render(int16_t* audio_mix);

  static RenderFn fn_table_[];
  
 private:
  void RenderFilteredNoise(int16_t* input_samples, int16_t* audio_mix);
  void RenderPhaseDistortionPulse(int16_t* input_samples, int16_t* audio_mix);
  void RenderPhaseDistortionSaw(int16_t* input_samples, int16_t* audio_mix);
  void RenderLPPulse(int16_t* input_samples, int16_t* audio_mix);
  void RenderLPSaw(int16_t* input_samples, int16_t* audio_mix);
  void RenderVariableSine(int16_t* input_samples, int16_t* audio_mix);
  void RenderVariablePulse(int16_t* input_samples, int16_t* audio_mix);
  void RenderVariableSaw(int16_t* input_samples, int16_t* audio_mix);
  void RenderSawPulseMorph(int16_t* input_samples, int16_t* audio_mix);
  void RenderSyncSine(int16_t* input_samples, int16_t* audio_mix);
  void RenderSyncTriangle(int16_t* input_samples, int16_t* audio_mix);
  void RenderSyncPulse(int16_t* input_samples, int16_t* audio_mix);
  void RenderSyncSaw(int16_t* input_samples, int16_t* audio_mix);
  void RenderWhistle(int16_t* input_samples, int16_t* audio_mix);
  void RenderPing(int16_t* input_samples, int16_t* audio_mix);
  // void RenderFoldSine(int16_t* input_samples, int16_t* audio_mix);
  // void RenderFoldTriangle(int16_t* input_samples, int16_t* audio_mix);
  void RenderDiracComb(int16_t* input_samples, int16_t* audio_mix);
  void RenderTanhSine(int16_t* input_samples, int16_t* audio_mix);
  void RenderExponentialSine(int16_t* input_samples, int16_t* audio_mix);
  void RenderTransfer(int16_t* input_samples, int16_t* audio_mix);
  void RenderFM(int16_t* input_samples, int16_t* audio_mix);
  
  uint32_t ComputePhaseIncrement(int16_t midi_pitch) const;
  
  inline int32_t ThisBlepSample(uint32_t t) const {
    if (t > 65535) {
      t = 65535;
    }
    return t * t >> 18;
  }
  
  inline int32_t NextBlepSample(uint32_t t) const {
    if (t > 65535) {
      t = 65535;
    }
    t = 65535 - t;
    return -static_cast<int32_t>(t * t >> 18);
  }

  // Quarter-table lookup with quadrant symmetry for 4x effective resolution.
  // Table must be uint16_t[257] mapping 0..1 quadrant to 0..65535.
  inline int16_t quadrant_lookup(const uint16_t* table, uint32_t phase) const {
    uint32_t quarter_phase = phase << 2;
    // Mirror for quadrants 1 and 3 (bit 30 set)
    quarter_phase ^= -((phase >> 30) & 1);
    uint16_t value = Interpolate824(table, quarter_phase);
    // Negate for quadrants 2 and 3 (bit 31 set)
    int32_t sign = static_cast<int32_t>(phase) >> 31;
    return static_cast<int16_t>(((value ^ sign) - sign) >> 1);
  }

  inline int16_t sine(uint32_t phase) const {
    return quadrant_lookup(lut_sine_quadrant_u16, phase);
  }

  inline int16_t triangle(uint32_t phase) const {
    // Phase offset ensures f(0) = 0, with peak at phase 1/4 (like sine).
    // This simplifies transfer waveshaping: input 0 always yields output 0.
    phase += 0x40000000;
    return ((phase >> 15) ^ (phase >> 31 ? 0xffff : 0x0000)) - 0x8000;
  }

  OscillatorShape shape_;
  Envelope gain_envelope_, timbre_envelope_;
  int16_t raw_timbre_bias_;
  uint16_t raw_gain_bias_;
  int16_t pitch_;

  // Calculated from shape, cached to avoid conditionals during render
  uint8_t transfer_carrier_;
  uint8_t transfer_function_;
  uint32_t transfer_bias_;
  uint8_t transfer_crest_factor_;
  uint8_t transfer_gain_shift_;

  uint32_t phase_;
  uint32_t phase_increment_;
  uint32_t modulator_phase_;
  bool high_;

  StateVariableFilter svf_;
  PhaseDistortionSquareModulator pd_square_;
  
  int32_t next_sample_;
  // The drive the filter state was last normalised by, so a drive that moves
  // can rescale what the filter still holds.
  int32_t previous_damp_drive_u15_;
  uint32_t noise_state_;
  uint16_t coherent_scale_codes_u16_;
  uint16_t coherent_scale_u15_;
  uint16_t incoherent_scale_u15_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Oscillator);
};

}  // namespace yarns

#endif // YARNS_OSCILLATOR_H_
