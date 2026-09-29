// Copyright 2013 Emilie Gillet.
// Copyright 2020 Chris Rogers.
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
// Voice.

#ifndef YARNS_VOICE_H_
#define YARNS_VOICE_H_

#include "stmlib/stmlib.h"

#include "yarns/envelope.h"
#include "yarns/oscillator.h"
#include "yarns/interpolator.h"
#include "yarns/synced_lfo.h"
#include "yarns/part.h"
#include "yarns/utils.h"

namespace yarns {

const uint16_t kNumOctaves = 11;

enum OscillatorMode {
  OSCILLATOR_MODE_OFF,
  OSCILLATOR_MODE_DRONE,
  OSCILLATOR_MODE_ENVELOPED,

  OSCILLATOR_MODE_LAST
};

enum ModAux {
  MOD_AUX_VELOCITY,
  MOD_AUX_MODULATION,
  MOD_AUX_AFTERTOUCH,
  MOD_AUX_BREATH,
  MOD_AUX_PEDAL,
  MOD_AUX_BEND,
  MOD_AUX_VIBRATO_LFO,
  MOD_AUX_FULL_LFO,
  MOD_AUX_ENVELOPE,
  MOD_AUX_PITCH_1,
  MOD_AUX_PITCH_2,
  MOD_AUX_PITCH_3,
  MOD_AUX_PITCH_4,
  MOD_AUX_PITCH_5,
  MOD_AUX_PITCH_6,
  MOD_AUX_PITCH_7,

  MOD_AUX_LAST
};

// envelope_, reachable only on the is_envelope() path.
const uint8_t kEnvelopesPerCVOutput = 1;

// The most audio voices any layout sounds at once, which is what a CPU budget
// multiplies a shape's per-sample cost by. multi.h folds the layout map and
// static-asserts this equals it; multi.h cannot declare it because envelope.h
// and the tools both need it without pulling multi.h in.
const uint8_t kMaxAudioVoices = 6;

// A role used by a CV output when it is not acting as an audio oscillator
enum DCRole {
  DC_PITCH,
  DC_VELOCITY,
  DC_AUX_1,
  DC_AUX_2,
  DC_LAST
};

enum LFORole {
  LFO_ROLE_PITCH,
  LFO_ROLE_TIMBRE,
  LFO_ROLE_AMPLITUDE,
  LFO_ROLE_LAST
};

class CVOutput;

class Voice {
 public:
  Voice() { }
  ~Voice() { }

  void Init();
  void ResetAllControllers();

  void Refresh();
  void NoteOn(
    int16_t note, uint8_t velocity, uint8_t portamento,
    int8_t portamento_mod_velocity, bool trigger,
    ADSR& adsr, int16_t timbre_envelope_target,
    uint32_t chiff_amount_q30, uint32_t chiff_audible_samples
  );
  void NoteOff(bool force = false);
  void ControlChange(uint8_t controller, uint8_t value);
  void PitchBend(uint16_t pitch_bend) {
    mod_pitch_bend_ = pitch_bend;
  }
  void Aftertouch(uint8_t velocity) {
    mod_aux_[MOD_AUX_AFTERTOUCH] = velocity << 9;
  }

  inline void set_pitch_bend_range(uint8_t pitch_bend_range) {
    pitch_bend_range_ = pitch_bend_range;
  }
  inline void set_vibrato_range(uint8_t vibrato_range) {
    vibrato_range_ = vibrato_range;
  }
  inline void set_vibrato_mod(uint8_t n) { vibrato_mod_ = n; }
  inline void set_tremolo_mod(uint8_t n) {
    tremolo_mod_target_ = n << (16 - 7); }

  inline void set_lfo_shape(LFORole role, uint8_t shape) {
    lfo_shapes_[role] = static_cast<LFOShape>(shape);
  }
  inline int16_t lfo_value(LFORole role) const {
    return lfos_[role].shape(lfo_shapes_[role]);
  }

  inline void set_aux_cv(uint8_t i) { aux_cv_source_ = i; }
  inline void set_aux_cv_2(uint8_t i) { aux_cv_source_2_ = i; }
  
  inline int32_t note() const { return note_; }
  inline uint8_t velocity() const { return mod_velocity_; }
  inline uint16_t aux_cv_16bit() const { return mod_aux_[aux_cv_source_]; }
  inline uint16_t aux_cv_2_16bit() const { return mod_aux_[aux_cv_source_2_]; }
  inline uint8_t aux_cv() const { return aux_cv_16bit() >> 8; }
  inline uint8_t aux_cv_2() const { return aux_cv_2_16bit() >> 8; }
  
  inline bool gate_on() const { return gate_; }
  inline void set_highest_priority(bool v) { is_highest_priority_ = v; }

  inline bool gate() const { return gate_ && !retrigger_delay_; }
  inline bool trigger() const  {
    return gate_ && trigger_pulse_;
  }
  
  inline void set_oscillator_mode(uint8_t m) {
    oscillator_mode_ = m;
  }
  inline void set_oscillator_shape(uint8_t s) {
    oscillator_.set_shape(static_cast<OscillatorShape>(s));
  }
  inline void set_timbre_init(uint8_t n) {
    timbre_init_target_ = n << (16 - 7); }
  inline void set_timbre_mod_lfo(uint8_t n) {
    timbre_mod_lfo_target_ = UINT16_MAX - lut_env_expo_u16[((127 - n) << 1)];
  }
  
  inline void set_tuning(int8_t coarse, int8_t fine) {
    tuning_ = (static_cast<int32_t>(coarse) << 7) + fine;
  }
  
  inline ModAux aux_1_source() const {
    return static_cast<ModAux>(aux_cv_source_);
  }
  inline ModAux aux_2_source() const {
    return static_cast<ModAux>(aux_cv_source_2_);
  }

  inline bool aux_1_envelope() const {
    return aux_cv_source_ == MOD_AUX_ENVELOPE && dc_output(DC_AUX_1);
  }
  inline bool aux_2_envelope() const {
    return aux_cv_source_2_ == MOD_AUX_ENVELOPE && dc_output(DC_AUX_2);
  }
  inline void set_dc_output(DCRole r, CVOutput* cvo) { dc_outputs_[r] = cvo; }
  inline CVOutput* dc_output(DCRole r) const { return dc_outputs_[r]; }
  inline void set_audio_output(CVOutput* cvo) { audio_output_ = cvo; }
  inline bool uses_audio() const {
    return audio_output_ && oscillator_mode_ != OSCILLATOR_MODE_OFF;
  }
  // Is this a gate-only part?
  inline bool has_cv_output() const {
    if (uses_audio()) return true;
    for (uint8_t i = 0; i < DC_LAST; ++i) {
      if (dc_outputs_[static_cast<DCRole>(i)]) return true;
    }
    return false;
  }

  inline Oscillator* oscillator() {
    return &oscillator_;
  }
  inline FastSyncedLFO* lfo(LFORole l) { return &lfos_[l]; }
  
 private:
  // Assemble the oscillator pitch from a base note exactly as Refresh does:
  // pitch bend + tuning/transpose + pitch LFO. Shared so NoteOn can prime the
  // oscillator against the same pitch the next Refresh/Render will warp
  // against, keeping the timbre-bias bump consistent (a held bend or active
  // vibrato would otherwise reappear as a per-note chirp).
  inline int32_t ApplyPitchMods(int32_t note) const {
    note += PitchBend64ths() >> 6;
    note += tuning_;
    note += VibratoPitch_q15_16() >> 16;
    return note;
  }

  // In 64ths of a pitch unit, which is the precision the range multiply earns
  // and the shift above throws away.
  inline int32_t PitchBend64ths() const {
    return static_cast<int32_t>(mod_pitch_bend_ - 8192) * pitch_bend_range_;
  }

  // The vibrato's pitch offset, scaled straight off the interpolator that
  // already carries the LFO to sixteen fractional bits. A second interpolator
  // targeting WHOLE pitch units used to stand here, and at VB=10 its target had
  // ten values against that one's 13778: the pitch sat on one of them for half a
  // second at a slow LFO rate and then jumped, which is the whole of the stepping
  // the CZ shapes make audible.
  //
  // Split either side of the range multiply so the product stays in int32: the
  // interpolator reaches +-16256 whole units and the range reaches 12.
  inline int32_t VibratoPitch_q15_16() const {
    return (scaled_vibrato_lfo_interpolator_.value_q15_16() >> 4)
        * vibrato_range_ >> 4;
  }

  // What ApplyPitchMods leaves below a whole pitch unit. Under two, since
  // tuning contributes none -- it is whole units already.
  inline uint32_t PitchModsRemainder_u1_16() const {
    return ((PitchBend64ths() & 63) << 10)
        + (VibratoPitch_q15_16() & 0xffff);
  }

  // Narrowest first, each width filling whole words, so every scalar sits
  // inside the reach of Thumb's short loads; the embedded objects follow,
  // smallest first.
  bool gate_;
  // Sets whether this voice can control a paraphonic CV envelope's tremolo
  bool is_highest_priority_;
  bool portamento_exponential_shape_;
  uint8_t mod_velocity_;
  uint8_t pitch_bend_range_;
  uint8_t vibrato_range_;
  uint8_t vibrato_mod_;
  uint8_t oscillator_mode_;
  uint8_t aux_cv_source_;
  uint8_t aux_cv_source_2_;
  uint8_t refresh_counter_;
  LFOShape lfo_shapes_[LFO_ROLE_LAST];

  int16_t mod_pitch_bend_;
  // This counter is used to artificially create a 750µs (3-systick) dip at LOW
  // level when the gate is currently HIGH and a new note arrive with a
  // retrigger command. This happens with note-stealing; or when sending a MIDI
  // sequence with overlapping notes.
  uint16_t retrigger_delay_;
  uint16_t trigger_pulse_;
  uint16_t tremolo_mod_target_;
  uint16_t tremolo_mod_current_;
  uint16_t timbre_mod_lfo_target_;
  uint16_t timbre_mod_lfo_current_;
  uint16_t timbre_init_target_;
  uint16_t timbre_init_current_;
  uint16_t mod_aux_[MOD_AUX_LAST];

  int32_t note_source_;
  int32_t note_target_;
  int32_t note_portamento_;
  int32_t note_;
  int32_t tuning_;
  uint32_t portamento_phase_;
  uint32_t portamento_phase_increment_;
  CVOutput* audio_output_;
  CVOutput* dc_outputs_[DC_LAST];

  ADSR adsr_;
  Interpolator<kRefreshHzToLfoSampleHzRatioBits> timbre_lfo_interpolator_, amplitude_lfo_interpolator_, scaled_vibrato_lfo_interpolator_;
  FastSyncedLFO lfos_[LFO_ROLE_LAST];
  Oscillator oscillator_;

  DISALLOW_COPY_AND_ASSIGN(Voice);
};

class CVOutput {
 public:
  CVOutput() { }
  ~CVOutput() { }

  typedef uint16_t (CVOutput::*DCFn)();
  static const DCFn dc_fn_table_[];

  void Init(bool reset_calibration);

  void Calibrate(uint16_t* calibrated_dac_code);

  // NB: a voice can supply DC to many CV outputs, but audio to only one output
  inline void AssignVoices(Voice* dc, DCRole dc_role, uint8_t num_dc, uint8_t num_audio) {
    num_dc_voices_ = num_dc;
    dc_role_ = dc_role;
    for (uint8_t i = 0; i < num_dc; ++i) {
      dc_voices_[i] = dc + i;
      dc_voices_[i]->set_dc_output(dc_role, this);
    }

    num_audio_voices_ = num_audio;
    zero_dac_code_ = volts_dac_code(0);
    envelope_.Init(zero_dac_code_ >> 1);
    // 10 Vpp, +/-5 V about the 0 V code, which is what a Eurorack audio output
    // is expected to swing. Named as twice the 5 V span because the calibration
    // table stops at -3 V and cannot be asked for -5 V directly.
    //
    // AND THAT IS ALL THE CODE THERE IS. 0 V is code 39187 with 5133 codes per
    // volt, so -5 V is code 64852 of 65535: 683 codes, 0.13 V, before the code
    // WRAPS and the output jumps to the opposite rail. The mix accumulator is
    // an int16 holding that code and cannot carry an excursion past it, so
    // nothing downstream may exceed its share of this span.
    const uint16_t span_pp_codes_u16 =
        (volts_dac_code(0) - volts_dac_code(5)) * 2;
    // Halved here, where the span stops being one: everything past this point
    // is an amplitude, so nothing downstream has to know the difference.
    const uint16_t scale_codes_u16 = span_pp_codes_u16 >> 1;
    // WHAT ONE VOICE MAY SPEND, so that the voices summed reach the span's
    // amplitude. An nth each: periodic voices line their peaks up sooner or
    // later, so n of them reach n times one.
    //
    // A shape whose voices are UNCORRELATED adds in power instead, reaching
    // only sqrt(n) times one, and an nth leaves it 6 dB under at four voices.
    // The geometric mean of the whole and the nth is what it may spend --
    // full/sqrt(n) -- and it must then cap its own peak at the nth, since n
    // peaks that do align would otherwise leave the span.
    //
    // Only WHISTLE takes it, and CREST is why rather than correlation: the
    // trade is peak headroom for level, so it pays only where the signal
    // visits its peak rarely. WHISTLE's crest is 3.8 to 5.3. The four NOISE
    // shapes are uncorrelated too and do lose the same 6 dB, but they drive
    // full-scale noise into a limiter and come out at crest 1.35 -- MEASURED,
    // capping them at the nth while driving to full/sqrt(n) returns 1.69 dB of
    // the 6.02 and clips 70% of samples to do it. There is no headroom there
    // to trade.
    const uint16_t coherent_scale_codes_u16 =
        scale_codes_u16 / num_audio_voices_;
    const uint16_t incoherent_scale_codes_u16 = static_cast<uint16_t>(
        IntegerSqrt(static_cast<uint32_t>(scale_codes_u16)
                    * coherent_scale_codes_u16));
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      Voice* audio_voice = audio_voices_[i] = dc_voices_[0] + i;
      audio_voice->oscillator()->Init(coherent_scale_codes_u16, incoherent_scale_codes_u16);
      audio_voice->set_audio_output(this);
    }
  }

  inline bool gate() const {
    if (!is_audio()) return dc_voices_[0]->gate();
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      if (audio_voices_[i]->gate()) return true;
    }
    return false;
  }
  inline bool trigger() const {
    if (!is_audio()) return dc_voices_[0]->trigger();
    for (uint8_t i = 0; i < num_audio_voices_; ++i) {
      if (audio_voices_[i]->trigger()) return true;
    }
    return false;
  }

  inline bool is_high_freq() const { return is_audio() || is_envelope(); }
  inline bool is_audio() const {
    return num_audio_voices_ > 0 && audio_voices_[0]->uses_audio();
  }
  inline bool is_envelope() const {
    return !is_audio() && (
      (dc_role_ == DC_AUX_1 && dc_voices_[0]->aux_1_envelope()) ||
      (dc_role_ == DC_AUX_2 && dc_voices_[0]->aux_2_envelope())
    );
  }
  inline bool sounding() const {
    return envelope_.stage() != ENV_STAGE_DEAD;
  }
  inline void NoteOn(
      ADSR& adsr, uint32_t chiff_amount_q30, uint32_t chiff_audible_samples) {
    // The range runs DOWNWARD -- DAC codes fall as volts rise -- and the
    // output range is the only bound: this envelope drives one CV output on its
    // own, so there is nothing for it to share with.
    envelope_.NoteOn(
      adsr, volts_dac_code(0) >> 1, volts_dac_code(7) >> 1, kEnvelopeSampleMax,
      chiff_amount_q30, chiff_audible_samples);
  }
  inline void NoteOff(bool force = false) {
    if (!force) {
      for (uint8_t i = 0; i < num_dc_voices_; ++i) {
        if (dc_voices_[i]->gate_on()) return;
      }
    }
    envelope_.NoteOff();
  }

  // When paraphonic (num_dc_voices_ > 1), only the highest-priority voice
  // controls the shared envelope's tremolo.
  uint16_t RefreshEnvelope(uint16_t tremolo, bool voice_is_highest_priority) {
    if (voice_is_highest_priority || num_dc_voices_ <= 1) {
      envelope_bias_ = envelope_.tremolo(tremolo);
    }
    return volts_dac_code(0) - envelope_value();
  }
  inline uint16_t envelope_value() {
    int32_t value = (envelope_bias_ + envelope_.value_without_bias()) << 1;
    CONSTRAIN(value, 0, UINT16_MAX);
    return value;
   }

  void RenderSamples(uint8_t block, uint8_t channel, uint16_t default_low_freq_cv);

  void Refresh();

  inline uint16_t dc_dac_code() const { return dac_code_; }

  inline uint16_t DacCodeFrom16BitValue(uint16_t value) const {
    uint32_t v = static_cast<uint32_t>(value);
    uint16_t scale = volts_dac_code(0) - volts_dac_code(7);
    return static_cast<uint16_t>(volts_dac_code(0) - (scale * v >> 16));
  }

  uint16_t pitch_dac_code();
  inline uint16_t velocity_dac_code() {
    return DacCodeFrom16BitValue(dc_voices_[0]->velocity() << 9);
  }
  inline uint16_t aux_cv_dac_code() {
    if (dc_voices_[0]->aux_1_source() >= MOD_AUX_PITCH_1) {
      return NoteToDacCode(
        dc_voices_[0]->note() +
        lut_fm_modulator_intervals[dc_voices_[0]->aux_1_source() - MOD_AUX_PITCH_1]
      );
    }
    return DacCodeFrom16BitValue(dc_voices_[0]->aux_cv_16bit());
  }
  inline uint16_t aux_cv_dac_code_2() {
    if (dc_voices_[0]->aux_2_source() >= MOD_AUX_PITCH_1) {
      return NoteToDacCode(
        dc_voices_[0]->note() +
        lut_fm_modulator_intervals[dc_voices_[0]->aux_2_source() - MOD_AUX_PITCH_1]
      );
    }
    return DacCodeFrom16BitValue(dc_voices_[0]->aux_cv_2_16bit());
  }
  inline uint16_t calibration_dac_code(uint8_t note) const {
    return calibrated_dac_code_[note];
  }

  inline void set_calibration_dac_code(uint8_t note, uint16_t dac_code) {
    calibrated_dac_code_[note] = dac_code;
    dirty_ = true;
  }

  inline uint16_t volts_dac_code(int8_t volts) const {
    return calibration_dac_code(volts + 3);
  }

 private:
  uint16_t NoteToDacCode(int32_t note) const;

  // Narrowest first, each width filling whole words so nothing is left as a
  // hole: Thumb's short loads reach bytes only in the first 32 bytes, halfwords
  // in the first 64 and words in the first 128.
  uint8_t num_dc_voices_;
  uint8_t num_audio_voices_;
  DCRole dc_role_;
  bool dirty_;  // Set to true when the calibration settings have changed.

  uint16_t dac_code_;
  uint16_t zero_dac_code_;
  int16_t envelope_bias_;
  uint16_t calibrated_dac_code_[kNumOctaves];

  Voice* dc_voices_[kNumMaxVoicesPerPart];  // dc_voices_[0] is primary, others for paraphonic envelope
  Voice* audio_voices_[kNumMaxVoicesPerPart];
  int32_t note_;
  Envelope envelope_;

  DISALLOW_COPY_AND_ASSIGN(CVOutput);
};

}  // namespace yarns

#endif // YARNS_VOICE_H_
