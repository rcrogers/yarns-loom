// Part::VoiceNoteOn's parameter chain, off target.
//
// Every off-target consumer needs it and none of them can run part.cc: that TU
// wants the arpeggiator, the looper, the just-intonation processor, the MIDI
// handler and the `multi` global. So there is one copy here, and
// tools/cvtest/panel.js holds it to the original by driving the REAL
// Part::VoiceNoteOn on the host and comparing every parameter that reaches the
// envelope.
//
// Before this file there were THREE derivations of AMOUNT -- part.cc's, the
// sim's and the host driver's -- and no check could see it: simparity pins the
// page to the host driver, and those two happened to agree while both differed
// from the firmware by up to 7557 in Q30.
//
// So: change part.cc and panel.js goes red. That is the point of it.
#ifndef YARNS_TOOLS_PANEL_CHAIN_H_
#define YARNS_TOOLS_PANEL_CHAIN_H_

#include "stmlib/stmlib.h"
#include "stmlib/utils/dsp.h"

#include "yarns/envelope.h"
#include "yarns/resources.h"

#include <algorithm>

namespace yarns {

// One timed stage's setting -> phase increment. Only 128 stage lengths exist,
// which is why milliseconds cannot express what the module does.
inline uint32_t PanelStageIncrement(int setting, int mod, int velocity) {
  return Interpolate88(
      lut_envelope_phase_increments,
      modulate_7_13(static_cast<uint8_t>(setting), static_cast<int8_t>(mod),
                    static_cast<uint8_t>(velocity)) << (15 - 13));
}

inline uint16_t PanelSustain_u16(int setting, int mod, int velocity) {
  return static_cast<uint16_t>(
      modulate_7_13(static_cast<uint8_t>(setting), static_cast<int8_t>(mod),
                    static_cast<uint8_t>(velocity)) << (16 - 13));
}

// Velocity modulation is included because the peak is NOT a setting -- it falls
// out of AMPLITUDE MOD VELOCITY and the note's velocity.
inline void PanelAdsr(
    ADSR* adsr,
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
  // A zero peak lands the attack's target on the release level and the
  // envelope drops the stage; part.cc says which corner reaches it.
  const int32_t kMinPeak_u16 = 1;
  adsr->peak_u16 = static_cast<uint16_t>(
      std::max(kMinPeak_u16, UINT16_MAX - (damping_22 >> (22 - 16))));
  adsr->sustain_u16 = PanelSustain_u16(sustain_setting, env_mod_sustain, vel);
  adsr->attack_u32 = PanelStageIncrement(attack_setting, env_mod_attack, vel);
  adsr->decay_u32 = PanelStageIncrement(decay_setting, env_mod_decay, vel);
  adsr->release_u32 = PanelStageIncrement(release_setting, env_mod_release, vel);
}

// EXCITER AMOUNT: modulate_7_13 resolves it six bits below the knob step, and
// the envelope takes the fraction of full scale. The reciprocal is CEILED, so
// a full setting reaches 1.0 and the clamp is what stops it going past.
inline uint32_t PanelChiffAmount_q30(
    int amount_setting, int amount_mod_velocity, int velocity) {
  // part.cc spells this ((1 << PackedPart::kTimbreBits) - 1) << 6, and part.h
  // is not reachable from here. The check is what holds the two equal.
  const uint16_t kChiffAmountFullScale_q7_6 = 127 << 6;
  const uint32_t kChiffAmountToFraction_q30 =
      ((1u << 30) + kChiffAmountFullScale_q7_6 - 1) / kChiffAmountFullScale_q7_6;
  uint32_t amount_q30 = modulate_7_13(
      static_cast<uint8_t>(amount_setting),
      static_cast<int8_t>(amount_mod_velocity),
      static_cast<uint8_t>(velocity)) * kChiffAmountToFraction_q30;
  if (amount_q30 > (1u << 30)) amount_q30 = 1u << 30;
  return amount_q30;
}

// EXCITER DURATION names a time on its own table; the envelope takes samples.
inline uint32_t PanelChiffAudibleSamples(
    int duration_setting, int duration_mod_velocity, int velocity) {
  return ChiffAudibleSamples(Interpolate88(
      lut_chiff_phase_increments,
      modulate_7_13(static_cast<uint8_t>(duration_setting),
                    static_cast<int8_t>(duration_mod_velocity),
                    static_cast<uint8_t>(velocity)) << (15 - 13)));
}

}  // namespace yarns

#endif  // YARNS_TOOLS_PANEL_CHAIN_H_
