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

namespace looper {

const uint8_t kBitsNoteIndex = 5;
STATIC_ASSERT(kBitsNoteIndex <= 7, bits); // Leave room for kNullIndex
const uint8_t kNullIndex = UINT8_MAX;

const uint8_t kMaxNotes = 31;
STATIC_ASSERT(kMaxNotes < (1 << kBitsNoteIndex), bits);

struct Link {
  Link() {
    on = off = kNullIndex;
  }
  // Note indexes
  uint8_t on;
  uint8_t off;
};

struct Note {
  Note() { }
  uint16_t on_pos;
  uint16_t off_pos;
  uint8_t pitch;
  uint8_t velocity;
};

const uint8_t kBitsPos = 13;
const uint8_t kBitsMIDI = 7;

struct PackedNote {
  PackedNote() { }
  unsigned int
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
  void Rewind();
  void Unpack(PackedPart& storage);
  void Pack(PackedPart& storage);

  inline uint16_t phase() const {
    return pos_;
  }
  void Clock();
  inline void Refresh() {
    uint16_t new_phase = lfo_.Refresh() >> 16;
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

  void RemoveOldestNote();
  void RemoveNewestNote();
  inline void AdvanceToPresent() {
    if (!needs_advance_) { return; }
    uint16_t new_pos = lfo_.GetPhase() >> 16;
    Advance(new_pos, true);
  }
  uint8_t RecordNoteOn(uint8_t pitch, uint8_t velocity);
  bool RecordNoteOff(uint8_t index);
  uint8_t PeekNextOn() const;
  uint8_t PeekNextOff() const;

  bool NoteIsPlaying(uint8_t index) const;
  uint16_t NoteFractionCompleted(uint8_t index) const;
  uint8_t NotePitch(uint8_t index) const;
  uint8_t NoteAgeOrdinal(uint8_t index) const;

  inline const Note& note_at(uint8_t index) const {
    return notes_[index];
  }

 private:

  inline uint8_t index_mod(int8_t i) const {
    return stmlib::modulo(i, kMaxNotes);
  }
  void Advance(uint16_t new_pos, bool play);
  bool Passed(uint16_t target, uint16_t before, uint16_t after) const;
  void LinkOn(uint8_t index);
  void LinkOff(uint8_t index);
  void RemoveNote(uint8_t index);

  Part* part_;

  Note notes_[kMaxNotes];
  uint8_t oldest_index_;
  uint8_t size_;
  // Linked lists track current and upcoming notes
  Link head_; // Points to the latest on/off
  Link next_link_[kMaxNotes];

  // Phase tracking
  SyncedLFO lfo_;
  uint16_t pos_;
  bool needs_advance_;

  DISALLOW_COPY_AND_ASSIGN(Deck);
};

} // namespace looper
}  // namespace yarns

#endif // YARNS_LOOPER_H_
