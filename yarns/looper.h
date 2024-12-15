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

#ifndef YARNS_LOOPER_H_
#define YARNS_LOOPER_H_

#include "stmlib/stmlib.h"
#include <algorithm>

#include "yarns/synced_lfo.h"

namespace yarns {

class Part;
struct PackedPart;
typedef void (Part::*NoteOnFn)(uint8_t looper_note_index, uint8_t pitch, uint8_t velocity);
typedef void (Part::*NoteOffFn)(uint8_t looper_note_index, uint8_t pitch);

namespace looper {

const uint8_t kBitsNoteIndex = 5;
STATIC_ASSERT(kBitsNoteIndex <= 7, bits); // Leave room for kNullIndex
const uint8_t kNullIndex = UINT8_MAX;

const uint8_t kMaxNotes = 30;
STATIC_ASSERT(kMaxNotes < (1 << kBitsNoteIndex), bits);

struct Event {
  // Event() {
  //   note_index = kNullIndex;
  //   on = false;
  // }
  // Event(uint8_t ni, bool o) {
  //   note_index = note_index;
  //   on = o;
  // }
  uint8_t note_index;
  bool on;
  bool exists() { return note_index != kNullIndex; }
};
typedef Event NoteEvents[kMaxNotes];

struct Note {
  Note() { }
  uint16_t on_pos, off_pos;
  uint8_t pitch, velocity;
  uint16_t length() const {
    return off_pos - 1 - on_pos;
  }
  // Event after_on, after_off;
  // Event& next_event_for(bool on_off) {
  //   return on_off ? after_on : after_off;
  // }
};

const uint8_t kBitsPos = 13;
const uint8_t kBitsMIDI = 7;

struct PackedNote {
  PackedNote() { }
  unsigned int // values free: 0
    on_pos    : kBitsPos,
    off_pos   : kBitsPos,
    pitch     : kBitsMIDI,
    velocity  : kBitsMIDI;
}__attribute__((packed));

class Deck {
 public:

  Deck() { }
  ~Deck() { }

  void Init(Part* part);

  void RemoveAll();
  void SetPhase(uint32_t phase);
  void Unpack(PackedPart& storage);
  void Pack(PackedPart& storage) const;

  inline uint16_t phase() const {
    return pos_;
  }
  uint16_t period_ticks() const;
  uint32_t lfo_note_phase() const;
  uint32_t ComputeTargetPhaseWithOffset(int32_t tick_counter) const;
  void SetTargetPhase(uint32_t phase);
  inline void Refresh() {
    lfo_.Refresh();
    uint16_t new_phase = lfo_.GetPhase() >> 16;
    if (
      // phase has definitely changed, or
      pos_ != new_phase ||
      // Increment is large enough to produce a 16-bit change, indicating that
      // the phase has wrapped exactly around
      (lfo_.GetPhaseIncrement() >> 16) > 0
    ) {
      needs_advance_ = true;
    }
  }

  inline bool num_notes() const { return size_; }

  void RemoveOldestNote();
  void RemoveNewestNote();
  void ProcessNotes(uint16_t new_pos, NoteOnFn note_on_fn, NoteOffFn note_off_fn);
  inline void ProcessNotesUntilLFOPhase(NoteOnFn note_on_fn, NoteOffFn note_off_fn) {
    if (!needs_advance_) { return; }
    uint16_t new_pos = lfo_.GetPhase() >> 16;
    ProcessNotes(new_pos, note_on_fn, note_off_fn);
  }
  uint8_t RecordNoteOn(uint8_t pitch, uint8_t velocity);
  bool RecordNoteOff(uint8_t index);
  uint8_t PeekNextEvent(bool on) const;

  uint16_t NoteFractionCompleted(uint8_t index) const;
  uint8_t NotePitch(uint8_t index) const;
  uint8_t NoteAgeOrdinal(uint8_t index) const;

  inline const Note& note_at(uint8_t index) const {
    return notes_[index];
  }

  const NoteEvents& event_array_after(Event& e) const {
    return e.on ? after_on_ : after_off_;
  }

  // NoteEvents& mutable_event_array_after(Event& e) {
  //   return e.on ? event_after_on_for_note_ : event_after_off_for_note_;
  // }

  const Event& event_after(Event& e) const {
    return event_array_after(e)[e.note_index];
  }

  const Event& event_before(Event& target) const {
    const NoteEvents& events = event_array_after(target);
    Event* current = &target;
    while (true) {
      Event* next = &const_cast<Event&>(event_after(*current));
      if (next == &target) return *current;
      current = next;
    }
  }

  // Event& mutable_event_after(Event& e) {
  //   return mutable_event_array_after(e)[e.note_index];
  // }

  const uint16_t event_pos(Event& e) const {
    const Note& note = notes_[e.note_index];
    return e.on ? note.on_pos : note.off_pos;
  }

  Event& Prepend(Event& curr, Event& next) {
    Event* event_array = const_cast<NoteEvents&>(event_array_after(curr));
    event_array[curr.note_index] = next;
    return event_array[curr.note_index];
  }

  uint16_t pos_offset;

 private:

  inline uint8_t index_mod(int8_t i) const {
    return stmlib::modulo(i, kMaxNotes);
  }
  bool Passed(uint16_t target, uint16_t before, uint16_t after) const;
  void AddEvent(Event& e);
  void RemoveNote(uint8_t index);
  void KillNote(uint8_t index);

  Part* part_;

  Note notes_[kMaxNotes];
  uint8_t oldest_index_;
  uint8_t size_;
  // Linked lists track current and upcoming notes
  Event* latest_event_; // Points to the latest on/off
  
  NoteEvents after_on_, after_off_;

  // Phase tracking
  SyncedLFO<18, 11> lfo_; // Gentle sync
  uint16_t pos_;
  bool needs_advance_;

  DISALLOW_COPY_AND_ASSIGN(Deck);
};

} // namespace looper
}  // namespace yarns

#endif // YARNS_LOOPER_H_
