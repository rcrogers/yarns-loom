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

#include "yarns/looper.h"

namespace yarns {

class Voice;

const uint8_t kNumSteps = 30;
const uint8_t kNumMaxVoicesPerPart = 4;
const uint8_t kNumParaphonicVoices = 3;
const uint8_t kNoteStackSize = 12;
const uint8_t kNoteStackMapping = kNoteStackSize + 1; // 1-based

const uint8_t kCCRecordOffOn = 110;
const uint8_t kCCDeleteRecording = 111;

const uint8_t kC4 = 60;
const uint8_t whiteKeyValues[] = {
  0,    0xff, 1,    0xff,
  2,    3,    0xff, 4,
  0xff, 5,    0xff, 6,
};
const uint8_t blackKeyValues[] = {
  0xff, 0,    0xff, 1,
  0xff, 0xff, 2,    0xff,
  3,    0xff, 4,    0xff,
};
const uint8_t kNumBlackKeys = 5;
const uint8_t kNumWhiteKeys = 7;

enum ArpeggiatorDirection {
  ARPEGGIATOR_DIRECTION_LINEAR,
  ARPEGGIATOR_DIRECTION_UP_DOWN,
  ARPEGGIATOR_DIRECTION_RANDOM,
  ARPEGGIATOR_DIRECTION_STEP_ROTATE,
  ARPEGGIATOR_DIRECTION_STEP_SUBROTATE,
  ARPEGGIATOR_DIRECTION_LAST
};

enum VoiceAllocationMode {
  VOICE_ALLOCATION_MODE_MONO,
  VOICE_ALLOCATION_MODE_POLY,
  VOICE_ALLOCATION_MODE_POLY_CYCLIC,
  VOICE_ALLOCATION_MODE_POLY_RANDOM,
  VOICE_ALLOCATION_MODE_POLY_VELOCITY,
  VOICE_ALLOCATION_MODE_POLY_SORTED,
  VOICE_ALLOCATION_MODE_POLY_UNISON_1,
  VOICE_ALLOCATION_MODE_POLY_UNISON_2,
  VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT,
  VOICE_ALLOCATION_MODE_POLY_NICE,
  VOICE_ALLOCATION_MODE_LAST
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

enum LegatoMode {
  LEGATO_MODE_OFF,
  LEGATO_MODE_AUTO_PORTAMENTO,
  LEGATO_MODE_ON,
  LEGATO_MODE_LAST
};
struct PackedPart {
  // Currently has 36 bits to spare

  struct PackedSequencerStep {
    unsigned int
      pitch : 8,
      velocity: 8;
  }__attribute__((packed));
  PackedSequencerStep sequencer_steps[kNumSteps];

  looper::PackedNote looper_notes[looper::kMaxNotes];
  unsigned int
    looper_oldest_index : looper::kBitsNoteIndex,
    looper_size         : looper::kBitsNoteIndex;

  static const uint8_t kTimbreBits = 7;

  signed int
    // MidiSettings
    transpose_octaves : 3,
    // VoicingSettings
    tuning_transpose : 7,
    tuning_fine : 7,
    amplitude_mod_velocity : kTimbreBits,
    timbre_mod_envelope : kTimbreBits,
    timbre_mod_velocity : kTimbreBits,
    env_mod_attack : kTimbreBits,
    env_mod_decay : kTimbreBits,
    env_mod_sustain : kTimbreBits,
    env_mod_release : kTimbreBits;

  // MidiSettings
  unsigned int
    channel : 5,
    min_note : 7,
    max_note : 7,
    min_velocity : 7,
    max_velocity : 7,
    out_mode : 2,
    sustain_mode : 3,
    play_mode : 2,
    input_response : 2,
    sustain_polarity : 1;

  // VoicingSettings
  unsigned int
    allocation_mode : 4,
    allocation_priority : 2,
    portamento : 7,
    legato_mode : 2,
    pitch_bend_range : 5,
    vibrato_shape: 3,
    vibrato_range : 4,
    vibrato_mod : 7,
    lfo_rate : 7,
    tuning_root : 4,
    tuning_system : 6,
    trigger_duration : 7, // probably excessive
    trigger_scale : 1,
    trigger_shape : 3,
    aux_cv : 4, // barely
    aux_cv_2 : 4, // barely
    tuning_factor : 4,
    oscillator_mode : 2,
    oscillator_shape : 7,
    tremolo_mod : kTimbreBits,
    tremolo_shape : 3,
    timbre_initial : kTimbreBits,
    timbre_mod_lfo : kTimbreBits,
    env_init_attack : kTimbreBits,
    env_init_decay : kTimbreBits,
    env_init_sustain : kTimbreBits,
    env_init_release : kTimbreBits;

  // SequencerSettings
  unsigned int
    clock_division : 5,
    gate_length : 6,
    arp_range : 2,
    arp_direction : 3,
    arp_pattern : 5,
    euclidean_length : 5,
    euclidean_fill : 5,
    euclidean_rotate : 5,
    num_steps : 5,
    clock_quantization : 1,
    loop_length : 3;

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
  uint8_t legato_mode;
  uint8_t pitch_bend_range;
  uint8_t vibrato_range;
  uint8_t vibrato_mod;
  uint8_t tremolo_mod;
  uint8_t tremolo_shape;
  uint8_t lfo_rate;
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
    packed.legato_mode = legato_mode;
    packed.pitch_bend_range = pitch_bend_range;
    packed.vibrato_range = vibrato_range;
    packed.vibrato_mod = vibrato_mod;
    packed.tremolo_mod = tremolo_mod;
    packed.tremolo_shape = tremolo_shape;
    packed.lfo_rate = lfo_rate;
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
    legato_mode = packed.legato_mode;
    pitch_bend_range = packed.pitch_bend_range;
    vibrato_range = packed.vibrato_range;
    vibrato_mod = packed.vibrato_mod;
    tremolo_mod = packed.tremolo_mod;
    tremolo_shape = packed.tremolo_shape;
    lfo_rate = packed.lfo_rate;
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
  PART_VOICING_LEGATO_MODE,
  PART_VOICING_PITCH_BEND_RANGE,
  PART_VOICING_VIBRATO_RANGE,
  PART_VOICING_VIBRATO_MOD,
  PART_VOICING_TREMOLO_MOD,
  PART_VOICING_TREMOLO_SHAPE,
  PART_VOICING_LFO_RATE,
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
  PART_SEQUENCER_EUCLIDEAN_ROTATE,
  PART_SEQUENCER_NUM_STEPS,
  PART_SEQUENCER_CLOCK_QUANTIZATION,
  PART_SEQUENCER_LOOP_LENGTH,
};

enum SequencerStepFlags {
  SEQUENCER_STEP_REST = 0x80,
  SEQUENCER_STEP_TIE = 0x81
};

struct SequencerStep {
  // BYTE 0:
  // 0x00 to 0x7f: note
  // 0x80: rest
  // 0x81: tie
  //
  // BYTE 1:
  // 7 bits of velocity + 1 bit for slide flag.
  SequencerStep() { }
  SequencerStep(uint8_t data_0, uint8_t data_1) {
    data[0] = data_0;
    data[1] = data_1;
  }

  uint8_t data[2];

  inline bool has_note() const { return !(data[0] & 0x80); }
  inline bool is_rest() const { return data[0] == SEQUENCER_STEP_REST; }
  inline bool is_tie() const { return data[0] == SEQUENCER_STEP_TIE; }
  inline bool is_continuation() const { return is_tie() || is_slid(); }
  inline uint8_t note() const { return data[0] & 0x7f; }

  inline bool is_slid() const { return data[1] & 0x80; }
  inline uint8_t velocity() const { return data[1] & 0x7f; }

  inline bool is_white() const { return whiteKeyValues[note() % 12] != 0xff; }
  inline uint8_t octave() const { return note() / 12; }
  inline int8_t octaves_above_middle_c() const { return ((int8_t) octave()) - (60 / 12); }
  inline uint8_t white_key_value() const { return whiteKeyValues[note() % 12]; }
  inline uint8_t black_key_value() const { return blackKeyValues[note() % 12]; }
  inline int8_t white_key_distance_from_middle_c() const {
    return octaves_above_middle_c() * ((int8_t) kNumWhiteKeys) + white_key_value();
  }
  inline int8_t black_key_distance_from_middle_c() const {
    return octaves_above_middle_c() * ((int8_t) kNumBlackKeys) + black_key_value();
  }
};

struct SequencerSettings {
  uint8_t clock_division;
  uint8_t gate_length;
  uint8_t arp_range;
  uint8_t arp_direction;
  uint8_t arp_pattern;
  uint8_t euclidean_length;
  uint8_t euclidean_fill;
  uint8_t euclidean_rotate;
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
    packed.euclidean_rotate = euclidean_rotate;
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
    euclidean_rotate = packed.euclidean_rotate;
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
    return 60;
  }
};

struct ArpeggiatorState {
  SequencerStep step;
  uint8_t step_index;
  int8_t key_index;
  int8_t octave;
  int8_t key_increment;
  void ResetKey() {
    key_index = 0;
    octave = 0;
    key_increment = 1;
  }
};

struct PressedKeys {

  static const uint8_t VELOCITY_SUSTAIN_MASK = 0x80;

  stmlib::NoteStack<kNoteStackSize> stack;
  bool ignore_note_off_messages;
  bool release_latched_keys_on_next_note_on;
  bool sustainable[kNoteStackMapping];

  void Init() {
    stack.Init();
    ignore_note_off_messages = false;
    release_latched_keys_on_next_note_on = false;
    std::fill(
      &sustainable[0],
      &sustainable[kNoteStackMapping],
      false
    );
  }

  bool SustainableNoteOff(uint8_t pitch) {
    SetSustain(pitch);
    if (IsSustained(pitch)) { return false; }
    stack.NoteOff(pitch);
    return true;
  }

  void SetSustain(uint8_t pitch) {
    uint8_t i = stack.Find(pitch);
    if (!i || !IsSustainable(i)) { return; }
    // Flag the note so that it is removed once the sustain pedal is released.
    stack.mutable_note(i)->velocity |= VELOCITY_SUSTAIN_MASK;
  }

  bool SustainAll() {
    bool changed = false;
    for (uint8_t i = 1; i <= stack.max_size(); ++i) {
      stmlib::NoteEntry* e = stack.mutable_note(i);
      if (e->note == stmlib::NOTE_STACK_FREE_SLOT) { continue; }
      if (IsSustained(*e)) { continue; }
      e->velocity |= VELOCITY_SUSTAIN_MASK;
      changed = true;
    }
    return changed;
  }

  void SetSustainable(bool value) {
    for (uint8_t i = 1; i <= stack.max_size(); ++i) {
      if (stack.note(i).note == stmlib::NOTE_STACK_FREE_SLOT) { continue; }
      sustainable[i - 1] = value;
    }
  }

  void Clutch(bool on) {
    release_latched_keys_on_next_note_on = on;
    SetSustainable(!on);
  }

  bool IsSustainable(uint8_t index) const {
    return ignore_note_off_messages || sustainable[index - 1];
  }

  bool IsSustained(const stmlib::NoteEntry &note_entry) const {
    // If the note is flagged, it can only be released by ReleaseLatchedNotes
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
  
  // The return value indicates whether the message can be forwarded to the
  // MIDI out (soft-thru). For example, when the arpeggiator is on, NoteOn
  // or NoteOff can return false to make sure that the chord that triggers
  // the arpeggiator does not find its way to the MIDI out. Instead it will 
  // be sent note by note within InternalNoteOn and InternalNoteOff.
  //
  // Also, note that channel / keyrange / velocity range filtering is not
  // applied here. It is up to the caller to call accepts() first to check
  // whether the message should be sent to the part.
  uint8_t PressedKeysNoteOn(PressedKeys &keys, uint8_t pitch, uint8_t velocity);
  bool NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  bool NoteOff(uint8_t channel, uint8_t note);
  uint8_t TransposeInputPitch(uint8_t pitch, int8_t transpose_octaves) const {
    CONSTRAIN(transpose_octaves, (0 - pitch) / 12, (127 - pitch) / 12);
    return pitch + 12 * transpose_octaves;
  }
  uint8_t TransposeInputPitch(uint8_t pitch) const {
    return TransposeInputPitch(pitch, midi_.transpose_octaves);
  }
  void InternalNoteOn(uint8_t note, uint8_t velocity);
  void InternalNoteOff(uint8_t note);
  bool ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
  bool PitchBend(uint8_t channel, uint16_t pitch_bend);
  bool Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity);
  bool Aftertouch(uint8_t channel, uint8_t velocity);
  void AllNotesOff();
  void StopSequencerArpeggiatorNotes();
  void Reset();
  void Clock();
  void Start();
  void Stop();
  void StopRecording();
  void StartRecording();
  void DeleteSequence();
  bool new_beat() const;

  inline void NewLayout() {
    midi_.min_note = 0;
    midi_.max_note = 127;
    midi_.min_velocity = 0;
    midi_.max_velocity = 127;

    voicing_.allocation_mode = num_voices_ > 1 ? VOICE_ALLOCATION_MODE_POLY : VOICE_ALLOCATION_MODE_MONO;
    voicing_.allocation_priority = stmlib::NOTE_STACK_PRIORITY_LAST;
    voicing_.portamento = 0;
    voicing_.legato_mode = LEGATO_MODE_OFF;
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
    return looped() && (
      midi_.play_mode == PLAY_MODE_SEQUENCER ||
      midi_.play_mode == PLAY_MODE_ARPEGGIATOR
    );
  }

  inline bool uses_poly_allocator() const {
    return
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY ||
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_NICE ||
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_STEAL_MOST_RECENT;
  }

  inline bool uses_sorted_dispatch() const {
    return
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_SORTED ||
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_1 ||
      voicing_.allocation_mode == VOICE_ALLOCATION_MODE_POLY_UNISON_2;
  }

  inline void LooperPlayNoteOn(uint8_t looper_note_index, uint8_t pitch, uint8_t velocity) {
    if (!looper_in_use()) { return; }
    looper_note_index_for_generated_note_index_[generated_notes_.NoteOn(pitch, velocity)] = looper_note_index;
    pitch = ApplySequencerInputResponse(pitch);
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      // Advance arp
      arp_ = BuildArpState(SequencerStep(pitch, velocity));
      pitch = arp_.step.note();
      if (arp_.step.has_note()) {
        InternalNoteOn(pitch, arp_.step.velocity());
        if (arp_.step.is_slid()) {
          InternalNoteOff(output_pitch_for_looper_note_[looper_note_index]);
        }
        output_pitch_for_looper_note_[looper_note_index] = pitch;
      } //  else if tie, arp_pitch_for_looper_note_ is already set to the tied pitch
    } else if (looper_can_control(pitch)) {
      InternalNoteOn(pitch, velocity);
      output_pitch_for_looper_note_[looper_note_index] = pitch;
    }
  }

  inline void LooperPlayNoteOff(uint8_t looper_note_index, uint8_t pitch) {
    if (!looper_in_use()) { return; }
    looper_note_index_for_generated_note_index_[generated_notes_.NoteOff(pitch)] = looper::kNullIndex;
    pitch = output_pitch_for_looper_note_[looper_note_index];
    if (pitch == looper::kNullIndex) { return; }
    output_pitch_for_looper_note_[looper_note_index] = looper::kNullIndex;
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      // Peek at next looper note
      uint8_t next_on_index = looper_.PeekNextOn();
      const looper::Note& next_on_note = looper_.note_at(next_on_index);
      SequencerStep next_step = SequencerStep(next_on_note.pitch, next_on_note.velocity);
      next_step = BuildArpState(next_step).step;
      if (next_step.is_continuation()) {
        // Leave this pitch in the care of the next looper note
        output_pitch_for_looper_note_[next_on_index] = pitch;
      } else {
        InternalNoteOff(pitch);
      }
    } else if (looper_can_control(pitch)) {
      InternalNoteOff(pitch);
    }
  }

  inline void LooperRecordNoteOn(uint8_t pressed_key_index) {
    const stmlib::NoteEntry& e = manual_keys_.stack.note(pressed_key_index);
    uint8_t looper_note_index = looper_.RecordNoteOn(e.note, e.velocity & 0x7f);
    looper_note_recording_pressed_key_[pressed_key_index] = looper_note_index;
    LooperPlayNoteOn(looper_note_index, e.note, e.velocity & 0x7f);
  }

  inline void LooperRecordNoteOff(uint8_t pressed_key_index) {
    const stmlib::NoteEntry& e = manual_keys_.stack.note(pressed_key_index);
    uint8_t looper_note_index = looper_note_recording_pressed_key_[pressed_key_index];
    if (looper_.RecordNoteOff(looper_note_index)) {
      LooperPlayNoteOff(looper_note_index, e.note);
    }
    looper_note_recording_pressed_key_[pressed_key_index] = looper::kNullIndex;
  }

  void DeleteRecording();

  inline void SustainOn() {
    PressedKeysSustainOn(manual_keys_);
    PressedKeysSustainOn(arp_keys_);
  }
  inline void SustainOff() {
    PressedKeysSustainOff(manual_keys_);
    PressedKeysSustainOff(arp_keys_);
  }
  void PressedKeysSustainOn(PressedKeys &keys);
  void PressedKeysSustainOff(PressedKeys &keys);
  inline const PressedKeys& PressedKeysForLatchUI() const {
    return midi_.play_mode == PLAY_MODE_ARPEGGIATOR ? arp_keys_ : manual_keys_;
  }
  inline PressedKeys& MutablePressedKeysForLatchUI() {
    return midi_.play_mode == PLAY_MODE_ARPEGGIATOR ? arp_keys_ : manual_keys_;
  }
  inline void PressedKeysResetLatch(PressedKeys &keys) {
    ReleaseLatchedNotes(keys);
    keys.Init();
  }
  void ResetLatch();

  inline uint8_t ArpUndoTransposeInputPitch(uint8_t pitch) const {
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR && pitch < SEQUENCER_STEP_REST) {
      // This is an arpeggiation control step, so undo input transpose
      pitch = TransposeInputPitch(pitch, -midi_.transpose_octaves);
    }
    return pitch;
  }

  inline void RecordStep(const SequencerStep& step) {
    if (seq_recording_) {
      SequencerStep* target = &seq_.step[seq_rec_step_];
      target->data[0] = step.data[0];
      target->data[1] |= step.data[1];
      ++seq_rec_step_;
      uint8_t last_step = seq_overdubbing_ ? seq_.num_steps : kNumSteps;
      // Extend sequence.
      if (!seq_overdubbing_ && seq_rec_step_ > seq_.num_steps) {
        seq_.num_steps = seq_rec_step_;
      }
      // Wrap to first step.
      if (seq_rec_step_ >= last_step) {
        seq_rec_step_ = 0;
      }
    }
  }

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
    return midi_.channel == 0x10 ? 0 : midi_.channel;
  }
  inline bool direct_thru() const {
    return midi_.out_mode == MIDI_OUT_MODE_THRU && !polychained_;
  }
  
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
  inline uint8_t playing_step() const {
    // correct for preemptive increment
    return stmlib::modulo(seq_step_ - 1, seq_.num_steps);
  }
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
    TouchVoices();
    TouchVoiceAllocation();
    ResetLatch();
  }

  void set_siblings(bool has_siblings) {
    has_siblings_ = has_siblings;
  }
  
 private:
  int16_t Tune(int16_t note);
  void ResetAllControllers();
  void TouchVoiceAllocation();
  void TouchVoices();
  
  void ReleaseLatchedNotes(PressedKeys &keys);
  void DispatchSortedNotes(bool legato);
  void VoiceNoteOn(Voice* voice, uint8_t pitch, uint8_t vel, bool legato);
  void KillAllInstancesOfNote(uint8_t note);

  uint8_t ApplySequencerInputResponse(int16_t pitch, int8_t root_pitch = 60) const;
  const SequencerStep BuildSeqStep() const;
  const ArpeggiatorState BuildArpState(SequencerStep seq_step) const;

  MidiSettings midi_;
  VoicingSettings voicing_;
  SequencerSettings seq_;
  
  Voice* voice_[kNumMaxVoicesPerPart];
  int8_t* custom_pitch_table_;
  uint8_t num_voices_;
  bool polychained_;

  PressedKeys manual_keys_;
  PressedKeys arp_keys_;
  bool hold_pedal_engaged_;

  stmlib::NoteStack<kNoteStackSize> generated_notes_;  // by sequencer or arpeggiator.
  stmlib::NoteStack<kNoteStackSize> mono_allocator_;
  stmlib::VoiceAllocator<kNumMaxVoicesPerPart * 2> poly_allocator_;
  uint8_t active_note_[kNumMaxVoicesPerPart];
  uint8_t cyclic_allocation_note_counter_;
  
  ArpeggiatorState arp_;
  
  bool seq_recording_;
  bool seq_overdubbing_;
  uint8_t seq_step_;
  uint8_t seq_rec_step_;
  
  looper::Deck looper_;

  // Tracks which looper notes are currently being recorded
  uint8_t looper_note_recording_pressed_key_[kNoteStackMapping];

  // Tracks which looper notes are currently playing, so they can be turned off later
  uint8_t looper_note_index_for_generated_note_index_[kNoteStackMapping];

  // Post-transpose
  uint8_t output_pitch_for_looper_note_[looper::kMaxNotes];

  uint16_t gate_length_counter_;
  
  bool has_siblings_;
  
  bool multi_is_recording_;

  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace yarns

#endif // YARNS_PART_H_
