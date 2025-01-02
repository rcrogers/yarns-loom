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

#include "yarns/part.h"

#include <algorithm>
#include <stdlib.h>

#include "stmlib/midi/midi.h"
#include "stmlib/utils/random.h"

#include "yarns/just_intonation_processor.h"
#include "yarns/midi_handler.h"
#include "yarns/resources.h"
#include "yarns/voice.h"
#include "yarns/multi.h"
#include "yarns/ui.h"

namespace yarns {
  
using namespace stmlib;
using namespace stmlib_midi;
using namespace std;

void Part::Init() {
  manual_keys_.Init();
  arp_keys_.Init();
  mono_allocator_.Init();
  poly_allocator_.Init();
  generated_notes_.Init();
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
  num_voices_ = 0;
  polychained_ = false;
  seq_recording_ = false;

  looper_.Init(this);

  midi_.channel = 0;
  midi_.min_note = 0;
  midi_.max_note = 127;
  midi_.min_velocity = 0;
  midi_.max_velocity = 127;
  midi_.out_mode = MIDI_OUT_MODE_GENERATED_EVENTS;
  midi_.sustain_mode = SUSTAIN_MODE_LATCH;
  midi_.sustain_polarity = 0;
  midi_.transpose_octaves = 0;

  voicing_.allocation_priority = NOTE_STACK_PRIORITY_LAST;
  voicing_.allocation_mode = POLY_MODE_OFF;
  voicing_.legato_retrigger = true;
  voicing_.portamento_legato_only = false;
  voicing_.portamento = 0;
  voicing_.pitch_bend_range = 2;
  voicing_.vibrato_range = 1;
  voicing_.vibrato_mod = 0;
  voicing_.lfo_rate = 70;
  voicing_.lfo_spread_types = 0;
  voicing_.lfo_spread_voices = 0;
  voicing_.trigger_duration = 2;
  voicing_.aux_cv = MOD_AUX_ENVELOPE;
  voicing_.aux_cv_2 = MOD_AUX_ENVELOPE;
  voicing_.tuning_transpose = 0;
  voicing_.tuning_fine = 0;
  voicing_.tuning_root = 0;
  voicing_.tuning_system = TUNING_SYSTEM_EQUAL;
  voicing_.tuning_factor = 0;
  voicing_.oscillator_mode = OSCILLATOR_MODE_OFF;
  voicing_.oscillator_shape = OSC_SHAPE_FM;

  voicing_.timbre_initial = 64;
  voicing_.timbre_mod_velocity = 32;
  voicing_.timbre_mod_envelope = -16;
  voicing_.timbre_mod_lfo = 16;

  voicing_.amplitude_mod_velocity = 48;
  voicing_.env_init_attack = 64;
  voicing_.env_init_decay = 64;
  voicing_.env_init_sustain = 64;
  voicing_.env_init_release = 32;
  voicing_.env_mod_attack = -32;
  voicing_.env_mod_decay = -32;
  voicing_.env_mod_sustain = 0;
  voicing_.env_mod_release = 32;

  seq_.clock_division = 20;
  seq_.gate_length = 3;
  seq_.arp_range = 0;
  seq_.arp_direction = 0;
  seq_.arp_pattern = LUT_ARPEGGIATOR_PATTERNS_SIZE - 1; // Pattern 0
  midi_.input_response = SEQUENCER_INPUT_RESPONSE_TRANSPOSE;
  midi_.play_mode = PLAY_MODE_MANUAL;
  seq_.clock_quantization = 0;
  seq_.loop_length = 2; // 1 bar

  StopRecording();
  DeleteSequence();
}
  
void Part::AllocateVoices(Voice* voice, uint8_t num_voices, bool polychain) {
  AllNotesOff();
    
  num_voices_ = std::min(num_voices, kNumMaxVoicesPerPart);
  polychained_ = polychain;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i] = voice + i;
  }
  poly_allocator_.Clear();
  poly_allocator_.set_size(num_voices_ * (polychain ? 2 : 1));
  TouchVoices();
}

uint8_t Part::HeldKeysNoteOn(HeldKeys &keys, uint8_t pitch, uint8_t velocity) {
  if (keys.stop_sustained_notes_on_next_note_on) StopSustainedNotes(keys);
  return keys.stack.NoteOn(pitch, velocity);
}

void Part::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  bool sent_from_step_editor = channel & 0x80;
  
  // scale velocity to compensate for its min/max range, so that voices using
  // velocity filtering can still have a full velocity range
  velocity = ((velocity - midi_.min_velocity) << 7) / (midi_.max_velocity - midi_.min_velocity + 1);

  if (seq_recording_) {
    if (!looped() && !sent_from_step_editor) {
      RecordStep(SequencerStep(note, velocity));
    } else if (looped()) {
      uint8_t pressed_key_index = HeldKeysNoteOn(manual_keys_, note, velocity);
      LooperRecordNoteOn(pressed_key_index);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    HeldKeysNoteOn(arp_keys_, note, velocity);
  } else {
    HeldKeysNoteOn(manual_keys_, note, velocity);
    if (sent_from_step_editor || manual_control()) {
      InternalNoteOn(note, velocity);
    }
  }
}

void Part::NoteOff(uint8_t channel, uint8_t note, bool respect_sustain) {
  bool sent_from_step_editor = channel & 0x80;

  uint8_t pressed_key_index = manual_keys_.stack.Find(note);
  if (seq_recording_ && looped() && looper_is_recording(pressed_key_index)) {
    // Directly mapping pitch to looper notes would be cleaner, but requires a
    // data structure more sophisticated than an array
    LooperRecordNoteOff(pressed_key_index);
    // Sustain is respected only if it was applied before recording
    if (!manual_keys_.IsSustained(note)) {
      manual_keys_.stack.NoteOff(note);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    arp_keys_.NoteOff(note, respect_sustain);
  } else {
    bool off = manual_keys_.NoteOff(note, respect_sustain);
    if (off && (sent_from_step_editor || manual_control())) {
      InternalNoteOff(note);
    }
  }
}

void Part::HeldKeysSustainOn(HeldKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.universally_sustainable = true;
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetIndividuallySustainable(true);
      break;
    case SUSTAIN_MODE_LATCH:
    case SUSTAIN_MODE_MOMENTARY_LATCH:
      keys.Latch(true);
      break;
    case SUSTAIN_MODE_FILTER:
      keys.universally_sustainable = true;
      keys.stop_sustained_notes_on_next_note_on = false;
      break;
    case SUSTAIN_MODE_CLUTCH:
      keys.Clutch(true);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
}

void Part::HeldKeysSustainOff(HeldKeys &keys) {
  switch (midi_.sustain_mode) {
    case SUSTAIN_MODE_NORMAL:
      keys.universally_sustainable = false;
      StopSustainedNotes(keys);
      break;
    case SUSTAIN_MODE_SOSTENUTO:
      keys.SetIndividuallySustainable(false);
      StopSustainedNotes(keys);
      break;
    case SUSTAIN_MODE_LATCH:
    case SUSTAIN_MODE_MOMENTARY_LATCH:
    case SUSTAIN_MODE_FILTER:
      if (midi_.sustain_mode == SUSTAIN_MODE_MOMENTARY_LATCH) {
        StopSustainedNotes(keys);
      }
      keys.Latch(false);
      break;
    case SUSTAIN_MODE_CLUTCH:
      keys.Clutch(false);
      break;
    case SUSTAIN_MODE_OFF:
    default:
      break;
  }
}

void Part::ResetAllKeys() {
  ResetKeys(manual_keys_);
  ResetKeys(arp_keys_);
  ControlChange(0, kCCHoldPedal, hold_pedal_engaged_ ? 127 : 0);
}

void Part::ControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
  switch (controller) {
    case kCCBreathController:
    case kCCFootPedalMsb:
      for (uint8_t i = 0; i < num_voices_; ++i) {
        voice_[i]->ControlChange(controller, value);
      }
      break;
      
    case kCCOmniModeOff:
      midi_.channel = channel;
      break;
      
    case kCCOmniModeOn:
      midi_.channel = kMidiChannelOmni;
      break;
      
    case kCCMonoModeOn:
      voicing_.allocation_mode = POLY_MODE_OFF;
      TouchVoiceAllocation();
      break;
      
    case kCCPolyModeOn:
      voicing_.allocation_mode = POLY_MODE_STEAL_RELEASE_SILENT;
      TouchVoiceAllocation();
      break;
      
    case kCCHoldPedal:
      hold_pedal_engaged_ = value >= 64;
      hold_pedal_engaged_ == (midi_.sustain_polarity == 0) ?
        SustainOn() : SustainOff();
      break;

    case 0x70:
      if (looped()) {
        looper_.RemoveOldestNote();
      } else if (seq_recording_) {
        RecordStep(SEQUENCER_STEP_TIE);
      }
      break;
    
    case 0x71:
      if (looped()) {
        looper_.RemoveNewestNote();
      } else if (seq_recording_) {
        RecordStep(SEQUENCER_STEP_REST);
      }
      break;

    case 0x78: // All Sound Off
      AllNotesOff();
      break;
      
    case 0x79:
      ResetAllControllers();
      break;
      
    case 0x7b: // All Notes Off
      AllNotesOff();
      break;
  }
}

void Part::PitchBend(uint8_t channel, uint16_t pitch_bend) {
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->PitchBend(pitch_bend);
  }
  
  if (seq_recording_ &&
      (pitch_bend > 8192 + 2048 || pitch_bend < 8192 - 2048)) {
    seq_.step[seq_rec_step_].set_slid(true);
  }
}

void Part::Aftertouch(uint8_t channel, uint8_t note, uint8_t velocity) {
  if (voicing_.allocation_mode != POLY_MODE_OFF) {
    uint8_t voice_index = \
        uses_poly_allocator() ? \
        poly_allocator_.Find(note) : \
        FindVoiceForNote(note);
    if (voice_index < poly_allocator_.size()) {
      voice_[voice_index]->Aftertouch(velocity);
    }
  } else {
    Aftertouch(channel, velocity);
  }
}

void Part::Aftertouch(uint8_t channel, uint8_t velocity) {
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->Aftertouch(velocity);
  }
}

void Part::Reset() {
  AllNotesOff();
  ResetAllControllers();
}

bool Part::current_step_has_swing() const {
  if (!multi.settings().clock_swing) return false;

  uint32_t step_counter = ticks_to_steps(multi.tick_counter());
  bool swing_even = multi.settings().clock_swing >= 0;
  bool step_even = step_counter % 2 == 1;
  return swing_even == step_even;
}

void Part::ClockStep() {
  step_counter_ = ticks_to_steps(multi.tick_counter());
  // Reset sequencer-driven arpeggiator (step or loop), if needed
  //
  // NB: when using looper, this produces predictable changes in the arp
  // output (i.e., resets the arp at a predictable point in the loop) IFF the
  // looper's LFO is locked onto the clock's phase and frequency. Clocking
  // changes may break the lock, and briefly cause mistimed arp resets
  if (arp_should_reset_on_step(step_counter_)) arpeggiator_.Reset();

  // The rest of the method is only for the step sequencer and/or arpeggiator
  if (!doing_stepped_stuff()) return;

  SequencerArpeggiatorResult result = BuildNextStepResult(step_counter_);
  arpeggiator_ = result.arpeggiator;
  if (result.note.has_note()) {
    uint8_t pitch = result.note.note();
    uint8_t velocity = result.note.velocity();
    GeneratedNoteOff(pitch); // Simulate a human retriggering a key
    if (GeneratedNoteOn(pitch, velocity) && !manual_keys_.stack.Find(pitch)) {
      InternalNoteOn(pitch, velocity, result.note.is_slid());
    }
  }
}

SequencerArpeggiatorResult Part::BuildNextStepResult(uint32_t step_counter) const {
  // In case of early return, the arp does not advance, and the note is a REST
  SequencerArpeggiatorResult result = {
    arpeggiator_, SequencerStep(SEQUENCER_STEP_REST, 0),
  };

  if (seq_.euclidean_length != 0) {
    // If euclidean rhythm is enabled, advance euclidean state
    uint32_t pattern_mask = 1 << (step_counter % seq_.euclidean_length);
    // Read euclidean pattern from ROM.
    uint16_t offset = static_cast<uint16_t>(seq_.euclidean_length - 1) << 5;
    uint32_t pattern = lut_euclidean[offset + seq_.euclidean_fill];
    if (!(pattern_mask & pattern)) return result; // If skipping this beat, early return
  }

  // Advance sequencer and arpeggiator state
  if (seq_.num_steps) {
    result.note = BuildSeqStep(step_counter % seq_.num_steps);
  }
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    // If seq-driven and there are no steps, early return
    if (seq_driven_arp() && !seq_.num_steps) return result;
    if (arp_should_reset_on_step(step_counter)) result.arpeggiator.Reset();
    result = result.arpeggiator.BuildNextResult(*this, arp_keys_, step_counter, result.note);
  }
  return result;
}

void Part::ClockStepGateEndings() {
  for (uint8_t v = 0; v < num_voices_; ++v) {
    if (gate_length_counter_[v]) { // Gate hasn't ended yet
      --gate_length_counter_[v];
      continue;
    }
    // Peek at next step to see if it's a continuation
    // If more than one voice has a step ending, the peek is redundant
    SequencerStep next_step = BuildNextStepResult(step_counter_ + 1).note;
    if (next_step.is_continuation()) {
      // The next step contains a "sustain" message; or a slid note. Extends
      // the duration of the current note.
      gate_length_counter_[v] += PPQN();
    } else if (active_note_[v] != VOICE_ALLOCATION_NOT_FOUND) {
      GeneratedNoteOff(active_note_[v]);
    }
  }
}

void Part::Start() {
  arpeggiator_.Reset();

  // Fast-forward the sequencer/arpeggiator state to the current song position.
  // If using the sequencer-driven arpeggiator, produces the cumulative arp
  // state based on any held keys
  if (looper_in_use()) {
    // First, move to the looper's start position, without side effects
    looper_.JumpToTick(0, NULL, NULL);

    // Don't generate side effects for negative ticks
    int32_t ticks = std::max(static_cast<int32_t>(0), multi.tick_counter(1));

    NoteOnFn on_fn = midi_.play_mode == PLAY_MODE_ARPEGGIATOR ? &Part::AdvanceArpForLooperNoteOnWithoutReturn : NULL;
    div_t cycles = std::div(static_cast<uint32_t>(ticks), looper_.period_ticks());
    for (uint16_t i = 0; i <= cycles.quot; i++) {
      if (i % sequence_repeats_per_arp_reset() == 0) arpeggiator_.Reset();

      uint16_t cycle_ticks = i < cycles.quot ? looper_.period_ticks() : cycles.rem;
      if (!cycle_ticks) continue; // Remainder is zero

      looper_.JumpToTick(cycle_ticks, on_fn, NULL);
    }
  } else if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    // The only state produced by the step sequencer is the arp
    int16_t last_step_triggered = seq_.step_offset + DIV_FLOOR(multi.tick_counter(), PPQN());
    uint16_t arp_reset_steps = steps_per_arp_reset();
    // NOOP if the last step triggered is less than 0 -- can't predict arp states before 0
    for (uint16_t step = 0; step <= last_step_triggered; step++) {
      if (arp_reset_steps && step % arp_reset_steps == 0) arpeggiator_.Reset();
      SequencerArpeggiatorResult result = BuildNextStepResult(step);
      arpeggiator_ = result.arpeggiator;
    }
  }

  // Reset state for notes being actively output or recorded
  std::fill(
    &looper_note_recording_pressed_key_[0],
    &looper_note_recording_pressed_key_[kNoteStackMapping],
    looper::kNullIndex
  );
  std::fill(
    &looper_note_index_for_generated_note_index_[0],
    &looper_note_index_for_generated_note_index_[kNoteStackMapping],
    looper::kNullIndex
  );
  std::fill(
    &output_pitch_for_looper_note_[0],
    &output_pitch_for_looper_note_[kNoteStackMapping],
    looper::kNullIndex
  );
  generated_notes_.Clear();
}

void Part::StopRecording() {
  if (!seq_recording_) { return; }
  seq_recording_ = false;
  if (looped()) {
    // Stop recording any held notes
    for (uint8_t i = 1; i <= manual_keys_.stack.max_size(); ++i) {
      const NoteEntry& e = manual_keys_.stack.note(i);
      if (e.note == NOTE_STACK_FREE_SLOT) { continue; }
      // This could be a transpose key that was held before StartRecording
      if (!looper_is_recording(i)) { continue; }
      LooperRecordNoteOff(i);
    }
  }
}

void Part::StartRecording() {
  if (seq_recording_) {
    return;
  }
  seq_recording_ = true;
  if (looped() && manual_control()) {
    // Start recording any held notes
    for (uint8_t i = 1; i <= manual_keys_.stack.max_size(); ++i) {
      const NoteEntry& e = manual_keys_.stack.note(i);
      if (
        e.note == NOTE_STACK_FREE_SLOT ||
        manual_keys_.IsSustained(e)
      ) { continue; }
      LooperRecordNoteOn(i);
    }
  } else {
    seq_rec_step_ = 0;
    seq_overdubbing_ = seq_.num_steps > 0;
  }
}

void Part::DeleteRecording() {
  if (midi_.play_mode == PLAY_MODE_MANUAL) { return; }
  StopSequencerArpeggiatorNotes();
  looped() ? looper_.RemoveAll() : DeleteSequence();
  seq_overwrite_ = false;
}

void Part::DeleteSequence() {
  std::fill(
    &seq_.step[0],
    &seq_.step[kNumSteps],
    SequencerStep(SEQUENCER_STEP_REST, 0)
  );
  seq_rec_step_ = 0;
  seq_.num_steps = 0;
  seq_overdubbing_ = false;
}

uint8_t Part::GeneratedNoteOn(uint8_t pitch, uint8_t velocity) {
  if (
    mono_allocator_.size() == mono_allocator_.max_size() ||
    generated_notes_.size() == generated_notes_.max_size()
  ) {
    return 0;
  }
  return generated_notes_.NoteOn(pitch, velocity);
}

void Part::StopSequencerArpeggiatorNotes() {
  while (generated_notes_.most_recent_note_index()) {
    GeneratedNoteOff(generated_notes_.most_recent_note().note);
  }
}

void Part::GeneratedNoteOff(uint8_t pitch) {
  uint8_t generated_note_index = generated_notes_.Find(pitch);
  uint8_t looper_note_index = looper_note_index_for_generated_note_index_[generated_note_index];
  looper_note_index_for_generated_note_index_[generated_note_index] = looper::kNullIndex;
  generated_notes_.NoteOff(pitch);
  if (looper_in_use()) {
    if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
      pitch = output_pitch_for_looper_note_[looper_note_index];
    }
    if (!looper_can_control(pitch)) return;
  } else if (manual_keys_.stack.Find(pitch)) return;
  InternalNoteOff(pitch);
}

uint8_t Part::ApplySequencerInputResponse(int16_t pitch, int8_t root_pitch) const {
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    return pitch;
  }

  // Find the most recent manual key that isn't being used to record
  uint8_t transpose_key = manual_keys_.stack.most_recent_note_index();
  while (transpose_key && looper_is_recording(transpose_key)) {
    transpose_key = manual_keys_.stack.note(transpose_key).next_ptr;
  }
  if (!transpose_key) { return pitch; }

  uint8_t transpose_pitch = manual_keys_.stack.note(transpose_key).note;
  switch (midi_.input_response) {
    case SEQUENCER_INPUT_RESPONSE_TRANSPOSE:
      pitch += transpose_pitch - root_pitch;
      while (pitch > 127) { pitch -= 12; }
      while (pitch < 0) { pitch += 12; }
      break;

    case SEQUENCER_INPUT_RESPONSE_REPLACE:
      pitch = transpose_pitch;
      break;

    case SEQUENCER_INPUT_RESPONSE_DIRECT:
    case SEQUENCER_INPUT_RESPONSE_OFF:
      break;
  }
  return pitch;
}

const SequencerStep Part::BuildSeqStep(uint8_t step_index) const {
  const SequencerStep& step = seq_.step[step_index];
  int16_t note = step.note();
  if (step.has_note()) {
    // When we play a monophonic sequence, we can make the guess that root
    // note = first note.
    // But this is not the case when we are playing several sequences at the
    // same time. In this case, we use root note = 60.
    int8_t root_note = !has_siblings_ ? seq_.first_note() : kC4;
    note = ApplySequencerInputResponse(note, root_note);
  }
  return SequencerStep((0x80 & step.data[0]) | (0x7f & note), step.data[1]);
}

void Part::RecordStep(const SequencerStep& step) {
  if (!seq_recording_) return;

  if (seq_overwrite_) { DeleteRecording(); }
  SequencerStep* target = &seq_.step[seq_rec_step_];
  target->data[0] = step.data[0];
  target->data[1] |= step.data[1];
  if (!target->has_note()) target->set_slid(false);
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

void Part::LooperPlayNoteOn(uint8_t looper_note_index, uint8_t pitch, uint8_t velocity) {
  if (!looper_in_use()) return;
  uint8_t generated_note_index = GeneratedNoteOn(pitch, velocity);
  if (!generated_note_index) return;
  looper_note_index_for_generated_note_index_[generated_note_index] = looper_note_index;
  pitch = ApplySequencerInputResponse(pitch);
  if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR) {
    // Advance arp
    SequencerArpeggiatorResult result = AdvanceArpForLooperNoteOn(pitch, velocity);
    arpeggiator_ = result.arpeggiator;
    pitch = result.note.note();
    if (result.note.has_note()) {
      bool slide = result.note.is_slid();
      InternalNoteOn(pitch, result.note.velocity(), slide);
      if (slide) {
        // NB: currently impossible (see LooperPlayNoteOff)
        InternalNoteOff(output_pitch_for_looper_note_[looper_note_index]);
      }
      output_pitch_for_looper_note_[looper_note_index] = pitch;
    } //  else if tie, output_pitch_for_looper_note_ is already set to the tied pitch
  } else if (looper_can_control(pitch)) {
    InternalNoteOn(pitch, velocity);
    output_pitch_for_looper_note_[looper_note_index] = pitch;
  }
}

void Part::LooperPlayNoteOff(uint8_t looper_note_index, uint8_t pitch) {
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
    // Predicting whether the looper will have looped around by this next note
    // (and possibly caused an arp reset) is hard, but fortunately, a reset
    // does not currently affect whether the arp output note is a
    // continuation, so we don't care
    //
    // Also NB: step_counter_ doesn't matter (see LooperPlayNoteOn)
    next_step = arpeggiator_.BuildNextResult(*this, arp_keys_, 0, next_step).note;
    if (next_step.is_continuation()) {
      // Leave this pitch in the care of the next looper note
      //
      // NB: currently impossible, since the arp can only return a
      // continuation when driven by an input sequencer note that is a
      // continuation, which the looper can't do
      output_pitch_for_looper_note_[next_on_index] = pitch;
    } else {
      InternalNoteOff(pitch);
    }
  } else if (looper_can_control(pitch)) {
    InternalNoteOff(pitch);
  }
}

void Part::LooperRecordNoteOn(uint8_t pressed_key_index) {
  if (seq_overwrite_) { DeleteRecording(); }
  const stmlib::NoteEntry& e = manual_keys_.stack.note(pressed_key_index);
  uint8_t looper_note_index = looper_.RecordNoteOn(e.note, e.velocity & 0x7f);
  looper_note_recording_pressed_key_[pressed_key_index] = looper_note_index;
  LooperPlayNoteOn(looper_note_index, e.note, e.velocity & 0x7f);
}

void Part::LooperRecordNoteOff(uint8_t pressed_key_index) {
  const stmlib::NoteEntry& e = manual_keys_.stack.note(pressed_key_index);
  uint8_t looper_note_index = looper_note_recording_pressed_key_[pressed_key_index];
  if (looper_.RecordNoteOff(looper_note_index)) {
    LooperPlayNoteOff(looper_note_index, e.note);
  }
  looper_note_recording_pressed_key_[pressed_key_index] = looper::kNullIndex;
}

void Part::ResetAllControllers() {
  ResetAllKeys();
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->ResetAllControllers();
  }
}

void Part::AllNotesOff() {
  poly_allocator_.ClearNotes();
  mono_allocator_.Clear();

  ResetAllKeys();

  generated_notes_.Clear();
  looper_note_index_for_generated_note_index_[generated_notes_.most_recent_note_index()] = looper::kNullIndex;
  for (uint8_t i = 0; i < num_voices_; ++i) {
    VoiceNoteOff(i);
  }
  std::fill(
      &active_note_[0],
      &active_note_[kNumMaxVoicesPerPart],
      VOICE_ALLOCATION_NOT_FOUND);
}

void Part::StopNotesBySustainStatus(HeldKeys &keys, bool sustain_status) {
  for (uint8_t i = 1; i <= keys.stack.max_size(); ++i) {
    const NoteEntry& e = keys.stack.note(i);
    if (e.note == NOTE_STACK_FREE_SLOT) continue;
    if (keys.IsSustained(e) != sustain_status) continue;
    NoteOff(tx_channel(), e.note, false);
  }
}

struct DispatchNote {
  NoteEntry const* note;
  bool done;
};

void Part::DispatchSortedNotes(bool via_note_off) {
  uint8_t num_notes = mono_allocator_.size();
  uint8_t num_dispatch = num_voices_;
  bool unison = voicing_.allocation_mode != POLY_MODE_SORTED;
  if (!unison) { num_dispatch = std::min(num_dispatch, num_notes); }

  // Set up structures to track assignments
  DispatchNote dispatch[num_dispatch];
  for (uint8_t d = 0; d < num_dispatch; ++d) {
    dispatch[d].note = &priority_note(d % num_notes);
    dispatch[d].done = false;
  }
  bool voice_intact[num_voices_];
  std::fill(&voice_intact[0], &voice_intact[num_voices_], false);

  // First pass: find voices that don't need to change
  for (uint8_t v = 0; v < num_voices_; ++v) {
    for (uint8_t d = 0; d < num_dispatch; ++d) {
      if (dispatch[d].done) { continue; }
      if (active_note_[v] != dispatch[d].note->note) { continue; }
      dispatch[d].done = true;
      voice_intact[v] = true;
      break; // Voice keeps its current note
    }
  }
  // Second pass: change remaining voices
  for (uint8_t v = 0; v < num_voices_; ++v) {
    if (voice_intact[v]) { continue; }
    const NoteEntry* note = NULL;
    for (uint8_t d = 0; d < num_dispatch; ++d) {
      if (dispatch[d].done) { continue; }
      dispatch[d].done = true;
      note = dispatch[d].note;
      break; // Voice gets this note
    }
    if (note) {
      VoiceNoteOn(v, note->note, note->velocity, via_note_off, !via_note_off);
    } else if (active_note_[v] != VOICE_ALLOCATION_NOT_FOUND) {
      VoiceNoteOff(v);
    }
  }
}

void Part::VoiceNoteOn(
  uint8_t voice_index, uint8_t pitch, uint8_t vel,
  bool legato, bool reset_gate_counter
) {
  uint8_t portamento = legato || !voicing_.portamento_legato_only ?
    voicing_.portamento : 0;
  bool trigger = !legato || voicing_.legato_retrigger;

  // If this pitch is under manual control, don't extend the gate
  if (reset_gate_counter && !manual_keys_.stack.Find(pitch)) {
    gate_length_counter_[voice_index] = gate_length();
  }
  active_note_[voice_index] = pitch;
  Voice* voice = voice_[voice_index];

  int32_t timbre_14 = (voicing_.timbre_mod_envelope << 7) + vel * voicing_.timbre_mod_velocity;
  CONSTRAIN(timbre_14, -1 << 13, (1 << 13) - 1)

  uint16_t vel_concave_up = UINT16_MAX - lut_env_expo[((127 - vel) << 1)];
  int32_t damping_22 = -voicing_.amplitude_mod_velocity * vel_concave_up;
  if (voicing_.amplitude_mod_velocity >= 0) {
    damping_22 += voicing_.amplitude_mod_velocity << 16;
  }

  ADSR adsr;
  adsr.peak = UINT16_MAX - (damping_22 >> (22 - 16));
  adsr.sustain = modulate_7_13(voicing_.env_init_sustain, voicing_.env_mod_sustain, vel) << (16 - 13);
  adsr.attack   = Interpolate88(lut_envelope_phase_increments,
    modulate_7_13(voicing_.env_init_attack  , voicing_.env_mod_attack , vel) << 2);
  adsr.decay    = Interpolate88(lut_envelope_phase_increments,
    modulate_7_13(voicing_.env_init_decay   , voicing_.env_mod_decay  , vel) << 2);
  adsr.release  = Interpolate88(lut_envelope_phase_increments,
    modulate_7_13(voicing_.env_init_release , voicing_.env_mod_release, vel) << 2);

  voice->NoteOn(Tune(pitch), vel, portamento, trigger, adsr, timbre_14 << 2);
}

void Part::VoiceNoteOff(uint8_t voice) {
  voice_[voice]->NoteOff();
  active_note_[voice] = VOICE_ALLOCATION_NOT_FOUND;
}

void Part::InternalNoteOn(uint8_t note, uint8_t velocity, bool force_legato) {
  if (midi_.out_mode == MIDI_OUT_MODE_GENERATED_EVENTS && !polychained_) {
    midi_handler.OnInternalNoteOn(tx_channel(), note, velocity);
  }
  
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOn(note, velocity);
  const NoteEntry& after = priority_note();
  if (voicing_.allocation_mode == POLY_MODE_OFF) {
    bool stealing = mono_allocator_.size() > 1;
    // If a previous note was a sequencer step tie/slide, it will have skipped
    // its normal ending, so we end all generated notes except the new note
    for (uint8_t i = 1; i <= generated_notes_.max_size(); ++i) {
      if (generated_notes_.note(i).note != after.note) {
        GeneratedNoteOff(generated_notes_.note(i).note);
      }
    }
    // Check if the note that has been played should be triggered according
    // to selected voice priority rules.
    if (before.note != after.note) {
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOn(i, after.note, after.velocity, force_legato || stealing, true);
      }
    }
  } else if (uses_sorted_dispatch()) {
    DispatchSortedNotes(false);
  } else {
    uint8_t voice_index = 0;
    switch (voicing_.allocation_mode) {
      case POLY_MODE_STEAL_RELEASE_SILENT:
      case POLY_MODE_STEAL_RELEASE_REASSIGN:
      case POLY_MODE_STEAL_HIGHEST_PRIORITY: {
        bool note_justifies_steal = mono_allocator_.priority_for_note(
          static_cast<stmlib::NoteStackFlags>(voicing_.allocation_priority),
          note
        ) < num_voices_;
        uint8_t note_to_steal_voice_from =
          voicing_.allocation_mode == POLY_MODE_STEAL_HIGHEST_PRIORITY
          ? before.note // Highest priority before this note
          : priority_note(num_voices_).note; // Note that just got deprioritized
        uint8_t stealable_voice_index = note_justifies_steal
          ? FindVoiceForNote(note_to_steal_voice_from) : NOT_ALLOCATED;
        voice_index = poly_allocator_.NoteOn(note, stealable_voice_index);
        if (voice_index == NOT_ALLOCATED) return;
        break;
      }
        
      case POLY_MODE_CYCLIC:
        if (cyclic_allocation_note_counter_ >= num_voices_) {
          cyclic_allocation_note_counter_ = 0;
        }
        voice_index = cyclic_allocation_note_counter_;
        ++cyclic_allocation_note_counter_;
        break;
      
      case POLY_MODE_RANDOM:
        voice_index = (Random::GetWord() >> 24) % num_voices_;
        break;
        
      case POLY_MODE_VELOCITY:
        voice_index = (static_cast<uint16_t>(velocity) * num_voices_) >> 7;
        break;
        
      default:
        break;
    }
    
    if (voice_index < num_voices_) {
      // Prevent the same note from being simultaneously played on two channels.
      KillAllInstancesOfNote(note);
      bool stealing = active_note_[voice_index] != VOICE_ALLOCATION_NOT_FOUND;
      VoiceNoteOn(voice_index, note, velocity, force_legato || stealing, true);
    } else {
      // Polychaining forwarding.
      midi_handler.OnInternalNoteOn(tx_channel(), note, velocity);
    }
  }
}

void Part::KillAllInstancesOfNote(uint8_t note) {
  while (true) {
    uint8_t index = FindVoiceForNote(note);
    if (index != VOICE_ALLOCATION_NOT_FOUND) {
      VoiceNoteOff(index);
    } else {
      break;
    }
  }
}

void Part::InternalNoteOff(uint8_t note) {
  if (midi_.out_mode == MIDI_OUT_MODE_GENERATED_EVENTS && !polychained_) {
    midi_handler.OnInternalNoteOff(tx_channel(), note);
  }
  
  if (voicing_.tuning_system == TUNING_SYSTEM_JUST_INTONATION) {
    just_intonation_processor.NoteOff(note);
  }
  
  bool had_unvoiced_notes = mono_allocator_.size() > num_voices_;
  const NoteEntry& before = priority_note();
  mono_allocator_.NoteOff(note);
  const NoteEntry& after = priority_note();
  if (voicing_.allocation_mode == POLY_MODE_OFF) {
    if (mono_allocator_.size() == 0) {
      // No key is pressed, we just close the gate.
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOff(i);
      }
    } else if (before.note != after.note) {
      // Removing the note gives priority to another note that is still held
      for (uint8_t i = 0; i < num_voices_; ++i) {
        VoiceNoteOn(i, after.note, after.velocity, true, false);
      }
    }
  } else if (uses_sorted_dispatch()) {
    KillAllInstancesOfNote(note);
    if (
      voicing_.allocation_mode == POLY_MODE_UNISON_RELEASE_REASSIGN ||
      had_unvoiced_notes
    ) {
      DispatchSortedNotes(true);
    }
  } else {
    uint8_t voice_index = \
        uses_poly_allocator() ? \
        poly_allocator_.NoteOff(note) : \
        FindVoiceForNote(note);
    if (voice_index < num_voices_) {
      VoiceNoteOff(voice_index);
      if (
        had_unvoiced_notes &&
        (voicing_.allocation_mode == POLY_MODE_STEAL_RELEASE_REASSIGN ||
          voicing_.allocation_mode == POLY_MODE_STEAL_HIGHEST_PRIORITY)
      ) { // Reassign freed voice to the highest-priority note that is not voiced
        const NoteEntry* priority_unvoiced_note = &after; // Just for initialization
        for (uint8_t i = 0; i < mono_allocator_.size(); ++i) {
          priority_unvoiced_note = &priority_note(i);
          if (FindVoiceForNote(priority_unvoiced_note->note) == VOICE_ALLOCATION_NOT_FOUND) break;
        }
        poly_allocator_.NoteOn(priority_unvoiced_note->note, NOT_ALLOCATED);
        VoiceNoteOn(voice_index, priority_unvoiced_note->note, priority_unvoiced_note->velocity, true, false);
      }
    } else {
       midi_handler.OnInternalNoteOff(tx_channel(), note);
    }
  }
}

void Part::TouchVoiceAllocation() {
  AllNotesOff();
  ResetAllControllers();
}

void Part::TouchVoices() {
  CONSTRAIN(voicing_.aux_cv, 0, MOD_AUX_LAST - 1);
  CONSTRAIN(voicing_.aux_cv_2, 0, MOD_AUX_LAST - 1);
  for (uint8_t i = 0; i < num_voices_; ++i) {
    voice_[i]->garbage(0);
    voice_[i]->set_pitch_bend_range(voicing_.pitch_bend_range);
    voice_[i]->set_vibrato_range(voicing_.vibrato_range);
    voice_[i]->set_vibrato_mod(voicing_.vibrato_mod);
    voice_[i]->set_tremolo_mod(voicing_.tremolo_mod);
    voice_[i]->set_lfo_shape(LFO_ROLE_PITCH, voicing_.vibrato_shape);
    voice_[i]->set_lfo_shape(LFO_ROLE_TIMBRE, voicing_.timbre_lfo_shape);
    voice_[i]->set_lfo_shape(LFO_ROLE_AMPLITUDE, voicing_.tremolo_shape);
    voice_[i]->set_trigger_duration(voicing_.trigger_duration);
    voice_[i]->set_trigger_scale(voicing_.trigger_scale);
    voice_[i]->set_trigger_shape(voicing_.trigger_shape);
    voice_[i]->set_aux_cv(voicing_.aux_cv);
    voice_[i]->set_aux_cv_2(voicing_.aux_cv_2);
    voice_[i]->set_oscillator_mode(voicing_.oscillator_mode);
    voice_[i]->set_oscillator_shape(voicing_.oscillator_shape);
    voice_[i]->set_tuning(voicing_.tuning_transpose, voicing_.tuning_fine);
    voice_[i]->set_timbre_init(voicing_.timbre_initial);
    voice_[i]->set_timbre_mod_lfo(voicing_.timbre_mod_lfo);
  }
}

bool Part::Set(uint8_t address, uint8_t value) {
  uint8_t* bytes;
  bytes = static_cast<uint8_t*>(static_cast<void*>(&midi_));
  uint8_t previous_value = bytes[address];
  bytes[address] = value;
  if (value == previous_value) { return false; }
  switch (address) {
    case PART_MIDI_CHANNEL:
    case PART_MIDI_MIN_NOTE:
    case PART_MIDI_MAX_NOTE:
    case PART_MIDI_MIN_VELOCITY:
    case PART_MIDI_MAX_VELOCITY:
    case PART_MIDI_INPUT_RESPONSE:
    case PART_MIDI_PLAY_MODE:
      // Shut all channels off when a MIDI parameter is changed to prevent
      // stuck notes.
      AllNotesOff();
      break;

    case PART_MIDI_TRANSPOSE_OCTAVES:
      // Release notes that are currently under direct manual control, sparing
      // notes that are controlled by sustain or the sequencer
      StopNotesBySustainStatus(manual_keys_, false);
      StopNotesBySustainStatus(arp_keys_, false);
      break;

    case PART_VOICING_ALLOCATION_MODE:
      TouchVoiceAllocation();
      break;
      
    case PART_VOICING_PITCH_BEND_RANGE:
    case PART_VOICING_LFO_RATE:
    case PART_VOICING_VIBRATO_RANGE:
    case PART_VOICING_VIBRATO_MOD:
    case PART_VOICING_TREMOLO_MOD:
    case PART_VOICING_VIBRATO_SHAPE:
    case PART_VOICING_TIMBRE_LFO_SHAPE:
    case PART_VOICING_TREMOLO_SHAPE:
    case PART_VOICING_TRIGGER_DURATION:
    case PART_VOICING_TRIGGER_SHAPE:
    case PART_VOICING_TRIGGER_SCALE:
    case PART_VOICING_AUX_CV:
    case PART_VOICING_AUX_CV_2:
    case PART_VOICING_OSCILLATOR_SHAPE:
    case PART_VOICING_TIMBRE_INIT:
    case PART_VOICING_TIMBRE_MOD_LFO:
    case PART_VOICING_TUNING_TRANSPOSE:
    case PART_VOICING_TUNING_FINE:
      TouchVoices();
      break;
      
    case PART_SEQUENCER_ARP_DIRECTION:
      arpeggiator_.key_increment = 1;
      break;

    case PART_SEQUENCER_ARP_PATTERN:
      if (midi_.play_mode == PLAY_MODE_ARPEGGIATOR &&
        // If changing seq_driven_arp
        (previous_value >= LUT_ARPEGGIATOR_PATTERNS_SIZE) !=
        (value >= LUT_ARPEGGIATOR_PATTERNS_SIZE)
      ) StopSequencerArpeggiatorNotes();
      break;

    case PART_MIDI_SUSTAIN_MODE:
    case PART_MIDI_SUSTAIN_POLARITY:
      AllNotesOff();
      break;

    case PART_VOICING_OSCILLATOR_MODE:
      AllNotesOff();
      TouchVoices();
      break;

    default:
      break;
  }
  return true;
}

struct Ratio { int p; int q; };

const Ratio ratio_table[] = {
  { 1, 1 },
  { 0, 1 },
  { 1, 8 },
  { 1, 4 },
  { 3, 8 },
  { 1, 2 },
  { 5, 8 },
  { 3, 4 },
  { 7, 8 },
  { 1, 1 },
  { 5, 4 },
  { 3, 2 },
  { 2, 1 },
  { 51095, 65536 }
};

int16_t Part::Tune(int16_t midi_note) {
  int16_t note = midi_note;
  int16_t pitch = note << 7;
  uint8_t pitch_class = (note + 240) % 12;

  // Just intonation.
  if (voicing_.tuning_system == TUNING_SYSTEM_JUST_INTONATION) {
    pitch = just_intonation_processor.NoteOn(note);
  } else if (voicing_.tuning_system == TUNING_SYSTEM_CUSTOM) {
    pitch += custom_pitch_table_[pitch_class];
  } else if (voicing_.tuning_system > TUNING_SYSTEM_JUST_INTONATION) {
    note -= voicing_.tuning_root;
    pitch_class = (note + 240) % 12;
    pitch += lookup_table_signed_table[LUT_SCALE_PYTHAGOREAN + \
        voicing_.tuning_system - TUNING_SYSTEM_PYTHAGOREAN][pitch_class];
  }
  
  int32_t root = (static_cast<int32_t>(voicing_.tuning_root) + 60) << 7;
  int32_t scaled_pitch = static_cast<int32_t>(pitch);
  scaled_pitch -= root;
  Ratio r = ratio_table[voicing_.tuning_factor];
  scaled_pitch = scaled_pitch * r.p / r.q;
  scaled_pitch += root;
  CONSTRAIN(scaled_pitch, 0, 16383);
  return static_cast<int16_t>(scaled_pitch);
}

}  // namespace yarns
