// Host driver for the CV OUTPUT PATH: Voice::NoteOn through CVOutput to the
// DAC. Nothing else off target reaches it -- the sim mirrors
// Part::VoiceNoteOn and stops one call short, and the envelope harness starts
// at Envelope::NoteOn -- which is the gap `acb66287` shipped through, where
// CVOutput::NoteOn narrowed the chiff increment to eight bits and only the aux
// CV outputs were wrong.
//
// Usage: ./cvtest <mode> [KEY=VALUE ...] ; one record per line.
//   chiff    every envelope this note reached, and the chiff parameters it got
//   dac      the aux CV channel's DAC words, one block per line
#define TEST 1
#define private public
#include "yarns/voice.h"
#include "tools/cvtest/dac_stub.h"
#include "tools/panel_chain.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// stmlib::Random lives in a TU the firmware links and this does not.
namespace stmlib { uint32_t Random::rng_state_ = 0x21; }

using namespace yarns;
using namespace stmlib;

namespace {

// One voice, an audio output and both aux outputs, so a single NoteOn reaches
// every envelope the firmware can route: the oscillator's gain and timbre, and
// one per aux CV.
Voice voice;
CVOutput audio_output, aux_1_output, aux_2_output;
ADSR adsr;

const uint8_t kAux1Channel = 1;

int OptInt(int argc, char** argv, const char* key, int fallback) {
  size_t n = strlen(key);
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  }
  return fallback;
}

void Wire() {
  voice.Init();
  audio_output.Init(true);
  aux_1_output.Init(true);
  aux_2_output.Init(true);
  voice.set_aux_cv(MOD_AUX_ENVELOPE);
  voice.set_aux_cv_2(MOD_AUX_ENVELOPE);
  voice.set_oscillator_mode(OSCILLATOR_MODE_ENVELOPED);
  audio_output.AssignVoices(&voice, DC_PITCH, 1, 1);
  aux_1_output.AssignVoices(&voice, DC_AUX_1, 1, 0);
  aux_2_output.AssignVoices(&voice, DC_AUX_2, 1, 0);
}

// Chiff state is range-independent by construction, so every envelope this
// note reached must hold the same values whatever its own rails are.
void PrintChiff(const char* name, const Envelope& envelope) {
  printf("%s %u %u %u %u %u %d %u %u\n", name,
         envelope.chiff_amount_initial_q30_,
         envelope.chiff_amount_q30_,
         envelope.chiff_phase_step_q32_,
         envelope.chiff_slew_time_at_amount_zero_q5_27_,
         envelope.chiff_slew_time_log2_q5_27_,
         envelope.chiff_slew_input_fraction_q30_,
         envelope.stage_phase_increment_u32_,
         envelope.stage_samples_left_);
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "chiff";

  const int amount = OptInt(argc, argv, "amount", 96);
  const int duration = OptInt(argc, argv, "duration", 67);
  const int velocity = OptInt(argc, argv, "velocity", 100);
  const int attack_setting = OptInt(argc, argv, "attack_setting", 40);
  const int decay_setting = OptInt(argc, argv, "decay_setting", 64);
  const int release_setting = OptInt(argc, argv, "release_setting", 64);
  const int blocks = OptInt(argc, argv, "blocks", 32);
  const int timbre = OptInt(argc, argv, "timbre", 8192);

  Wire();

  adsr.peak_u16 = UINT16_MAX;
  adsr.sustain_u16 = static_cast<uint16_t>(65535L * 60 / 100);
  adsr.attack_u32 = PanelStageIncrement(attack_setting, 0, 0);
  adsr.decay_u32 = PanelStageIncrement(decay_setting, 0, 0);
  adsr.release_u32 = PanelStageIncrement(release_setting, 0, 0);

  // The two the CV path is here to carry. Part::VoiceNoteOn forms the amount as
  // a fraction of full scale and the duration as an absolute sample count off
  // its own table -- both wider than a byte, which is the whole point.
  const uint32_t chiff_amount_q30 = PanelChiffAmount_q30(amount, 0, 0);
  const uint32_t chiff_audible_samples =
      PanelChiffAudibleSamples(duration, 0, 0);

  voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true,
               adsr, static_cast<int16_t>(timbre),
               chiff_amount_q30, chiff_audible_samples);

  if (!strcmp(mode, "chiff")) {
    // What the caller handed out, then what each destination holds.
    printf("given %u %u\n", chiff_amount_q30, chiff_audible_samples);
    PrintChiff("gain", voice.oscillator_.gain_envelope_);
    PrintChiff("timbre", voice.oscillator_.timbre_envelope_);
    PrintChiff("aux1", aux_1_output.envelope_);
    PrintChiff("aux2", aux_2_output.envelope_);
    return 0;
  }

  if (!strcmp(mode, "dac")) {
    for (int block = 0; block < blocks; ++block) {
      aux_1_output.RenderSamples(0, kAux1Channel, 0);
      for (size_t i = 0; i < kAudioBlockSize; ++i) {
        // The buffer is int16_t and the value in it is a Q16 DAC code, so the
        // top half of the range reads back negative. The DAC takes the bits.
        printf("%u%c", static_cast<uint16_t>(g_dac_block[kAux1Channel][i]),
               i + 1 == kAudioBlockSize ? '\n' : ' ');
      }
    }
    return 0;
  }

  // THE PITCH PIPELINE'S RESOLUTION. Every term that moves the pitch computes
  // more precision than a whole pitch unit, and each used to shift it away. A CZ
  // shape's aliases move up to 43 times faster than the note, so a whole unit of
  // pitch is a 41 Hz step in an audible tone -- the artifact was a staircase.
  //
  // The property is BEHAVIOURAL, not a copy of the arithmetic: move one control
  // by its own smallest step and the oscillator's phase increment has to move
  // with it. A plateau means a fraction is being dropped.
  //
  // DEMONSTRATED against the commit before the fix: bend 15 of 511 steps, and
  // portamento 128 of 1821 refreshes -- 128 being exactly the pitch units in
  // the semitone it glides, which is the staircase itself.
  if (!strcmp(mode, "pitch")) {
    int failures = 0;

    // PITCH BEND, at its default range of two semitones. 16384 bend steps cover
    // 256 pitch units, so 64 bend steps share a unit and 63 of every 64 landed
    // on a plateau.
    voice.set_pitch_bend_range(2);
    uint32_t previous = 0;
    int moved = 0;
    const int kBendSteps = 512;
    for (int i = 0; i < kBendSteps; ++i) {
      voice.PitchBend(static_cast<uint16_t>(8192 + i));
      voice.Refresh();
      if (i && voice.oscillator_.phase_increment_ != previous) ++moved;
      previous = voice.oscillator_.phase_increment_;
    }
    printf("%s bend: %d of %d steps moved the increment\n",
           moved > kBendSteps * 9 / 10 ? "PASS" : "FAIL", moved, kBendSteps - 1);
    if (moved <= kBendSteps * 9 / 10) ++failures;
    voice.PitchBend(8192);

    // PORTAMENTO, over ONE SEMITONE. A wide glide crosses a whole pitch unit
    // every refresh whatever the resolution, so it cannot see this: the
    // interval has to be narrow enough that the glide spends several refreshes
    // inside one unit.
    voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    voice.NoteOn(61 << 7, static_cast<uint8_t>(velocity), 32, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    previous = 0;
    moved = 0;
    int refreshes = 0;
    while (voice.portamento_phase_increment_ && refreshes < 20000) {
      voice.Refresh();
      if (refreshes && voice.oscillator_.phase_increment_ != previous) ++moved;
      previous = voice.oscillator_.phase_increment_;
      ++refreshes;
    }
    printf("%s portamento: %d of %d refreshes moved the increment\n",
           moved > refreshes * 3 / 4 ? "PASS" : "FAIL", moved, refreshes - 1);
    if (moved <= refreshes * 3 / 4) ++failures;

    // AND THE NOTE MUST NOT DRIFT. With nothing modulating it, the fraction has
    // to settle to zero: a standing one detunes a held note by up to a unit.
    voice.NoteOn(60 << 7, static_cast<uint8_t>(velocity), 0, 0, true, adsr,
                 static_cast<int16_t>(timbre), chiff_amount_q30,
                 chiff_audible_samples);
    for (int i = 0; i < 4000; ++i) voice.Refresh();
    const uint32_t settled = voice.oscillator_.phase_increment_;
    voice.oscillator_.Refresh(60 << 7, 0, 0, 0);
    printf("%s held note: increment %u against %u for the bare pitch\n",
           settled == voice.oscillator_.phase_increment_ ? "PASS" : "FAIL",
           settled, voice.oscillator_.phase_increment_);
    if (settled != voice.oscillator_.phase_increment_) ++failures;

    return failures ? 1 : 0;
  }

  fprintf(stderr, "unknown mode: %s\n", mode);
  return 1;
}
