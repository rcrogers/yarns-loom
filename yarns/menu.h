
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
// Menus.

#ifndef YARNS_MENU_H
#define YARNS_MENU_H

#include "stmlib/stmlib.h"

#include "yarns/settings.h"

namespace yarns {

#define MENU_LAYOUT_CLOCK \
  SETTING_LAYOUT, \
  SETTING_CLOCK_TEMPO, \
  SETTING_CLOCK_SWING, \
  SETTING_CLOCK_INPUT_DIVISION, \
  SETTING_CLOCK_OUTPUT_DIVISION, \
  SETTING_CLOCK_BAR_DURATION, \
  SETTING_CLOCK_NUDGE_FIRST_TICK, \
  SETTING_CLOCK_MANUAL_START

#define MENU_MIDI \
  SETTING_MIDI_CHANNEL, \
  SETTING_MIDI_MIN_NOTE, \
  SETTING_MIDI_MAX_NOTE, \
  SETTING_MIDI_MIN_VELOCITY, \
  SETTING_MIDI_MAX_VELOCITY, \
  SETTING_MIDI_OUT_MODE, \
  SETTING_MIDI_SUSTAIN_MODE, \
  SETTING_MIDI_SUSTAIN_POLARITY, \
  SETTING_MIDI_INPUT_RESPONSE

#define MENU_VOICING_ALLOCATION_MONO \
  SETTING_VOICING_ALLOCATION_PRIORITY, \
  SETTING_VOICING_LEGATO_MODE

#define MENU_VOICING_ALLOCATION_POLY \
  SETTING_VOICING_ALLOCATION_MODE, \
  SETTING_VOICING_ALLOCATION_PRIORITY

#define MENU_VOICING_ALLOCATION_MIXED \
  SETTING_VOICING_ALLOCATION_MODE, \
  MENU_VOICING_ALLOCATION_MONO

#define MENU_MODULATION \
  SETTING_VOICING_PITCH_BEND_RANGE, \
  SETTING_VOICING_VIBRATO_MOD, \
  SETTING_VOICING_VIBRATO_SHAPE, \
  SETTING_VOICING_VIBRATO_RANGE

#define MENU_EUCLIDEAN \
  SETTING_SEQUENCER_EUCLIDEAN_LENGTH, \
  SETTING_SEQUENCER_EUCLIDEAN_FILL, \
  SETTING_SEQUENCER_EUCLIDEAN_ROTATE

#define MENU_TUNING \
  SETTING_VOICING_TUNING_SYSTEM, \
  SETTING_VOICING_TUNING_ROOT, \
  SETTING_VOICING_TUNING_FACTOR

#define MENU_END \
  SETTING_REMOTE_CONTROL_CHANNEL, \
  SETTING_LAST

#define MENU_LIVE \
  SETTING_MIDI_TRANSPOSE_OCTAVES, \
  SETTING_VOICING_PORTAMENTO, \
  SETTING_VOICING_LFO_RATE, \
  SETTING_SEQUENCER_CLOCK_QUANTIZATION, \
  SETTING_SEQUENCER_LOOP_LENGTH, \
  SETTING_SEQUENCER_CLOCK_DIVISION, \
  SETTING_SEQUENCER_GATE_LENGTH, \
  SETTING_SEQUENCER_ARP_RANGE, \
  SETTING_SEQUENCER_ARP_DIRECTION, \
  SETTING_SEQUENCER_ARP_PATTERN, \
  MENU_EUCLIDEAN, \
  SETTING_VOICING_TUNING_TRANSPOSE, \
  SETTING_VOICING_TUNING_FINE

static const SettingIndex menu_live[] = {
  SETTING_MENU_SETUP,
  SETTING_MENU_OSCILLATOR,
  SETTING_MENU_ENVELOPE,
  MENU_LIVE,
  SETTING_LAST
};

static const SettingIndex menu_oscillator[] = {
  SETTING_VOICING_OSCILLATOR_MODE,
  SETTING_VOICING_OSCILLATOR_SHAPE,
  SETTING_VOICING_TIMBRE_INIT,
  SETTING_VOICING_TIMBRE_MOD_LFO,
  SETTING_VOICING_TIMBRE_LFO_SHAPE,
  SETTING_VOICING_TIMBRE_MOD_ENVELOPE,
  SETTING_VOICING_TIMBRE_MOD_VELOCITY,

  SETTING_LAST
};

static const SettingIndex menu_amplitude[] = {
  SETTING_VOICING_TREMOLO_MOD,
  SETTING_VOICING_TREMOLO_SHAPE,
  SETTING_VOICING_ENV_PEAK_MOD_VELOCITY,
  SETTING_VOICING_ENV_INIT_ATTACK,
  SETTING_VOICING_ENV_MOD_ATTACK,
  SETTING_VOICING_ENV_INIT_DECAY,
  SETTING_VOICING_ENV_MOD_DECAY,
  SETTING_VOICING_ENV_INIT_SUSTAIN,
  SETTING_VOICING_ENV_MOD_SUSTAIN,
  SETTING_VOICING_ENV_INIT_RELEASE,
  SETTING_VOICING_ENV_MOD_RELEASE,

  SETTING_LAST
};

static const SettingIndex menu_live_quad_triggers[] = {
  SETTING_MENU_SETUP,
  SETTING_MENU_ENVELOPE,
  SETTING_VOICING_TRIGGER_DURATION,
  SETTING_VOICING_TRIGGER_SCALE,
  SETTING_VOICING_TRIGGER_SHAPE,
  SETTING_SEQUENCER_CLOCK_DIVISION,
  SETTING_SEQUENCER_RHYTHM_PATTERN,
  MENU_EUCLIDEAN,
  SETTING_LAST
};

static const SettingIndex mono[] = {
  MENU_LAYOUT_CLOCK,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_MONO,
  MENU_MODULATION,
  SETTING_VOICING_TRIGGER_DURATION,
  SETTING_VOICING_CV_OUT_3,
  SETTING_VOICING_CV_OUT_4,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex dual_mono[] = {
  MENU_LAYOUT_CLOCK,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_MONO,
  MENU_MODULATION,
  SETTING_VOICING_CV_OUT,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex quad_mono[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_CLOCK_OVERRIDE,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_MONO,
  MENU_MODULATION,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex dual_poly[] = {
  MENU_LAYOUT_CLOCK,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_POLY,
  MENU_MODULATION,
  SETTING_VOICING_CV_OUT_3,
  SETTING_VOICING_CV_OUT_4,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex quad_poly[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_CLOCK_OVERRIDE,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_POLY,
  MENU_MODULATION,
  MENU_TUNING,
  MENU_END
};

#define MENU_FULL_POLYCHAINED \
  MENU_LAYOUT_CLOCK, \
  MENU_MIDI,\
  SETTING_VOICING_ALLOCATION_PRIORITY, \
  MENU_MODULATION, \
  SETTING_VOICING_CV_OUT_3, \
  SETTING_VOICING_CV_OUT_4, \
  MENU_TUNING, \
  MENU_END

static const SettingIndex dual_polychained[] = {
  MENU_FULL_POLYCHAINED
};

static const SettingIndex quad_polychained[] = {
  MENU_FULL_POLYCHAINED
};

static const SettingIndex octal_polychained[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_CLOCK_OVERRIDE,
  MENU_MIDI,
  SETTING_VOICING_ALLOCATION_PRIORITY,
  MENU_MODULATION,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex quad_triggers[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_MIDI_CHANNEL,
  SETTING_MIDI_MIN_VELOCITY,
  SETTING_MIDI_MAX_VELOCITY,
  SETTING_MIDI_NOTE,
  SETTING_MIDI_OUT_MODE,
  SETTING_MIDI_INPUT_RESPONSE,
  MENU_END
};

#define MENU_FULL_HYBRID \
  MENU_LAYOUT_CLOCK, \
  SETTING_CLOCK_OVERRIDE, \
  MENU_MIDI, \
  MENU_VOICING_ALLOCATION_MIXED, \
  MENU_MODULATION, \
  MENU_TUNING, \
  MENU_END

static const SettingIndex three_one[] = {
  MENU_FULL_HYBRID
};

static const SettingIndex two_two[] = {
  MENU_FULL_HYBRID
};

static const SettingIndex paraphonic_plus_two[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_CLOCK_OVERRIDE,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_MIXED,
  MENU_MODULATION,
  SETTING_VOICING_CV_OUT_3,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex two_one[] = {
  MENU_LAYOUT_CLOCK,
  MENU_MIDI,
  MENU_VOICING_ALLOCATION_MIXED,
  MENU_MODULATION,
  SETTING_VOICING_CV_OUT_4,
  MENU_TUNING,
  MENU_END
};

static const SettingIndex quad_voltages[] = {
  MENU_LAYOUT_CLOCK,
  SETTING_CLOCK_OVERRIDE,
  SETTING_MIDI_CHANNEL,
  SETTING_VOICING_CV_OUT,
  MENU_END
};

static const SettingIndex* setup_setting_list_for_layout[] = {
  mono,
  dual_mono,
  quad_mono,
  dual_poly,
  quad_poly,
  dual_polychained,
  quad_polychained,
  octal_polychained,
  quad_triggers,
  quad_voltages,
  three_one,
  two_two,
  two_one,
  paraphonic_plus_two,
};

class Menu {
 public:
  void Init(SettingIndex t) {
    menu_type_ = t;
    pos_ = 0;
  }

  const SettingIndex* setting_list() {
    switch (menu_type_) {
      case SETTING_MENU_SETUP:
        return setup_setting_list_for_layout[multi.layout()];

      case SETTING_MENU_OSCILLATOR:
        return menu_oscillator;

      case SETTING_MENU_ENVELOPE:
        return menu_amplitude;

      default:
        switch (multi.layout()) {
          case LAYOUT_QUAD_TRIGGERS:
            return menu_live_quad_triggers;
          default:
            return menu_live;
        }
    }
  }

  const Setting& setting() {
    return setting_defs.get(setting_list()[pos_]);
  }

  void increment_index(int32_t n) {
    pos_ += n;
    if (pos_ < 0) {
      pos_ = 0;
    } else if (setting_list()[pos_] == SETTING_LAST) {
      --pos_;
    }
  }

 private:
  SettingIndex menu_type_;
  int8_t pos_;

};

} // namespace yarns

#endif // YARNS_MENU_H