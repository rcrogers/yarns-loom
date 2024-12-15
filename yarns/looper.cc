// Copyright 2019 Chris Rogers.
//
// Author: Chris Rogers (teukros@gmail.com)
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
// Looper.

#include "yarns/looper.h"

#include "yarns/resources.h"
#include "yarns/multi.h"
#include "yarns/part.h"

namespace yarns {

namespace looper {

void Deck::Init(Part* part) {
  part_ = part;
  RemoveAll();
  SetPhase(ComputeTargetPhaseWithOffset(0));
}

void Deck::RemoveAll() {
  for (uint8_t i = 0; i < size_; ++i) {
    KillNote(index_mod(oldest_index_ + i));
  }

  std::fill(
    &notes_[0],
    &notes_[kMaxNotes],
    Note()
  );
  latest_event_ = NULL;
  oldest_index_ = 0;
  size_ = 0;

  std::fill(
    &after_on_[0],
    &after_on_[kMaxNotes],
    Event()
  );
  std::fill(
    &after_off_[0],
    &after_off_[kMaxNotes],
    Event()
  );
}

void Deck::SetPhase(uint32_t phase) {
  lfo_.SetPhase(phase);
  if (multi.internal_clock()) {
    // A stored LFO increment may have been invalidated by changes to clock
    // settings (leading to a glitchy Start, esp if clock has slowed), so we
    // preemptively update it
    lfo_.SetPhaseIncrement(multi.phase_increment_for_tick_at_tempo() / period_ticks());
  }
  ProcessNotes(phase >> 16, NULL, NULL);
}

void Deck::Unpack(PackedPart& storage) {
  RemoveAll();
  oldest_index_ = storage.looper_oldest_index;
  size_ = storage.looper_size;
  for (uint8_t ordinal = 0; ordinal < kMaxNotes; ++ordinal) {
    uint8_t index = index_mod(oldest_index_ + ordinal);
    PackedNote& packed_note = storage.looper_notes[index];
    Note& note = notes_[index];

    note.on_pos   = packed_note.on_pos  << (16 - kBitsPos);
    note.off_pos  = packed_note.off_pos << (16 - kBitsPos);
    note.pitch    = packed_note.pitch;
    note.velocity = packed_note.velocity;

    if (ordinal < size_) {
      ProcessNotes(note.on_pos, NULL, NULL);
      AddEvent((Event){ index, true });
      ProcessNotes(note.off_pos, NULL, NULL);
      AddEvent((Event){ index, false });
    }
  }
}

void Deck::Pack(PackedPart& storage) const {
  storage.looper_oldest_index = oldest_index_;
  storage.looper_size = size_;
  for (uint8_t ordinal = 0; ordinal < kMaxNotes; ++ordinal) {
    uint8_t index = index_mod(oldest_index_ + ordinal);
    PackedNote& packed_note = storage.looper_notes[index];
    const Note& note = notes_[index];

    packed_note.on_pos    = (note.on_pos  - pos_offset) >> (16 - kBitsPos);
    packed_note.off_pos   = (note.off_pos - pos_offset) >> (16 - kBitsPos);
    packed_note.pitch     = note.pitch;
    packed_note.velocity  = note.velocity;
  }
}

uint16_t Deck::period_ticks() const {
  return part_->PPQN() << part_->sequencer_settings().loop_length;
}

uint32_t Deck::lfo_note_phase() const {
  return lfo_.GetPhase() << part_->sequencer_settings().loop_length;
}

uint32_t Deck::ComputeTargetPhaseWithOffset(int32_t tick_counter) const {
  return lfo_.ComputeTargetPhase(tick_counter, period_ticks(), pos_offset << 16);
}

void Deck::SetTargetPhase(uint32_t phase) {
  lfo_.SetTargetPhase(phase);
}

void Deck::RemoveOldestNote() {
  RemoveNote(oldest_index_);
  if (size_) {
    oldest_index_ = index_mod(oldest_index_ + 1);
  }
}

void Deck::RemoveNewestNote() {
  RemoveNote(index_mod(oldest_index_ + size_ - 1));
}

uint8_t Deck::PeekNextEvent(bool on) const {
  Event* event = latest_event_;
  while (event) {
    event = &const_cast<Event&>(event_after(*event));
    if (event->on == on) return event->note_index;
    if (event == latest_event_) break;
  }
  return kNullIndex;
}

void Deck::ProcessNotes(uint16_t new_pos, NoteOnFn note_on_fn, NoteOffFn note_off_fn) {
  if (!latest_event_) return; // No events
  Event* first_seen_event_ptr = NULL;
  while (true) {
    Event& event = const_cast<Event&>(event_after(*latest_event_));
    if (&event == first_seen_event_ptr) break;
    if (!first_seen_event_ptr) first_seen_event_ptr = &event;

    uint8_t note_index = event.note_index;
    const Note& note = notes_[note_index];
    if (!Passed(event_pos(event), pos_, new_pos)) break;
    latest_event_ = &event; // TODO maybe postpone?

    if (event.on) {
      if (!after_off_[note_index].exists())
        // If the next 'on' note doesn't yet have an off link, it's still held,
        // and has been for an entire loop
        RecordNoteOff(note_index); // TODO advance pos_ before doing this?
        if (note_off_fn) {
          (part_->*note_off_fn)(note_index, note.pitch);
        }
      }
      if (note_on_fn) (part_->*note_on_fn)(note_index, note.pitch, note.velocity);
    else {
      if (note_off_fn) (part_->*note_off_fn)(note_index, note.pitch);
    }
  }

  pos_ = new_pos;
  needs_advance_ = false;
}

uint8_t Deck::RecordNoteOn(uint8_t pitch, uint8_t velocity) {
  if (size_ == kMaxNotes) {
    RemoveOldestNote();
  }
  uint8_t index = index_mod(oldest_index_ + size_);

  AddEvent((Event){ index, true });
  Note& note = notes_[index];
  note.pitch = pitch;
  note.velocity = velocity;
  note.on_pos = pos_;
  note.off_pos = pos_;
  after_off_[index].note_index = kNullIndex;
  size_++;

  return index;
}

// Returns whether the NoteOff should be sent
bool Deck::RecordNoteOff(uint8_t index) {
  if (
    // Note was already removed
    !after_on_[index].exists() ||
    // off link was already set by Advance
    after_off_[index].exists()
  ) {
    return false;
  }
  AddEvent((Event){ index, false });
  notes_[index].off_pos = pos_;
  return true;
}

uint16_t Deck::NoteFractionCompleted(uint8_t index) const {
  const Note& note = notes_[index];
  uint16_t pos_since_on = pos_ - note.on_pos;
  return (static_cast<uint32_t>(pos_since_on) << 16) / note.length();
}

uint8_t Deck::NotePitch(uint8_t index) const {
  return notes_[index].pitch;
}

uint8_t Deck::NoteAgeOrdinal(uint8_t index) const {
  return index_mod(index - oldest_index_);
}

bool Deck::Passed(uint16_t target, uint16_t before, uint16_t after) const {
  if (before < after) {
    return (target > before and target <= after);
  } else {
    return (target > before or target <= after);
  }
}

void Deck::AddEvent(Event& e) {
  if (latest_event_ == NULL) {
    latest_event_ = &Prepend(e, e);
  } else {
    Prepend(e, const_cast<Event&>(event_after(*latest_event_)));
    latest_event_ = &Prepend(*latest_event_, e);
  }
}

void Deck::KillNote(uint8_t target_index) {
  Note& target_note = notes_[target_index];
  if (
    // Note is being recorded
    !after_off_[target_index].exists() ||
    // Note is being played
    Passed(pos_, target_note.on_pos, target_note.off_pos)
  ) {
    part_->LooperPlayNoteOff(target_index, target_note.pitch);
  }
}

void Deck::RemoveNote(uint8_t target_index) {
  // Though this takes an arbitrary index, other methods like NoteAgeOrdinal
  // assume that notes are stored sequentially in memory, so removing a "middle"
  // note will cause problems
  if (!size_) {
    return;
  }

  KillNote(target_index);

  Event* before_on_for_target_note;
  Event* before_off_for_target_note;
  for (uint8_t i = 0; i < size_; ++i) {
    uint8_t search_note_index = index_mod(oldest_index_ + i);
    
    Event& after_on_for_search_note = after_on_[search_note_index];
    if (after_on_for_search_note.note_index == target_index) {
      if (after_on_for_search_note.on) before_on_for_target_note = &after_on_for_search_note;
      else before_off_for_target_note = &after_on_for_search_note;
    }

    Event& after_off_for_search_note = after_off_[search_note_index];
    if (after_off_for_search_note.note_index == target_index) {
      if (after_off_for_search_note.on) before_on_for_target_note = &after_off_for_search_note;
      else before_off_for_target_note = &after_off_for_search_note;
    }

    // if (before_on && before_off) break;
  }

  // if (before_on_for_target_note) {
  //   before_on_for_target_note = event_after(
  // }

  size_--;
  uint8_t search_prev_index;
  uint8_t search_next_index;

  search_prev_index = target_index;
  while (true) {
    search_next_index = next_link_[search_prev_index].on;
    if (search_next_index == target_index) {
      break;
    }
    search_prev_index = search_next_index;
  }
  next_link_[search_prev_index].on = next_link_[target_index].on;
  if (target_index == search_prev_index) {
    // If this was the last note
    latest_event_.on = kNullIndex;
  } else if (target_index == latest_event_.on) {
    latest_event_.on = search_prev_index;
  }

  if (next_link_[target_index].off == kNullIndex) {
    // Don't try to relink off
    return;
  }

  search_prev_index = target_index;
  while (true) {
    search_next_index = next_link_[search_prev_index].off;
    if (search_next_index == target_index) {
      break;
    }
    search_prev_index = search_next_index;
  }
  next_link_[search_prev_index].off = next_link_[target_index].off;
  next_link_[target_index].off = kNullIndex;
  if (target_index == search_prev_index) {
    // If this was the last note
    latest_event_.off = kNullIndex;
  } else if (target_index == latest_event_.off) {
    latest_event_.off = search_prev_index;
  }
}

} // namespace looper
}  // namespace yarns
