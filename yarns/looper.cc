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
#include "yarns/part.h"

namespace yarns {

namespace looper {

void Deck::Init(Part* part) {
  part_ = part;
  RemoveAll();
  JumpToPhase(ComputeTargetPhaseWithOffset(0));
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
  head_.on = kNullIndex;
  head_.off = kNullIndex;
  oldest_index_ = 0;
  size_ = 0;

  std::fill(
    &next_link_[0],
    &next_link_[kMaxNotes],
    Link()
  );
}

void Deck::JumpToPhase(uint32_t phase) {
  lfo_.SetPhase(phase);
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
      LinkOn(index);
      ProcessNotes(note.off_pos, NULL, NULL);
      LinkOff(index);
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

void Deck::Clock(int32_t tick_counter) {
  lfo_.Tap(tick_counter, period_ticks(), pos_offset << 16);
}

uint32_t Deck::ComputeTargetPhaseWithOffset(int32_t tick_counter) const {
  return lfo_.ComputeTargetPhase(tick_counter, period_ticks(), pos_offset << 16);
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

uint8_t Deck::PeekNextOn() const {
  if (head_.on == kNullIndex) {
    return kNullIndex;
  }
  return next_link_[head_.on].on;
}

uint8_t Deck::PeekNextOff() const {
  if (head_.off == kNullIndex) {
    return kNullIndex;
  }
  return next_link_[head_.off].off;
}

void Deck::ProcessNotes(uint16_t new_pos, NoteOnFn note_on_fn, NoteOffFn note_off_fn) {
  uint8_t first_seen_on_index, first_seen_off_index;
  first_seen_on_index = first_seen_off_index = looper::kNullIndex;
  while (true) {
    const uint8_t on_index = PeekNextOn();
    const uint8_t off_index = PeekNextOff();
    const Note& on = notes_[on_index];
    const Note& off = notes_[off_index];

    bool can_on = (
      on_index != kNullIndex &&
      on_index != first_seen_on_index &&
      Passed(on.on_pos, pos_, new_pos)
    );
    bool can_off = (
      off_index != kNullIndex &&
      off_index != first_seen_off_index &&
      Passed(off.off_pos, pos_, new_pos)
    );

    if (can_on && (
      // TODO if these are for the same note, shit could get screwy?
      !can_off || (on.on_pos - pos_) < (off.off_pos - pos_)
    )) {
      if (first_seen_on_index == looper::kNullIndex) first_seen_on_index = on_index;
      if (next_link_[on_index].off == kNullIndex) {
        // If the next 'on' note doesn't yet have an off link, it's still held,
        // and has been for an entire loop
        RecordNoteOff(on_index);
        if (note_off_fn) (part_->*note_off_fn)(on_index, on.pitch);
      }
      head_.on = on_index;
      if (note_on_fn) (part_->*note_on_fn)(on_index, on.pitch, on.velocity);
    } else if (can_off) {
      if (first_seen_off_index == looper::kNullIndex) first_seen_off_index = off_index;
      head_.off = off_index;
      if (note_off_fn) (part_->*note_off_fn)(off_index, off.pitch);
    } else break; // Neither upcoming event is eligible yet
  }

  pos_ = new_pos;
}

uint8_t Deck::RecordNoteOn(uint8_t pitch, uint8_t velocity) {
  if (size_ == kMaxNotes) {
    RemoveOldestNote();
  }
  uint8_t index = index_mod(oldest_index_ + size_);

  LinkOn(index);
  Note& note = notes_[index];
  note.pitch = pitch;
  note.velocity = velocity;
  note.on_pos = pos_;
  note.off_pos = pos_;
  next_link_[index].off = kNullIndex;
  size_++;

  return index;
}

// Returns whether the NoteOff should be sent
bool Deck::RecordNoteOff(uint8_t index) {
  if (
    // Note was already removed
    next_link_[index].on == kNullIndex ||
    // off link was already set by Advance
    next_link_[index].off != kNullIndex
  ) {
    return false;
  }
  LinkOff(index);
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

void Deck::LinkOn(uint8_t index) {
  if (head_.on == kNullIndex) {
    // there is no prev note to link to this one, so link it to itself
    next_link_[index].on = index;
  } else {
    next_link_[index].on = next_link_[head_.on].on;
    next_link_[head_.on].on = index;
  }
  head_.on = index;
}

void Deck::LinkOff(uint8_t index) {
  if (head_.off == kNullIndex) {
    // there is no prev note to link to this one, so link it to itself
    next_link_[index].off = index;
  } else {
    next_link_[index].off = next_link_[head_.off].off;
    next_link_[head_.off].off = index;
  }
  head_.off = index;
}

void Deck::KillNote(uint8_t target_index) {
  Note& target_note = notes_[target_index];
  if (
    // Note is being recorded
    next_link_[target_index].off == kNullIndex ||
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
  next_link_[target_index].on = kNullIndex; // unneeded?
  if (target_index == search_prev_index) {
    // If this was the last note
    head_.on = kNullIndex;
  } else if (target_index == head_.on) {
    head_.on = search_prev_index;
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
    head_.off = kNullIndex;
  } else if (target_index == head_.off) {
    head_.off = search_prev_index;
  }
}

} // namespace looper
}  // namespace yarns
