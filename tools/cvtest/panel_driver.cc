// Host driver for the PANEL CHAIN: Part::VoiceNoteOn, the real one.
//
// The sim cannot run it -- part.cc wants the arpeggiator, the looper, the
// just-intonation processor, the MIDI handler and the `multi` global, and the
// page carries its engine inline -- so tools/panel_chain.h holds
// a copy. This runs both and prints them side by side, which is what stops the
// copy drifting.
//
// The globals part.cc reaches for are defined here rather than linked:
// multi.cc and midi_handler.cc both include ui.h and its GPIO reads. Part only
// touches two fields of `multi`, so an empty one is enough.
//
// Usage: ./paneltest <amount_mod> <duration_mod> <env_mod> <velocity>
// One line per setting: what the firmware delivered, then what the mirror says.
#define TEST 1
#define private public
#include "yarns/part.h"
#include "yarns/voice.h"
#include "yarns/multi.h"
#include "yarns/midi_handler.h"
#include "tools/cvtest/dac_stub.h"
#include "tools/panel_chain.h"
#include <cstdio>
#include <cstdlib>

namespace stmlib { uint32_t Random::rng_state_ = 0x21; }
namespace yarns {
Multi multi;
MidiHandler midi_handler;
MidiHandler::MidiBuffer MidiHandler::output_buffer_;
MidiHandler::SmallMidiBuffer MidiHandler::high_priority_output_buffer_;
}

using namespace yarns;
using namespace stmlib;

namespace {

Part part;
Voice voice;
CVOutput audio_output, aux_output;

void Wire() {
  voice.Init();
  audio_output.Init(true);
  aux_output.Init(true);
  voice.set_aux_cv(MOD_AUX_ENVELOPE);
  voice.set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
  audio_output.AssignVoices(&voice, DC_PITCH, 1, 1);
  aux_output.AssignVoices(&voice, DC_AUX_1, 1, 0);
  part.Init();
  part.AllocateVoices(&voice, 1, false);
}

}  // namespace

int main(int argc, char** argv) {
  const int amount_mod = argc > 1 ? atoi(argv[1]) : 0;
  const int duration_mod = argc > 2 ? atoi(argv[2]) : 0;
  const int env_mod = argc > 3 ? atoi(argv[3]) : 0;
  const int velocity = argc > 4 ? atoi(argv[4]) : 100;

  Wire();
  // AMPLITUDE MOD VELOCITY drives the peak, so it is swept as its own axis
  // rather than pinned: -64 at velocity 127 is the corner that needs a floor.
  const int amplitude_mod_velocity = argc > 5 ? atoi(argv[5]) : 0;

  for (int setting = 0; setting <= 127; ++setting) {
    VoicingSettings& voicing = part.voicing_;
    voicing.chiff_amount = setting;
    voicing.chiff_amount_mod_velocity = amount_mod;
    voicing.chiff_duration = setting;
    voicing.chiff_duration_mod_velocity = duration_mod;
    voicing.env_init_attack = setting;
    voicing.env_init_decay = setting;
    voicing.env_init_sustain = setting;
    voicing.env_init_release = setting;
    voicing.env_mod_attack = env_mod;
    voicing.env_mod_decay = env_mod;
    voicing.env_mod_sustain = env_mod;
    voicing.env_mod_release = env_mod;
    voicing.amplitude_mod_velocity = amplitude_mod_velocity;
    // Held clear of the chiff, which reads the ADSR: TIMBRE MOD ENVELOPE
    // changes the note's rails, not what this compares.
    voicing.timbre_mod_envelope = 0;
    voicing.timbre_mod_velocity = 0;

    part.VoiceNoteOn(0, 60, static_cast<uint8_t>(velocity), false, true);

    // What the firmware actually delivered, read off the envelope it reached.
    const Envelope& envelope = aux_output.envelope_;
    const ADSR& delivered = *envelope.adsr_;

    ADSR mirrored;
    PanelAdsr(&mirrored, setting, setting, setting, setting,
                    amplitude_mod_velocity, velocity,
                    env_mod, env_mod, env_mod, env_mod);

    // DURATION reaches the envelope transformed, not stored, so the mirror's
    // is compared by EFFECT: a second envelope on the same rails, given the
    // mirror's parameters, must land in the same state as the one the
    // firmware drove.
    static Envelope reference;
    reference.Init(static_cast<int16_t>(aux_output.volts_dac_code(0) >> 1));
    reference.NoteOn(mirrored,
                     aux_output.volts_dac_code(0) >> 1,
                     aux_output.volts_dac_code(7) >> 1,
                     PanelChiffAmount_q30(setting, amount_mod, velocity),
                     PanelChiffAudibleSamples(setting, duration_mod, velocity));

    printf("%d %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", setting,
           delivered.peak_u16, mirrored.peak_u16,
           delivered.sustain_u16, mirrored.sustain_u16,
           delivered.attack_u32, mirrored.attack_u32,
           delivered.decay_u32, mirrored.decay_u32,
           delivered.release_u32, mirrored.release_u32,
           envelope.chiff_amount_initial_q30_, reference.chiff_amount_initial_q30_,
           envelope.chiff_slew_time_at_amount_zero_q5_27_,
           reference.chiff_slew_time_at_amount_zero_q5_27_,
           envelope.chiff_phase_step_q32_, reference.chiff_phase_step_q32_);
  }
  return 0;
}
