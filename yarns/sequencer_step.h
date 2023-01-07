#ifndef YARNS_SEQUENCER_STEP_H_
#define YARNS_SEQUENCER_STEP_H_

#include "stmlib/stmlib.h"

namespace yarns {

const uint8_t kC4 = 60;

enum SequencerStepFlags {
  SEQUENCER_STEP_REST = 0x80,
  SEQUENCER_STEP_TIE = 0x81
};

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

  // 0-indexed
  inline uint8_t octave() const { return note() / 12; }
  inline uint8_t white_key_value() const { return whiteKeyValues[note() % 12]; }
  inline uint8_t black_key_value() const { return blackKeyValues[note() % 12]; }
  inline uint8_t color_key_value() const { return is_white() ? white_key_value() : black_key_value(); }

  inline int8_t octaves_above_middle_c() const { return ((int8_t) octave()) - (60 / 12); }
  inline int8_t white_key_distance_from_middle_c() const {
    return octaves_above_middle_c() * ((int8_t) kNumWhiteKeys) + white_key_value();
  }
  inline int8_t black_key_distance_from_middle_c() const {
    return octaves_above_middle_c() * ((int8_t) kNumBlackKeys) + black_key_value();
  }
};

}

#endif // YARNS_SEQUENCER_STEP_H_