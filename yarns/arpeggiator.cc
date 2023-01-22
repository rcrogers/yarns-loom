// Copyright 2013 Emilie Gillet.
// Copyright 2019 Chris Rogers.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// Arpeggiator.

#include "yarns/arpeggiator.h"

#include "stmlib/stmlib.h"
#include "stmlib/utils/random.h"

#include "yarns/part.h"
#include "yarns/resources.h"

namespace yarns {
    
using namespace stmlib;

void Arpeggiator::Reset() {
  key_index = 0;
  octave = 0;
  key_increment = 1;
}

const SequencerArpeggiatorResult Arpeggiator::BuildNextResult(
  const Part& part,
  const HeldKeys& arp_keys,
  uint32_t step_counter, // May differ from part's current step (for peeking)
  SequencerStep seq_step
) const {
  // In case of early return, the arp does not advance, and the note is a REST
  SequencerArpeggiatorResult result = {
    *this, SequencerStep(SEQUENCER_STEP_REST, 0)
  };
  Arpeggiator& next = result.arpeggiator;

  if (part.seq_driven_arp()) {
    if (!seq_step.has_note()) { // Here, the output step can also be a TIE
      result.note.data[0] = seq_step.data[0];
      return result;
    }
  } else { // Use an arp pattern
    uint8_t pattern_step_index = step_counter % 16;
    // Build a dummy input step for JUMP/GRID
    seq_step.data[0] = kC4 + 1 + pattern_step_index;
    seq_step.data[1] = 0x7f; // Full velocity
    uint16_t pattern_mask = 1 << pattern_step_index;
    uint16_t pattern = lut_arpeggiator_patterns[LUT_ARPEGGIATOR_PATTERNS_SIZE - part.sequencer_settings().arp_pattern - 1];
    if (!(pattern_mask & pattern)) return result;
  }

  uint8_t num_keys = arp_keys.stack.size();
  if (!num_keys) {
    next.Reset();
    return result;
  }

  uint8_t arp_range = part.sequencer_settings().arp_range;
  uint8_t arp_direction = part.sequencer_settings().arp_direction;

  uint8_t num_octaves = arp_range + 1;
  uint8_t num_keys_all_octaves = num_keys * num_octaves;
  uint8_t display_octave = seq_step.octave();
  if (display_octave > 0) display_octave--; // Match octave display in UI, ranging 0..9
  // Update arepggiator note/octave counter.
  switch (arp_direction) {
    case ARPEGGIATOR_DIRECTION_RANDOM:
      {
        uint16_t random = Random::GetSample();
        next.octave = (random & 0xff) % num_octaves;
        next.key_index = random >> 8;
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_JUMP:
      {
        // If step value by color within octave is greater than total chord size, rest without moving
        if (seq_step.color_key_value() >= num_keys_all_octaves) return result;

        // Advance active position by octave # -- C4 -> pos + 4; C0 -> pos + 0
        next.key_index = modulo(next.key_index + display_octave, num_keys_all_octaves);
        if (seq_step.is_white()) {
          next.key_increment = 0; // Move is already complete
        } else { // If black key
          // Play the black key value as an absolute position in the arp chord,
          // then return to active position
          next.key_increment = next.key_index - seq_step.color_key_value();
          next.key_index = seq_step.color_key_value();
        }

        next.octave = modulo(next.key_index / num_keys, num_octaves);
      }
      break;
    case ARPEGGIATOR_DIRECTION_STEP_GRID:
      {
        // If step value by color within octave is greater than total chord size, rest without moving
        if (seq_step.color_key_value() >= num_keys_all_octaves) return result;

        // Map linear position to X-Y grid coordinates
        // C4 -> 4x4 grid; C0 -> 1x1; C1 -> 1x1; C9 -> 9x9
        uint8_t size = std::max(static_cast<uint8_t>(1), display_octave);
        uint8_t x_pos = modulo(next.key_index, size);
        uint8_t y_pos = modulo(next.key_index / size, size);
        // Move by 1 position within grid, with step color determining direction
        if (seq_step.is_white()) {
          x_pos = modulo(x_pos + 1, size);
        } else {
          y_pos = modulo(y_pos + 1, size);
        }
        // Map grid position back to linear position, which can be > chord size
        // Max linear position is 81 (9x9), so no risk of int8_t overflow
        next.key_index = x_pos + y_pos * size;
        next.key_increment = 0; // Move is already complete

        next.octave = modulo(next.key_index / num_keys, num_octaves);
      }
      break;
    default:
      {
        if (num_keys == 1 && arp_range == 0) {
          // This is a corner case for the Up/down pattern code.
          // Get it out of the way.
          next.key_index = 0;
          next.octave = 0;
        } else {
          bool wrapped = true;
          while (wrapped) {
            if (next.key_index >= num_keys || next.key_index < 0) {
              next.octave += next.key_increment;
              next.key_index = next.key_increment > 0 ? 0 : num_keys - 1;
            }
            wrapped = false;
            if (next.octave > arp_range || next.octave < 0) {
              next.octave = next.key_increment > 0 ? 0 : arp_range;
              if (arp_direction == ARPEGGIATOR_DIRECTION_UP_DOWN) {
                next.key_increment = -next.key_increment;
                next.key_index = next.key_increment > 0 ? 1 : num_keys - 2;
                next.octave = next.key_increment > 0 ? 0 : arp_range;
                wrapped = true;
              }
            }
          }
        }
      }
      break;
  }

  // Build arpeggiator step
  const NoteEntry* arpeggio_note = &arp_keys.stack.note_by_priority(
    static_cast<stmlib::NoteStackFlags>(part.voicing_settings().allocation_priority),
    modulo(next.key_index, num_keys)
  );
  next.key_index += next.key_increment;

  // TODO step type algorithm

  uint8_t note = arpeggio_note->note;
  uint8_t velocity = arpeggio_note->velocity & 0x7f;
  if (part.seq_driven_arp()) {
    velocity = (velocity * seq_step.velocity()) >> 7;
  }
  note += 12 * next.octave;
  while (note > 127) {
    note -= 12;
  }
  result.note.data[0] = note;
  result.note.data[1] = velocity;

  return result;
}

}