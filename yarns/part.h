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
// Part.

#ifndef YARNS_PART_H_
#define YARNS_PART_H_

#include <algorithm>

#include "stmlib/stmlib.h"
#include "stmlib/algorithms/voice_allocator.h"
#include "stmlib/algorithms/note_stack.h"

#include "yarns/resources.h"
#include "yarns/looper.h"
#include "yarns/sequencer_step.h"
#include "yarns/arpeggiator.h"

namespace yarns {

class Voice;

const uint8_t kNumSteps = 30;
const uint8_t kNumMaxVoicesPerPart = 4;
const uint8_t kNumParaphonicVoices = 3;
const uint8_t kNoteStackSize = 12;
const uint8_t kNoteStackMapping = kNoteStackSize + 1; // 1-based

const uint8_t kMidiChannelOmni = 0x10;

const uint8_t kCCRecordOffOn = 110;
const uint8_t kCCDeleteRecording = 111;

enum ArpeggiatorDirection {
  ARPEGGIATOR_DIRECTION_LINEAR,
  ARPEGGIATOR_DIRECTION_UP_DOWN,
  ARPEGGIATOR_DIRECTION_RANDOM,
  ARPEGGIATOR_DIRECTION_STEP_JUMP,
  ARPEGGIATOR_DIRECTION_STEP_GRID,
  ARPEGGIATOR_DIRECTION_LAST
};

enum PolyMode {
  POLY_MODE_OFF,
  POLY_MODE_STEAL_RELEASE_SILENT,
  POLY_MODE_CYCLIC,
  POLY_MODE_RANDOM,
  POLY_MODE_VELOCITY,
  POLY_MODE_SORTED,
  POLY_MODE_UNISON_RELEASE_REASSIGN,
  POLY_MODE_UNISON_RELEASE_SILENT,
  POLY_MODE_STEAL_HIGHEST_PRIORITY,
  POLY_MODE_STEAL_RELEASE_REASSIGN,
  POLY_MODE_LAST
};

enum VoiceAllocationFlags {
  VOICE_ALLOCATION_NOT_FOUND = 0xff
};

enum MidiOutMode {
  MIDI_OUT_MODE_OFF,
  MIDI_OUT_MODE_THRU,
  MIDI_OUT_MODE_GENERATED_EVENTS
};

enum TuningSystem {
  TUNING_SYSTEM_EQUAL,
  TUNING_SYSTEM_JUST_INTONATION,
  TUNING_SYSTEM_PYTHAGOREAN,
  TUNING_SYSTEM_QUARTER_EB,
  TUNING_SYSTEM_QUARTER_E,
  TUNING_SYSTEM_QUARTER_EA,
  TUNING_SYSTEM_RAGA_1,
  TUNING_SYSTEM_RAGA_27 = TUNING_SYSTEM_RAGA_1 + 26,
  TUNING_SYSTEM_CUSTOM,
  TUNING_SYSTEM_LAST
};

enum SequencerInputResponse {
  SEQUENCER_INPUT_RESPONSE_OFF,
  SEQUENCER_INPUT_RESPONSE_TRANSPOSE,
  SEQUENCER_INPUT_RESPONSE_REPLACE,
  SEQUENCER_INPUT_RESPONSE_DIRECT,
  SEQUENCER_INPUT_RESPONSE_LAST,
};

enum PlayMode {
  PLAY_MODE_MANUAL,
  PLAY_MODE_ARPEGGIATOR,
  PLAY_MODE_SEQUENCER,
  PLAY_MODE_LAST
};

enum SustainMode {
  SUSTAIN_MODE_OFF,
  SUSTAIN_MODE_NORMAL,
  SUSTAIN_MODE_SOSTENUTO,
  SUSTAIN_MODE_LATCH,
  SUSTAIN_MODE_MOMENTARY_LATCH,
  SUSTAIN_MODE_CLUTCH,
  SUSTAIN_MODE_FILTER,
  SUSTAIN_MODE_LAST,
};

struct SequencerArpeggiatorResult { // Supports multiple return
  Arpeggiator arpeggiator; // Resulting arp state
  SequencerStep note; // Resulting note, including possible rest/tie
};

struct PackedPart {
  // Currently has 7 bits to spare

  struct PackedSequencerStep {
    unsigned int
      pitch : 8, // values free: 126 (about 29 bits' worth across 30 steps)
      velocity: 8; // values free: 0
  }__attribute__((packed));
  PackedSequencerStep sequencer_steps[kNumSteps];

  looper::PackedNote looper_notes[looper::kMaxNotes];
  unsigned int
    looper_oldest_index : looper::kBitsNoteIndex,
    looper_size         : looper::kBitsNoteIndex;

  static const uint8_t kTimbreBits = 7;
  static const uint8_t kLFOShapeBits = 2;

  signed int
    // MidiSettings
    transpose_octaves : 3, // values free: 0
    // VoicingSettings
    tuning_transpose : 7, // values free: 55
    tuning_fine : 7, // values free: 0
    lfo_spread_types : kTimbreBits,
    lfo_spread_voices : kTimbreBits,
    amplitude_mod_velocity : kTimbreBits,
    timbre_mod_envelope : kTimbreBits,
    timbre_mod_velocity : kTimbreBits,
    env_mod_attack : kTimbreBits,
    env_mod_decay : kTimbreBits,
    env_mod_sustain : kTimbreBits,
    env_mod_release : kTimbreBits;

  // MidiSettings
  unsigned int
    channel : 5, // values free: 15
    min_note : 7,
    max_note : 7,
    min_velocity : 7,
    max_velocity : 7,
    out_mode : 2, // values free: 1
    sustain_mode : 3, // values free: 1
    play_mode : 2, // values free: 1
    input_response : 2, // values free: 0
    sustain_polarity : 1;

  // VoicingSettings
  unsigned int
    allocation_mode : 4, // values free: 6
    allocation_priority : 2, // values free: 0
    portamento : 7,
    legato_retrigger : 1,
    portamento_legato_only: 1,
    pitch_bend_range : 5, // values free: 8
    vibrato_range : 4, // values free: 3
    vibrato_mod : 7,
    lfo_rate : 7, // values free: 32
    tuning_root : 4, // values free: 4
    tuning_system : 6, // values free: 30
    trigger_duration : 7, // Breaking: probably excessive
    trigger_scale : 1,
    trigger_shape : 3, // values free: 2
    aux_cv : 4, // values free: 0
    aux_cv_2 : 4, // values free: 0
    tuning_factor : 4, // values free: 2
    oscillator_mode : 2, // values free: 1
    oscillator_shape : 7, // Breaking: 1 bit unused, values unused: 77
    tremolo_mod : kTimbreBits,
    vibrato_shape : kLFOShapeBits,
    timbre_lfo_shape : kLFOShapeBits,
    tremolo_shape : kLFOShapeBits,
    timbre_initial : kTimbreBits,
    timbre_mod_lfo : kTimbreBits,
    env_init_attack : kTimbreBits,
    env_init_decay : kTimbreBits,
    env_init_sustain : kTimbreBits,
    env_init_release : kTimbreBits;

  // SequencerSettings
  unsigned int
    clock_division : 5, // values free: 0
    gate_length : 6, // values free: 0
    arp_range : 2, // values free: 0
    arp_direction : 3, // values free: 3
    arp_pattern : 5, // values free: 0
    euclidean_length : 5, // values free: 0
    euclidean_fill : 5, // values free: 0
    step_offset : 5, // values free: 0
    num_steps : 5, // values free: 1
    clock_quantization : 1,
    loop_length : 3; // values free: 0

}__attribute__((packed));

struct MidiSettings {
  uint8_t channel;
  uint8_t min_note;
  uint8_t max_note;
  uint8_t min_velocity;
  uint8_t max_velocity;
  uint8_t out_mode;
  uint8_t sustain_mode;
  int8_t transpose_octaves;
  uint8_t play_mode;
  uint8_t input_response;
  uint8_t sustain_polarity;
  uint8_t padding[5];

  void Pack(PackedPart& packed) const {
    packed.channel = channel;
    packed.min_note = min_note;
    packed.max_note = max_note;
    packed.min_velocity = min_velocity;
    packed.max_velocity = max_velocity;
    packed.out_mode = out_mode;
    packed.sustain_mode = sustain_mode;
    packed.transpose_octaves = transpose_octaves;
    packed.play_mode = play_mode;
    packed.input_response = input_response;
    packed.sustain_polarity = sustain_polarity;
  }

  void Unpack(PackedPart& packed) {
    channel = packed.channel;
    min_note = packed.min_note;
    max_note = packed.max_note;
    min_velocity = packed.min_velocity;
    max_velocity = packed.max_velocity;
    out_mode = packed.out_mode;
    sustain_mode = packed.sustain_mode;
    transpose_octaves = packed.transpose_octaves;
    play_mode = packed.play_mode;
    input_response = packed.input_response;
    sustain_polarity = packed.sustain_polarity;
  }

};

struct VoicingSettings {
  uint8_t allocation_mode;
  uint8_t allocation_priority;
  uint8_t portamento;
  uint8_t legato_retrigger;
  uint8_t portamento_legato_only;
  uint8_t pitch_bend_range;
  uint8_t vibrato_range;
  uint8_t vibrato_mod;
  uint8_t tremolo_mod;
  uint8_t vibrato_shape;
  uint8_t timbre_lfo_shape;
  uint8_t tremolo_shape;
  uint8_t lfo_rate;
  int8_t lfo_spread_types;
  int8_t lfo_spread_voices;
  int8_t tuning_transpose;
  int8_t tuning_fine;
  int8_t tuning_root;
  uint8_t tuning_system;
  uint8_t trigger_duration;
  uint8_t trigger_scale;
  uint8_t trigger_shape;
  uint8_t aux_cv;
  uint8_t aux_cv_2;
  uint8_t tuning_factor;
  uint8_t oscillator_mode;
  uint8_t oscillator_shape;
  uint8_t timbre_initial;
  uint8_t timbre_mod_lfo;
  int8_t timbre_mod_envelope;
  int8_t timbre_mod_velocity;
  int8_t amplitude_mod_velocity;
  uint8_t env_init_attack;
  uint8_t env_init_decay;
  uint8_t env_init_sustain;
  uint8_t env_init_release;
  int8_t env_mod_attack;
  int8_t env_mod_decay;
  int8_t env_mod_sustain;
  int8_t env_mod_release;
  // uint8_t padding[-2];

  void Pack(PackedPart& packed) const {
    packed.allocation_mode = allocation_mode;
    packed.allocation_priority = allocation_priority;
    packed.portamento = portamento;
    packed.legato_retrigger = legato_retrigger;
    packed.portamento_legato_only = portamento_legato_only;
    packed.pitch_bend_range = pitch_bend_range;
    packed.vibrato_range = vibrato_range;
    packed.vibrato_mod = vibrato_mod;
    packed.tremolo_mod = tremolo_mod;
    packed.vibrato_shape = vibrato_shape;
    packed.timbre_lfo_shape = timbre_lfo_shape;
    packed.tremolo_shape = tremolo_shape;
    packed.lfo_rate = lfo_rate;
    packed.lfo_spread_types = lfo_spread_types;
    packed.lfo_spread_voices = lfo_spread_voices;
    packed.tuning_transpose = tuning_transpose;
    packed.tuning_fine = tuning_fine;
    packed.tuning_root = tuning_root;
    packed.tuning_system = tuning_system;
    packed.trigger_duration = trigger_duration;
    packed.trigger_scale = trigger_scale;
    packed.trigger_shape = trigger_shape;
    packed.aux_cv = aux_cv;
    packed.aux_cv_2 = aux_cv_2;
    packed.tuning_factor = tuning_factor;
    packed.oscillator_mode = oscillator_mode;
    packed.oscillator_shape = oscillator_shape;
    packed.timbre_initial = timbre_initial;
    packed.timbre_mod_lfo = timbre_mod_lfo;
    packed.timbre_mod_envelope = timbre_mod_envelope;
    packed.timbre_mod_velocity = timbre_mod_velocity;
    packed.amplitude_mod_velocity = amplitude_mod_velocity;
    packed.env_init_attack = env_init_attack;
    packed.env_init_decay = env_init_decay;
    packed.env_init_sustain = env_init_sustain;
    packed.env_init_release = env_init_release;
    packed.env_mod_attack = env_mod_attack;
    packed.env_mod_decay = env_mod_decay;
    packed.env_mod_sustain = env_mod_sustain;
    packed.env_mod_release = env_mod_release;
  }

  void Unpack(PackedPart& packed) {
    allocation_mode = packed.allocation_mode;
    allocation_priority = packed.allocation_priority;
    portamento = packed.portamento;
    legato_retrigger = packed.legato_retrigger;
    portamento_legato_only = packed.portamento_legato_only;
    pitch_bend_range = packed.pitch_bend_range;
    vibrato_range = packed.vibrato_range;
    vibrato_mod = packed.vibrato_mod;
    tremolo_mod = packed.tremolo_mod;
    vibrato_shape = packed.vibrato_shape;
    timbre_lfo_shape = packed.timbre_lfo_shape;
    tremolo_shape = packed.tremolo_shape;
    lfo_rate = packed.lfo_rate;
    lfo_spread_types = packed.lfo_spread_types;
    lfo_spread_voices = packed.lfo_spread_voices;
    tuning_transpose = packed.tuning_transpose;
    tuning_fine = packed.tuning_fine;
    tuning_root = packed.tuning_root;
    tuning_system = packed.tuning_system;
    trigger_duration = packed.trigger_duration;
    trigger_scale = packed.trigger_scale;
    trigger_shape = packed.trigger_shape;
    aux_cv = packed.aux_cv;
    aux_cv_2 = packed.aux_cv_2;
    tuning_factor = packed.tuning_factor;
    oscillator_mode = packed.oscillator_mode;
    oscillator_shape = packed.oscillator_shape;
    timbre_initial = packed.timbre_initial;
    timbre_mod_lfo = packed.timbre_mod_lfo;
    timbre_mod_envelope = packed.timbre_mod_envelope;
    timbre_mod_velocity = packed.timbre_mod_velocity;
    amplitude_mod_velocity = packed.amplitude_mod_velocity;
    env_init_attack = packed.env_init_attack;
    env_init_decay = packed.env_init_decay;
    env_init_sustain = packed.env_init_sustain;
    env_init_release = packed.env_init_release;
    env_mod_attack = packed.env_mod_attack;
    env_mod_decay = packed.env_mod_decay;
    env_mod_sustain = packed.env_mod_sustain;
    env_mod_release = packed.env_mod_release;
  }

};


enum PartSetting {
  PART_MIDI_CHANNEL,
  PART_MIDI_MIN_NOTE,
  PART_MIDI_MAX_NOTE,
  PART_MIDI_MIN_VELOCITY,
  PART_MIDI_MAX_VELOCITY,
  PART_MIDI_OUT_MODE,
  PART_MIDI_SUSTAIN_MODE,
  PART_MIDI_TRANSPOSE_OCTAVES,
  PART_MIDI_PLAY_MODE,
  PART_MIDI_INPUT_RESPONSE,
  PART_MIDI_SUSTAIN_POLARITY,
  PART_MIDI_LAST = PART_MIDI_CHANNEL + sizeof(MidiSettings) - 1,
  PART_VOICING_ALLOCATION_MODE,
  PART_VOICING_ALLOCATION_PRIORITY,
  PART_VOICING_PORTAMENTO,
  PART_VOICING_LEGATO_RETRIGGER,
  PART_VOICING_PORTAMENTO_LEGATO_ONLY,
  PART_VOICING_PITCH_BEND_RANGE,
  PART_VOICING_VIBRATO_RANGE,
  PART_VOICING_VIBRATO_MOD,
  PART_VOICING_TREMOLO_MOD,
  PART_VOICING_VIBRATO_SHAPE,
  PART_VOICING_TIMBRE_LFO_SHAPE,
  PART_VOICING_TREMOLO_SHAPE,
  PART_VOICING_LFO_RATE,
  PART_VOICING_LFO_SPREAD_TYPES,
  PART_VOICING_LFO_SPREAD_VOICES,
  PART_VOICING_TUNING_TRANSPOSE,
  PART_VOICING_TUNING_FINE,
  PART_VOICING_TUNING_ROOT,
  PART_VOICING_TUNING_SYSTEM,
  PART_VOICING_TRIGGER_DURATION,
  PART_VOICING_TRIGGER_SCALE,
  PART_VOICING_TRIGGER_SHAPE,
  PART_VOICING_AUX_CV,
  PART_VOICING_AUX_CV_2,
  PART_VOICING_TUNING_FACTOR,
  PART_VOICING_OSCILLATOR_MODE,
  PART_VOICING_OSCILLATOR_SHAPE,
  PART_VOICING_TIMBRE_INIT,
  PART_VOICING_TIMBRE_MOD_LFO,
  PART_VOICING_TIMBRE_MOD_ENVELOPE,
  PART_VOICING_TIMBRE_MOD_VELOCITY,
  PART_VOICING_ENV_PEAK_MOD_VELOCITY,
  PART_VOICING_ENV_INIT_ATTACK,
  PART_VOICING_ENV_INIT_DECAY,
  PART_VOICING_ENV_INIT_SUSTAIN,
  PART_VOICING_ENV_INIT_RELEASE,
  PART_VOICING_ENV_MOD_ATTACK,
  PART_VOICING_ENV_MOD_DECAY,
  PART_VOICING_ENV_MOD_SUSTAIN,
  PART_VOICING_ENV_MOD_RELEASE,
  PART_VOICING_LAST = PART_VOICING_ALLOCATION_MODE + sizeof(VoicingSettings) - 1,
  PART_SEQUENCER_CLOCK_DIVISION,
  PART_SEQUENCER_GATE_LENGTH,
  PART_SEQUENCER_ARP_RANGE,
  PART_SEQUENCER_ARP_DIRECTION,
  PART_SEQUENCER_ARP_PATTERN,
  PART_SEQUENCER_EUCLIDEAN_LENGTH,
  PART_SEQUENCER_EUCLIDEAN_FILL,
  PART_SEQUENCER_STEP_OFFSET,
  PART_SEQUENCER_NUM_STEPS,
  PART_SEQUENCER_CLOCK_QUANTIZATION,
  PART_SEQUENCER_LOOP_LENGTH,
};


struct SequencerSettings {
  uint8_t clock_division;
  uint8_t gate_length;
  uint8_t arp_range;
  uint8_t arp_direction;
  uint8_t arp_pattern;
  uint8_t euclidean_length;
  uint8_t euclidean_fill;
  uint8_t step_offset;
  uint8_t num_steps;
  uint8_t clock_quantization;
  uint8_t loop_length;
  uint8_t padding_fields[5];

  SequencerStep step[kNumSteps];
  uint8_t padding_steps[2];

  void Pack(PackedPart& packed) const {
    for (uint8_t i = 0; i < kNumSteps; i++) {
      PackedPart::PackedSequencerStep& packed_step = packed.sequencer_steps[i];
      packed_step.pitch = step[i].data[0];
      packed_step.velocity = step[i].data[1];
    }

    packed.clock_division = clock_division;
    packed.gate_length = gate_length;
    packed.arp_range = arp_range;
    packed.arp_direction = arp_direction;
    packed.arp_pattern = arp_pattern;
    packed.euclidean_length = euclidean_length;
    packed.euclidean_fill = euclidean_fill;
    packed.step_offset = step_offset;
    packed.num_steps = num_steps;
    packed.clock_quantization = clock_quantization;
    packed.loop_length = loop_length;
  }

  void Unpack(PackedPart& packed) {
    for (uint8_t i = 0; i < kNumSteps; i++) {
      PackedPart::PackedSequencerStep& packed_step = packed.sequencer_steps[i];
      step[i].data[0] = packed_step.pitch;
      step[i].data[1] = packed_step.velocity;
    }

    clock_division = packed.clock_division;
    gate_length = packed.gate_length;
    arp_range = packed.arp_range;
    arp_direction = packed.arp_direction;
    arp_pattern = packed.arp_pattern;
    euclidean_length = packed.euclidean_length;
    euclidean_fill = packed.euclidean_fill;
    step_offset = packed.step_offset;
    num_steps = packed.num_steps;
    clock_quantization = packed.clock_quantization;
    loop_length = packed.loop_length;
  }
  
  int16_t first_note() const {
    for (uint8_t i = 0; i < num_steps; ++i) {
      if (step[i].has_note()) {
        return step[i].note();
      }
    }
    return kC4;
  }
};

struct HeldKeys {

  static const uint8_t VELOCITY_SUSTAIN_MASK = 0x80;

  stmlib::NoteStack<kNoteStackSize> stack;
  bool universally_sustainable; // Includes keys not yet pressed
  bool stop_sustained_notes_on_next_note_on;
  bool individually_sustainable[kNoteStackMapping]; // Only keys already held

  void Init() {
    stack.Init();
    universally_sustainable = false;
    stop_sustained_notes_on_next_note_on = false;
    std::fill(
      &individually_sustainable[0],
      &individually_sustainable[kNoteStackMapping],
      false
    );
  }

  // Returns true if result is NoteOff
  bool NoteOff(uint8_t pitch, bool respect_sustain) {
    if (respect_sustain) {
      SetSustain(pitch);
      if (IsSustained(pitch)) return false;
    }
    stack.NoteOff(pitch);
    return true;
  }

  void SetSustain(uint8_t pitch) {
    uint8_t i = stack.Find(pitch);
    if (!i || !IsSustainable(i)) { return; }
    // Flag the note so that it is removed once the sustain pedal is released.
    stack.mutable_note(i)->velocity |= VELOCITY_SUSTAIN_MASK;
  }

  void SetIndividuallySustainable(bool value) {
    for (uint8_t i = 1; i <= stack.max_size(); ++i) {
      if (stack.note(i).note == stmlib::NOTE_STACK_FREE_SLOT) { continue; }
      individually_sustainable[i - 1] = value;
    }
  }

  void Clutch(bool sustain_on) {
    stop_sustained_notes_on_next_note_on = !sustain_on;
    SetIndividuallySustainable(sustain_on);
  }

  void Latch(bool sustain_on) {
    universally_sustainable = sustain_on;
    stop_sustained_notes_on_next_note_on = true;
  }

  bool IsSustainable(uint8_t index) const {
    return universally_sustainable || individually_sustainable[index - 1];
  }

  bool IsSustained(const stmlib::NoteEntry &note_entry) const {
    // If the note is flagged, it can only be released by StopSustainedNotes
    return note_entry.velocity & VELOCITY_SUSTAIN_MASK;
  }
  bool IsSustained(uint8_t pitch) const {
    return IsSustained(stack.note(stack.Find(pitch)));
  }

};

class Part {
 public:
  Part() { }
  ~Part() { }
  
  void Init();
  
  uint8_t HeldKeysNoteOn(HeldKeys &keys, uint8_t pitch, uint8_t velocity);
  void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void NoteOff(uint8_t channel, uint8_t note, bool respect_sustain = true);
  uint8_t TransposeInputPitch(uint8_t pitch, int8_t transpose_octaves) const {
    CONSTRAIN(transpose_octaves, (0 - pitch) / 12, (127 - pitch) / 12);
    return pitch + 12 * transpose_octaves;
  }
  uint8_t TransposeInputPitch(uint8_t pitch) const {
    return TransposeInputPitch(pitch, midi_.transpose_octaves);
  }
  void InternalNoteOn(uint8_t note, uint8_t velocity, bool force_legato = false);
  void InternalNoteOff(uint8_t note);
  // Absolute CCs only
  void ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
  void PitchBend(uint8_t channel, uint16_t pitch_bend);
  void Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity);
  void Aftertouch(uint8_t channel, uint8_t velocity);
  void AllNotesOff();
  void StopSequencerArpeggiatorNotes();
  // Returns the 1-based index of the new note IFF there was room for it
  uint8_t GeneratedNoteOn(uint8_t pitch, uint8_t velocity);
  void GeneratedNoteOff(uint8_t pitch);
  void Reset();
  // Pulses Per Quarter Note
  uint16_t PPQN() const { return lut_clock_ratio_ticks[seq_.clock_division]; }
  void Clock();
  // Advance step sequencer and/or arpeggiator
  SequencerArpeggiatorResult BuildNextStepResult(uint32_t step_counter) const;
  void ClockStepGateEndings();
  void Start();
  void AdvanceArpForSongPosition();
  inline uint32_t ticks_to_steps(int32_t ticks) {
    return seq_.step_offset + ticks / PPQN();
  }
  inline uint16_t steps_per_arp_reset() const {
    int8_t sequence_repeats_per_arp_reset = seq_.arp_pattern - LUT_ARPEGGIATOR_PATTERNS_SIZE;
    if (sequence_repeats_per_arp_reset <= 0) return 0;
    uint8_t steps_per_sequence_repeat = looped() ? (1 << seq_.loop_length) : seq_.num_steps;
    return sequence_repeats_per_arp_reset * steps_per_sequence_repeat;
  }
  void StopRecording();
  void StartRecording();
  void DeleteSequence();

  inline void NewLayout() {
    midi_.min_note = 0;
    midi_.max_note = 127;
    midi_.min_velocity = 0;
    midi_.max_velocity = 127;

    voicing_.allocation_mode = num_voices_ > 1 ? POLY_MODE_STEAL_RELEASE_SILENT : POLY_MODE_OFF;
    voicing_.allocation_priority = stmlib::NOTE_STACK_PRIORITY_LAST;
    voicing_.portamento = 0;
    voicing_.legato_retrigger = true;
    voicing_.portamento_legato_only = false;
  }

  inline bool seq_has_notes() const { return looped() ? looper_.num_notes() : seq_.num_steps; }
  inline bool seq_overwrite() const { return seq_overwrite_; }
  inline void toggle_seq_overwrite() { set_seq_overwrite(!seq_overwrite_); }
  inline void set_seq_overwrite(bool b) {
    seq_overwrite_ = b && seq_has_notes();
  }

  inline const looper::Deck& looper() const { return looper_; }
  inline looper::Deck& mutable_looper() { return looper_; }
  inline uint8_t LooperCurrentNoteIndex() const {
    return looper_note_index_for_generated_note_index_[generated_notes_.most_recent_note_index()];
  }

  inline bool looper_is_recording(uint8_t pressed_key_index) const {
    return looper_note_recording_pressed_key_[pressed_key_index] != looper::kNullIndex;
  }

  inline bool looper_can_control(uint8_t pitch) const {
    if (!manual_control()) { return true; }
    uint8_t key = manual_keys_.stack.Find(pitch);
    if (!key) { return true; } // We got here first
    if (manual_keys_.IsSustained(pitch)) {
      // Manual control has not been relinquished
      return false;
    }
    return looper_is_recording(key);
  }

  inline bool looped() const {
    return seq_.clock_quantization == 0;
  }

  inline bool looper_in_use() const {
    return looped() && sequencer_in_use();
  }
  inline bool doing_stepped_stuff() const {
    return !(looper_in_use() || midi_.play_mode == PLAY_MODE_MANUAL);
  }
  inline bool sequencer_in_use() const {
    return midi_.play_mode == PLAY_MODE_SEQUENCER ||
      (midi_.play_mode == PLAY_MODE_ARPEGGIATOR && seq_driven_arp());
  }

  // Does arpeggiator use notes from the loop/step sequence?
  inline bool seq_driven_arp() const {
    return seq_.arp_pattern >= LUT_ARPEGGIATOR_PATTERNS_SIZE;
  }

  inline bool uses_poly_allocator() const {
    return
      voicing_.allocation_mode == POLY_MODE_STEAL_RELEASE_SILENT ||
      voicing_.allocation_mode == POLY_MODE_STEAL_RELEASE_REASSIGN ||
      voicing_.allocation_mode == POLY_MODE_STEAL_HIGHEST_PRIORITY;
  }

  inline bool uses_sorted_dispatch() const {
    return
      voicing_.allocation_mode == POLY_MODE_SORTED ||
      voicing_.allocation_mode == POLY_MODE_UNISON_RELEASE_REASSIGN ||
      voicing_.allocation_mode == POLY_MODE_UNISON_RELEASE_SILENT;
  }

  void LooperPlayNoteOn(uint8_t looper_note_index, uint8_t pitch, uint8_t velocity);
  void LooperPlayNoteOff(uint8_t looper_note_index, uint8_t pitch);
  void LooperRecordNoteOn(uint8_t pressed_key_index);
  void LooperRecordNoteOff(uint8_t pressed_key_index);

  void DeleteRecording();

  inline void SustainOn() {
    HeldKeysSustainOn(manual_keys_);
    HeldKeysSustainOn(arp_keys_);
  }
  inline void SustainOff() {
    HeldKeysSustainOff(manual_keys_);
    HeldKeysSustainOff(arp_keys_);
  }
  void HeldKeysSustainOn(HeldKeys &keys);
  void HeldKeysSustainOff(HeldKeys &keys);
  inline const HeldKeys& HeldKeysForUI() const {
    return midi_.play_mode == PLAY_MODE_ARPEGGIATOR ? arp_keys_ : manual_keys_;
  }
  inline HeldKeys& MutableHeldKeysForUI() {
    return midi_.play_mode == PLAY_MODE_ARPEGGIATOR ? arp_keys_ : manual_keys_;
  }
  inline void ResetKeys(HeldKeys &keys) {
    StopSustainedNotes(keys);
    keys.Init();
  }
  void ResetAllKeys();

  void RecordStep(const SequencerStep& step);

  inline void ModifyNoteAtCurrentStep(uint8_t note) {
    if (seq_recording_) {
      seq_.step[seq_rec_step_].data[0] = note;
    }
  }
  
  inline void RecordStep(SequencerStepFlags flag) {
    RecordStep(SequencerStep(flag, 0));
  }
  
  inline bool manual_control() const {
    return (
      midi_.play_mode == PLAY_MODE_MANUAL || (
        midi_.input_response == SEQUENCER_INPUT_RESPONSE_DIRECT &&
        midi_.play_mode == PLAY_MODE_SEQUENCER
      )
    );
  }
  
  void AllocateVoices(Voice* voice, uint8_t num_voices, bool polychain);
  inline void set_custom_pitch_table(int8_t* table) {
    custom_pitch_table_ = table;
  }
  
  inline uint8_t tx_channel() const {
    return midi_.channel == kMidiChannelOmni ? 0 : midi_.channel;
  }
  // The return value indicates whether the message can be forwarded to the
  // MIDI out (soft-thru). For example, when the arpeggiator is on, NoteOn
  // or NoteOff can return false to make sure that the chord that triggers
  // the arpeggiator does not find its way to the MIDI out. Instead it will 
  // be sent note by note within InternalNoteOn and InternalNoteOff.
  //
  // Also, note that channel / keyrange / velocity range filtering is not
  // applied here. It is up to the caller to call accepts() first to check
  // whether the message should be sent to the part.
  inline bool notes_thru() const {
    return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
  }
  inline bool cc_thru() const { return midi_.out_mode != MIDI_OUT_MODE_OFF; }
  
  inline bool has_velocity_filtering() {
    return midi_.min_velocity != 0 || midi_.max_velocity != 127;
  }

  inline uint8_t FindVoiceForNote(uint8_t note) const {
    for (uint8_t i = 0; i < num_voices_; ++i) {
      if (active_note_[i] == note) {
        return i;
      }
    }
    return VOICE_ALLOCATION_NOT_FOUND;
  }

  inline const stmlib::NoteEntry& priority_note(
      stmlib::NoteStackFlags priority,
      uint8_t index = 0
  ) const {
    return mono_allocator_.note_by_priority(priority, index);
  }
  inline const stmlib::NoteEntry& priority_note(uint8_t index = 0) const {
    return priority_note(
        static_cast<stmlib::NoteStackFlags>(voicing_.allocation_priority),
        index
    );
  }

  inline Voice* voice(uint8_t index) const { return voice_[index]; }
  inline uint8_t num_voices() const { return num_voices_; }
  
  bool Set(uint8_t address, uint8_t value);
  inline uint8_t Get(uint8_t address) const {
    const uint8_t* bytes;
    bytes = static_cast<const uint8_t*>(static_cast<const void*>(&midi_));
    return bytes[address];
  }

  inline const MidiSettings& midi_settings() const { return midi_; }
  inline const VoicingSettings& voicing_settings() const { return voicing_; }
  inline const SequencerSettings& sequencer_settings() const { return seq_; }
  inline MidiSettings* mutable_midi_settings() { return &midi_; }
  inline VoicingSettings* mutable_voicing_settings() { return &voicing_; }
  inline SequencerSettings* mutable_sequencer_settings() { return &seq_; }

  inline bool has_notes() const {
    return arp_keys_.stack.most_recent_note_index() ||
      manual_keys_.stack.most_recent_note_index();
  }
  
  inline bool recording() const { return seq_recording_; }
  inline bool overdubbing() const { return seq_overdubbing_; }
  inline uint8_t recording_step() const { return seq_rec_step_; }
  // TODO this depends on step_counter_ state, does it matter for song pos?
  inline uint8_t playing_step() const { return step_counter_ % seq_.num_steps; }
  inline uint8_t num_steps() const { return seq_.num_steps; }
  inline void increment_recording_step_index(uint8_t n) {
    seq_rec_step_ += n;
    seq_rec_step_ = stmlib::modulo(seq_rec_step_, overdubbing() ? seq_.num_steps : kNumSteps);
  }

  void Pack(PackedPart& packed) const {
    looper_.Pack(packed);
    midi_.Pack(packed);
    voicing_.Pack(packed);
    seq_.Pack(packed);
  }

  void Unpack(PackedPart& packed) {
    looper_.Unpack(packed);
    midi_.Unpack(packed);
    voicing_.Unpack(packed);
    seq_.Unpack(packed);
  }
  
  void AfterDeserialize() {
    CONSTRAIN(midi_.play_mode, 0, PLAY_MODE_LAST - 1);
    CONSTRAIN(seq_.clock_quantization, 0, 1);
    CONSTRAIN(seq_.loop_length, 0, 7);
    CONSTRAIN(seq_.arp_range, 0, 3);
    CONSTRAIN(seq_.arp_direction, 0, ARPEGGIATOR_DIRECTION_LAST - 1);
    AllNotesOff();
    TouchVoices();
    TouchVoiceAllocation();
    ResetAllKeys();
  }

  void set_siblings(bool has_siblings) {
    has_siblings_ = has_siblings;
  }
  
 private:
  int16_t Tune(int16_t note);
  void ResetAllControllers();
  void TouchVoiceAllocation();
  void TouchVoices();
  
  void StopNotesBySustainStatus(HeldKeys &keys, bool where_sustained);
  void StopSustainedNotes(HeldKeys &keys) {
    StopNotesBySustainStatus(keys, true);
  }
  void DispatchSortedNotes(bool legato);
  void VoiceNoteOn(
    uint8_t voice, uint8_t pitch, uint8_t vel,
    bool legato, bool reset_gate_counter
  );
  void VoiceNoteOff(uint8_t voice);
  void KillAllInstancesOfNote(uint8_t note);

  uint8_t ApplySequencerInputResponse(int16_t pitch, int8_t root_pitch = kC4) const;
  const SequencerStep BuildSeqStep(uint8_t step_index) const;
  const SequencerArpeggiatorResult BuildNextArpeggiatorResult(
    uint32_t pattern_step_counter, const SequencerStep& seq_step) const {
    return arpeggiator_.BuildNextResult(
      *this, arp_keys_, pattern_step_counter, seq_step);
  }

  MidiSettings midi_;
  VoicingSettings voicing_;
  SequencerSettings seq_;
  
  Voice* voice_[kNumMaxVoicesPerPart];
  int8_t* custom_pitch_table_;
  uint8_t num_voices_;
  bool polychained_;

  HeldKeys manual_keys_;
  HeldKeys arp_keys_;
  bool hold_pedal_engaged_;

  stmlib::NoteStack<kNoteStackSize> generated_notes_;  // by sequencer or arpeggiator.
  stmlib::NoteStack<kNoteStackSize> mono_allocator_;
  stmlib::VoiceAllocator<kNumMaxVoicesPerPart * 2> poly_allocator_;
  uint8_t active_note_[kNumMaxVoicesPerPart]; // Tracks active note for each voice
  uint8_t cyclic_allocation_note_counter_;
  
  Arpeggiator arpeggiator_;
  
  bool seq_recording_;
  bool seq_overdubbing_;
  int32_t step_counter_;
  uint8_t seq_rec_step_;
  bool seq_overwrite_;
  
  looper::Deck looper_;

  // Tracks which looper note (if any) is currently being recorded by a given
  // held key. Used to a) find and conclude that looper note again when a
  // NoteOff arrives, and b) allow ApplySequencerInputResponse to distinguish
  // keys held for true manual control vs recording (ignored)
  uint8_t looper_note_recording_pressed_key_[kNoteStackMapping];

  // Tracks which looper notes are currently playing, so they can be turned off later
  uint8_t looper_note_index_for_generated_note_index_[kNoteStackMapping];

  // Post-transpose
  uint8_t output_pitch_for_looper_note_[looper::kMaxNotes];

  uint16_t gate_length_counter_[kNumMaxVoicesPerPart];
  
  bool has_siblings_;
  
  bool multi_is_recording_;

  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace yarns

#endif // YARNS_PART_H_
