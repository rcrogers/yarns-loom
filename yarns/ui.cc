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
// User interface.

#include "stmlib/system/system_clock.h"
#include "stmlib/utils/print.h"

#include "yarns/multi.h"
#include "yarns/ui.h"
#include "yarns/voice.h"

#include <cstring>

namespace yarns {

using namespace std;
using namespace stmlib;

const uint16_t kRefreshMsec = 900;
const uint16_t kCrossfadeMsec = kRefreshMsec >> 3;
const uint32_t kLongPressMsec = kRefreshMsec * 2 / 3;
const uint32_t kRefreshFreq = UINT16_MAX / kRefreshMsec;

/* static */
const Ui::Command Ui::commands_[] = {
  { "*LOAD*", UI_MODE_LOAD_SELECT_PROGRAM, NULL },
  { "*SAVE*", UI_MODE_SAVE_SELECT_PROGRAM, NULL },
  { "*PART SWAP SETTINGS*", UI_MODE_SWAP_SELECT_PART, NULL },
  { "*INIT*", UI_MODE_PARAMETER_SELECT, &Ui::DoInitCommand },
  { "*QUICK CONFIG*", UI_MODE_LEARNING, &Ui::DoLearnCommand },
  { "*>SYSEX DUMP*", UI_MODE_PARAMETER_SELECT, &Ui::DoDumpCommand },
  { "*CALIBRATE*", UI_MODE_CALIBRATION_SELECT_VOICE, NULL },
  { "*EXIT*", UI_MODE_PARAMETER_SELECT, NULL },
};

/* static */
Ui::Mode Ui::modes_[] = {
  // UI_MODE_PARAMETER_SELECT
  { &Ui::OnIncrementParameterSelect, &Ui::OnClick,
    &Ui::PrintParameterName,
    UI_MODE_PARAMETER_EDIT,
    NULL, 0, 0 },
  
  // UI_MODE_PARAMETER_EDIT
  { &Ui::OnIncrementParameterEdit, &Ui::OnClick,
    &Ui::PrintParameterValue,
    UI_MODE_PARAMETER_SELECT,
    NULL, 0, 0 },
  
  // UI_MODE_MAIN_MENU
  { &Ui::OnIncrement, &Ui::OnClickMainMenu,
    &Ui::PrintCommandName,
    UI_MODE_MAIN_MENU,
    NULL, 0, MAIN_MENU_LAST - 1 },
  
  // UI_MODE_LOAD_SELECT_PROGRAM
  { &Ui::OnIncrement, &Ui::OnClickLoadSave,
    &Ui::PrintProgramNumber,
    UI_MODE_MAIN_MENU,
    NULL, 0, kNumPrograms },
  
  // UI_MODE_SAVE_SELECT_PROGRAM
  { &Ui::OnIncrement, &Ui::OnClickLoadSave,
    &Ui::PrintProgramNumber,
    UI_MODE_MAIN_MENU,
    NULL, 0, kNumPrograms },
  
  // UI_MODE_SWAP_SELECT_PART
  { &Ui::OnIncrement, &Ui::OnClickSwapPart,
    &Ui::PrintSwapPart,
    UI_MODE_PARAMETER_SELECT,
    NULL, 0, kNumParts - 1 },

  // UI_MODE_CALIBRATION_SELECT_VOICE
  { &Ui::OnIncrement, &Ui::OnClickCalibrationSelectVoice,
    &Ui::PrintCalibrationVoiceNumber,
    UI_MODE_CALIBRATION_SELECT_VOICE,
    NULL, 0, kNumCVOutputs },
  
  // UI_MODE_CALIBRATION_SELECT_NOTE
  { &Ui::OnIncrement, &Ui::OnClickCalibrationSelectNote,
    &Ui::PrintCalibrationNote,
    UI_MODE_CALIBRATION_SELECT_NOTE,
    NULL, 0, kNumOctaves },
  
  // UI_MODE_CALIBRATION_ADJUST_LEVEL
  { &Ui::OnIncrementCalibrationAdjustment, &Ui::OnClick,
    &Ui::PrintCalibrationNote,
    UI_MODE_CALIBRATION_SELECT_NOTE,
    NULL, 0, 0 },

  // UI_MODE_PUSH_IT_SELECT_NOTE
  { &Ui::OnIncrementPushItNote, &Ui::OnClick,
    &Ui::PrintPushItNote,
    UI_MODE_PARAMETER_SELECT,
    NULL, 0, 127 },

  // UI_MODE_LEARNING
  { &Ui::OnIncrement, &Ui::OnClickLearning,
    &Ui::PrintLearning,
    UI_MODE_PARAMETER_SELECT,
    NULL, 0, 127 },
    
  // UI_MODE_FACTORY_TESTING
  { &Ui::OnIncrementFactoryTesting, &Ui::OnClickFactoryTesting,
    &Ui::PrintFactoryTesting,
    UI_MODE_PARAMETER_SELECT,
    NULL, 0, 99 },
};

void Ui::Init() {
  encoder_.Init();
  display_.Init();
  switches_.Init();
  queue_.Init();
  
  mode_ = UI_MODE_PARAMETER_SELECT;
  active_part_ = 0;

  setup_menu_.Init(SETTING_MENU_SETUP);
  oscillator_menu_.Init(SETTING_MENU_OSCILLATOR);
  envelope_menu_.Init(SETTING_MENU_ENVELOPE);
  live_menu_.Init(SETTING_LAST);
  current_menu_ = &live_menu_;

  previous_tap_time_ = 0;
  tap_tempo_count_ = 0;
  tap_tempo_resolved_ = true;
  
  start_stop_press_time_ = 0;
  
  push_it_note_ = kC4;
  modes_[UI_MODE_MAIN_MENU].incremented_variable = &command_index_;
  modes_[UI_MODE_LOAD_SELECT_PROGRAM].incremented_variable = &program_index_;
  modes_[UI_MODE_SAVE_SELECT_PROGRAM].incremented_variable = &program_index_;
  modes_[UI_MODE_SWAP_SELECT_PART].incremented_variable = &swap_part_index_;
  modes_[UI_MODE_CALIBRATION_SELECT_VOICE].incremented_variable = \
      &calibration_voice_;
  modes_[UI_MODE_CALIBRATION_SELECT_NOTE].incremented_variable = \
      &calibration_note_;
  modes_[UI_MODE_FACTORY_TESTING].incremented_variable = \
      &factory_testing_number_;

  refresh_was_automatic_ = true;
}

void Ui::Poll() {
  encoder_.Debounce();
  
  // Handle press and long press on encoder.
  if (encoder_.just_pressed()) {
    encoder_press_time_ = system_clock.milliseconds();
    encoder_long_press_event_sent_ = false;
  }
  if (!encoder_long_press_event_sent_) {
    if (encoder_.pressed()) {
      uint32_t duration = system_clock.milliseconds() - encoder_press_time_;
      if (duration >= kLongPressMsec && !encoder_long_press_event_sent_) {
        queue_.AddEvent(CONTROL_ENCODER_LONG_CLICK, 0, 0);
        encoder_long_press_event_sent_ = true;
      }
    } else if (encoder_.released()) {
      queue_.AddEvent(CONTROL_ENCODER_CLICK, 0, 0);
    }
  }
  
  // Encoder increment.
  int32_t increment = encoder_.increment();
  if (increment != 0) {
    queue_.AddEvent(CONTROL_ENCODER, 0, increment);
  }

  // Switch press and long press.
  switches_.Debounce();
  PollSwitch(UI_SWITCH_REC        , rec_press_time_       , rec_long_press_event_sent_        );
  PollSwitch(UI_SWITCH_START_STOP_TIE , start_stop_press_time_, start_stop_long_press_event_sent_ );
  PollSwitch(UI_SWITCH_TAP_TEMPO_REST , tap_tempo_press_time_ , tap_tempo_long_press_event_sent_  );

  display_.RefreshSlow();
  
  // Read LED brightness from multi and copy to LEDs driver.
  uint8_t leds_brightness[kNumCVOutputs];
  if (multi.recording() && recording_part().looped()) {
    // light up LED #1 for the first 1/4 of the loop, LED #2 for the second 1/4, and so on
    uint16_t phase = recording_part().looper().phase();
    div_t div_result = div(phase, UINT16_MAX / kNumCVOutputs);
    for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
      const uint8_t sub_phase = (div_result.rem * kNumCVOutputs) >> 8;
      if (div_result.quot == i) {
        leds_brightness[i] = UINT8_MAX - sub_phase;
      // Fade in -- disabled
      // } else if (div_result.quot == modulo(i - 1, kNumCVOutputs)) {
      //   leds_brightness[i] = sub_phase > (UINT8_MAX >> 1) ? (sub_phase - (UINT8_MAX >> 1)) << 1 : 0;
      } else {
        leds_brightness[i] = 0;
      }
    }
  } else {
    multi.GetLedsBrightness(leds_brightness);
  }
  // Linearize brightness
  for (uint8_t i = 0; i < kNumCVOutputs; ++i) {
    uint8_t expo_brightness = (UINT16_MAX - lut_env_expo[UINT8_MAX - leds_brightness[i]]) >> 8;
    leds_brightness[i] = (leds_brightness[i] >> 1) + (expo_brightness >> 1);
  }

  if (mode_ == UI_MODE_FACTORY_TESTING) {
    ++factory_testing_leds_counter_;
    uint16_t x = factory_testing_leds_counter_;
    leds_brightness[0] = (((x + 384) & 511) < 128) ? 255 : 0;
    leds_brightness[1] = (((x + 256) & 511) < 128) ? 255 : 0;
    leds_brightness[2] = (((x + 128) & 511) < 128) ? 255 : 0;
    leds_brightness[3] = (((x + 000) & 511) < 128) ? 255 : 0;
  }
  
  channel_leds.SetBrightness(leds_brightness);
}

void Ui::PollSwitch(const UiSwitch ui_switch, uint32_t& press_time, bool& long_press_event_sent) {
  if (switches_.just_pressed(ui_switch)) {
    press_time = system_clock.milliseconds();
    long_press_event_sent = false;
  }
  if (!long_press_event_sent) {
    if (switches_.pressed(ui_switch)) {
      uint32_t duration = system_clock.milliseconds() - press_time;
      if (duration >= kLongPressMsec && !long_press_event_sent) {
        queue_.AddEvent(CONTROL_SWITCH_HOLD, ui_switch, 0);
        long_press_event_sent = true;
      }
    } else if (switches_.released(ui_switch)
               && !long_press_event_sent) {
      queue_.AddEvent(CONTROL_SWITCH, ui_switch, 0);
    }
  }
}

void Ui::FlushEvents() {
  queue_.Flush();
}

// Display refresh functions.
const char* const calibration_strings[] = {
  "-3", "-2", "-1", " 0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "OK"
};

const char notes_long[] = "C d D e E F g G a A b B ";

// Display starts at "-1" -- C4 = MIDI note 60 = octave index 5 = display octave 4
const char octave[] = "-0123456789";

void Ui::PrintParameterName() {
  display_.Print(setting().short_name, setting().name);
}

void Ui::PrintParameterValue() {
  setting_defs.Print(setting(), multi.GetSettingValue(setting(), active_part_), buffer_);
  display_.Print(buffer_, buffer_, UINT16_MAX, GetFadeForSetting(setting()));
}

void Ui::PrintCommandName() {
  display_.Print(commands_[command_index_].name);
}

void Ui::PrintProgramNumber() {
  if (program_index_ < kNumPrograms) {
    strcpy(buffer_, "P1");
    buffer_[1] += program_index_;
    display_.Print(buffer_);
  } else {
    display_.Print("--");
  } 
}

void Ui::PrintCalibrationVoiceNumber() {
  if (calibration_voice_ < kNumCVOutputs) {
    strcpy(buffer_, "*1");
    buffer_[1] += calibration_voice_;
    display_.Print(buffer_);
  } else {
    display_.Print("OK");
  } 
}

void Ui::PrintCalibrationNote() {
  display_.Print(
      calibration_strings[calibration_note_],
      calibration_strings[calibration_note_]);
}

// NB: just writes buffer, doesn't display it
void Ui::PrintPartAndPlayMode(uint8_t part) {
  uint8_t play_mode = multi.part(part).midi_settings().play_mode;
  strcpy(buffer_, "1x");
  buffer_[0] += part;
  buffer_[1] = setting_defs.get(SETTING_SEQUENCER_PLAY_MODE).values[play_mode][0];
  buffer_[2] = '\0';
}

void Ui::PrintArpeggiatorMovementStep(SequencerStep step) {
  if (step.is_white()) {
    Settings::PrintSignedInteger(buffer_, step.white_key_value());
  } else {
    int8_t value = step.black_key_value();
    Settings::PrintSignedInteger(buffer_, (value >= 0 ? value + 1 : abs(value)));
    if (buffer_[0] == ' ') {
      buffer_[0] = value >= 0 ? '>' : '<';
    }
  }
  display_.Print(buffer_, buffer_);
}

uint16_t Ui::GetBrightnessFromSequencerPhase(const Part& part) {
  if (part.looped()) {
    return UINT16_MAX - part.looper().phase();
  } else if (!part.sequencer_settings().num_steps) {
    return UINT16_MAX;
  } else {
    return ((1 + part.playing_step()) << 16) / part.sequencer_settings().num_steps;
  }
}

const uint16_t kMasksNewLooperBeat[kDisplayWidth] = { 0x8000, 0x8000 };
void Ui::PrintLoopSequencerStatus() {
  uint8_t note_index = recording_part().LooperCurrentNoteIndex();

  if (note_index == looper::kNullIndex) { // Print the metronome
    // Display metronome if the looper is in the first 1/16th of a beat
    if (recording_part().looper().lfo_note_phase() >> (32 - 4) == 0) {
      if (recording_part().seq_overwrite()) {
        display_.Print("//");
      } else {
        display_.PrintMasks(kMasksNewLooperBeat);
      }
    } else {
      display_.Print("__", "__", GetBrightnessFromSequencerPhase(recording_part()));
    }
    return;
  }

  const looper::Deck& looper = recording_part().looper();
  uint16_t brightness = UINT16_MAX - looper.NoteFractionCompleted(note_index);
  if (recording_mode_is_displaying_pitch_) {
    PrintNote(looper.NotePitch(note_index), brightness);
  } else {
    Settings::PrintInteger(buffer_, looper.NoteAgeOrdinal(note_index) + 1);
    display_.Print(buffer_, buffer_, brightness);
  }
}

void Ui::PrintStepSequencerStatus() {
  if (push_it_) {
    PrintPushItNote();
    return;
  }

  uint8_t rec_step = recording_part().recording_step();
  // If playing a sequencer step other than the selected one, 2/3 brightness
  uint16_t brightness = (
    recording_part().num_steps() == 0 ||
    rec_step == recording_part().playing_step()
  ) ? UINT16_MAX : 43690;
  const SequencerStep& step = recording_part().sequencer_settings().step[rec_step];
  uint16_t fade = step.is_slid() ? kRefreshFreq << 1 : 0;

  if (recording_mode_is_displaying_pitch_) {
    if (step.is_rest()) display_.Print("RS", "RS", brightness, fade);
    else if (step.is_tie()) display_.Print("TI", "TI", brightness, fade);
    else PrintNote(step.note(), brightness, fade);
  } else {
    Settings::PrintInteger(buffer_, rec_step + 1);
    display_.Print(buffer_, buffer_, brightness, fade);
  }
}

void Ui::PrintNote(int16_t note, uint16_t brightness, uint16_t fade) {
  buffer_[0] = notes_long[(note % 12) << 1];
  buffer_[1] = notes_long[1 + ((note % 12) << 1)];
  buffer_[1] = buffer_[1] == ' ' ? octave[note / 12] : buffer_[1];
  buffer_[2] = '\0';
  display_.Print(buffer_, buffer_, brightness, fade);
}

void Ui::PrintPushItNote() {
  PrintNote(push_it_note_);
}

void Ui::PrintLearning() {
  display_.Print("++");
}

void Ui::PrintFactoryTesting() {
  switch (factory_testing_display_) {
    case UI_FACTORY_TESTING_DISPLAY_EMPTY:
      display_.Print("\xff\xff");
      break;
      
    case UI_FACTORY_TESTING_DISPLAY_NUMBER:
      {
        strcpy(buffer_, "00");
        buffer_[0] += factory_testing_number_ / 10;
        buffer_[1] += factory_testing_number_ % 10;
        display_.Print(buffer_);
      }
      break;
      
    case UI_FACTORY_TESTING_DISPLAY_CLICK:
      display_.Print("OK");
      break;
      
    case UI_FACTORY_TESTING_DISPLAY_SW_1:
    case UI_FACTORY_TESTING_DISPLAY_SW_2:
    case UI_FACTORY_TESTING_DISPLAY_SW_3:
      {
        strcpy(buffer_, "B1");
        buffer_[1] += factory_testing_display_ - UI_FACTORY_TESTING_DISPLAY_SW_1;
        display_.Print(buffer_);
      }
      break;
  }
}

void Ui::SplashOn(Splash splash) {
  splash_ = splash;
  refresh_was_automatic_ = false;
  queue_.Touch(); // Reset idle timer
  display_.set_blink(false);
}

void Ui::SplashString(const char* text) {
  display_.Print(text);
  SplashOn(SPLASH_STRING);
  display_.Scroll();
}

void Ui::SplashPartString(const char* label, uint8_t part) {
  strcpy(buffer_, label);
  buffer_[2] = '\0';
  display_.Print(buffer_);
  SplashOn(SPLASH_PART_STRING, part);
}

void Ui::SplashSetting(const Setting& s, uint8_t part) {
  splash_setting_def_ = &s;

  setting_defs.Print(s, multi.GetSettingValue(s, splash_part_), buffer_);
  display_.Print(buffer_, buffer_, UINT16_MAX, GetFadeForSetting(s));
  display_.Scroll();
  SplashOn(SPLASH_SETTING_VALUE, part);
}

void Ui::CrossfadeBrightness(uint32_t fade_in_start_time, uint32_t fade_out_end_time, bool fade_in) {
  uint16_t brightness = UINT16_MAX;
  uint32_t fade_in_elapsed = queue_.idle_time() - fade_in_start_time;
  uint32_t fade_out_remaining = fade_out_end_time - queue_.idle_time();
  if (fade_in_elapsed < kCrossfadeMsec && fade_in) {
    brightness = UINT16_MAX * fade_in_elapsed / kCrossfadeMsec;
  } else if (fade_out_remaining < kCrossfadeMsec) {
    brightness = UINT16_MAX * fade_out_remaining / kCrossfadeMsec;
  }
  display_.set_brightness(brightness, false);
}

// Generic Handlers
void Ui::OnLongClick(const Event& e) {
  switch (mode_) {
    case UI_MODE_MAIN_MENU:
    case UI_MODE_LOAD_SELECT_PROGRAM:
    case UI_MODE_SAVE_SELECT_PROGRAM:
    case UI_MODE_SWAP_SELECT_PART:
      mode_ = UI_MODE_PARAMETER_SELECT;
      break;
      
    default:
      previous_mode_ = mode_;
      mode_ = UI_MODE_MAIN_MENU;
      command_index_ = 0;
      break;
  }
}

void Ui::OnClick(const Event& e) {
  if (&setting() == &setting_defs.get(SETTING_MENU_SETUP)) {
    current_menu_ = &setup_menu_;
    return;
  } else if (&setting() == &setting_defs.get(SETTING_MENU_OSCILLATOR)) {
    current_menu_ = &oscillator_menu_;
    return;
  } else if (&setting() == &setting_defs.get(SETTING_MENU_ENVELOPE)) {
    current_menu_ = &envelope_menu_;
    return;
  } else if (current_menu_ != &live_menu_ && mode_ == UI_MODE_PARAMETER_EDIT) {
    current_menu_ = &live_menu_;
  }
  mode_ = modes_[mode_].next_mode; 
}

void Ui::OnIncrement(const Event& e) {
  Mode* mode = &modes_[mode_];
  if (!mode->incremented_variable) {
    return;
  }
  int8_t v = *mode->incremented_variable;
  v += e.data;
  CONSTRAIN(v, mode->min_value, mode->max_value);
  *mode->incremented_variable = v;
}

// Specialized Handlers
void Ui::OnClickMainMenu(const Event& e) {
  if (commands_[command_index_].function) {
    (this->*commands_[command_index_].function)();
  }
  mode_ = commands_[command_index_].next_mode;
}

void Ui::OnClickLoadSave(const Event& e) {
  if (program_index_ == kNumPrograms) {
    program_index_ = active_program_;  // Cancel
  } else {
    active_program_ = program_index_;
    if (mode_ == UI_MODE_SAVE_SELECT_PROGRAM) {
      storage_manager.SaveMulti(program_index_);
      strcpy(buffer_, "S1");
    } else {
      storage_manager.LoadMulti(program_index_);
      strcpy(buffer_, "L1");
    }
    buffer_[1] += program_index_;
    SplashString(buffer_);
  }
  mode_ = UI_MODE_PARAMETER_SELECT;
}

void Ui::OnClickSwapPart(const Event& e) {
  multi.SwapParts(active_part_, swap_part_index_);
  buffer_[0] = active_part_ + '1';
  buffer_[1] = swap_part_index_ + '1';
  SplashString(buffer_);
  mode_ = UI_MODE_PARAMETER_SELECT;
}

void Ui::OnClickCalibrationSelectVoice(const Event& e) {
  if (calibration_voice_ == kNumCVOutputs) {
    mode_ = UI_MODE_PARAMETER_SELECT;
    calibration_voice_ = 0;
    storage_manager.SaveCalibration();
  } else {
    mode_ = UI_MODE_CALIBRATION_SELECT_NOTE;
  }
  calibration_note_ = 0;
}

void Ui::OnClickCalibrationSelectNote(const Event& e) {
  if (calibration_note_ == kNumOctaves) {
    mode_ = UI_MODE_CALIBRATION_SELECT_VOICE;
    calibration_note_ = 0;
  } else {
    mode_ = UI_MODE_CALIBRATION_ADJUST_LEVEL;
  }
}

void Ui::OnClickRecording(const Event& e) {
  if (recording_part().looped()) { return; }

  if (push_it_) {
    if (!recording_part().overdubbing()) {
      multi.PushItNoteOff(push_it_note_);
    }
    push_it_ = false;
    mutable_recording_part()->RecordStep(SequencerStep(push_it_note_, 100));
  } else {
    const SequencerStep& step = recording_part().sequencer_settings().step[recording_part().recording_step()];
    if (step.has_note()) {
      push_it_note_ = step.note();
    } else {
      push_it_note_ = recording_part().TransposeInputPitch(kC4);
      multi.PushItNoteOn(push_it_note_);
    }
    push_it_ = true;
  }
}

void Ui::OnClickLearning(const Event& e) {
  multi.StopLearning();
  mode_ = UI_MODE_PARAMETER_SELECT;
}

void Ui::OnClickFactoryTesting(const Event& e) {
  factory_testing_display_ = UI_FACTORY_TESTING_DISPLAY_CLICK;
}

void Ui::OnIncrementParameterSelect(const Event& e) {
  current_menu_->increment_index(e.data);
}

void Ui::OnIncrementParameterEdit(const stmlib::Event& e) {
  int16_t value = multi.GetSettingValue(setting(), active_part_);
  value += e.data;
  multi.ApplySetting(setting(), active_part_, value);
}

void Ui::OnIncrementCalibrationAdjustment(const stmlib::Event& e) {
  CVOutput* voice = multi.mutable_cv_output(calibration_voice_);
  int32_t code = voice->calibration_dac_code(calibration_note_);
  code -= e.data * (switches_.pressed(2) ? 32 : 1);
  CONSTRAIN(code, 0, 65535);
  voice->set_calibration_dac_code(calibration_note_, code);
}

void Ui::OnIncrementRecording(const stmlib::Event& e) {
  if (recording_part().looped()) {
    mutable_recording_part()->mutable_looper().pos_offset += e.data << 9;
    Settings::PrintInteger(buffer_, recording_part().looper().pos_offset >> 9);
    SplashString(buffer_);
    return;
  }

  if (push_it_) {
    if (recording_part().overdubbing()) {
      push_it_note_ += e.data;
      CONSTRAIN(push_it_note_, 0, 127);
      mutable_recording_part()->ModifyNoteAtCurrentStep(push_it_note_);
    } else {
      OnIncrementPushItNote(e);
    }
  } else {
    mutable_recording_part()->increment_recording_step_index(e.data);
  }
}

void Ui::OnIncrementPushItNote(const stmlib::Event& e) {
  int16_t previous_note = push_it_note_;
  push_it_note_ += e.data;
  CONSTRAIN(push_it_note_, 0, 127);
  if (push_it_note_ != previous_note) {
    multi.PushItNoteOn(push_it_note_);
    multi.PushItNoteOff(previous_note);
  }
}

void Ui::OnIncrementFactoryTesting(const Event& e) {
  factory_testing_display_ = UI_FACTORY_TESTING_DISPLAY_NUMBER;
  OnIncrement(e);
}

void Ui::StopRecording() {
  push_it_ = false;
  multi.StopRecording(active_part_);
}

void Ui::OnSwitchPress(const Event& e) {
  if (mode_ == UI_MODE_FACTORY_TESTING) {
    factory_testing_display_ = static_cast<UiFactoryTestingDisplay>(
        UI_FACTORY_TESTING_DISPLAY_SW_1 + e.control_id);
    return;
  }
  
  switch (e.control_id) {
    case UI_SWITCH_REC:
      {
        if (multi.recording()) {
          if (recording_mode_is_displaying_pitch_) {
            StopRecording();
            recording_mode_is_displaying_pitch_ = false;
          } else {
            // Toggle pitch display on
            recording_mode_is_displaying_pitch_ = true;
          }
        } else {
          multi.StartRecording(active_part_);
        }
      }
      break;
      
    case UI_SWITCH_START_STOP_TIE:
      if (multi.recording()) {
        if (recording_part().looped()) {
          mutable_recording_part()->mutable_looper().RemoveOldestNote();
        } else {
          if (push_it_ && !recording_part().overdubbing()) {
            multi.PushItNoteOff(push_it_note_);
          }
          push_it_ = false;
          mutable_recording_part()->RecordStep(SEQUENCER_STEP_TIE);
        }
      } else {
        if (!multi.running()) {
          multi.Start(false);
        } else {
          multi.Stop();
        }
      }
      break;
      
    case UI_SWITCH_TAP_TEMPO_REST:
      if (multi.recording()) {
        if (recording_part().looped()) {
          mutable_recording_part()->mutable_looper().RemoveNewestNote();
        } else {
          if (push_it_ && !recording_part().overdubbing()) {
            multi.PushItNoteOff(push_it_note_);
          }
          push_it_ = false;
          mutable_recording_part()->RecordStep(SEQUENCER_STEP_REST);
        }
      } else {
        TapTempo();
      }
      break;
  }
}

HeldKeys& Ui::ActivePartHeldKeys() {
  return mutable_active_part()->MutableHeldKeysForUI();
}

void Ui::OnSwitchHeld(const Event& e) {
  bool recording_any = multi.recording();
  switch (e.control_id) {

    case UI_SWITCH_REC:
      if (recording_any) {
        mutable_recording_part()->DeleteRecording();
        SplashPartString("RX", active_part_);
      } else {
        HeldKeys &keys = ActivePartHeldKeys();
        if (keys.universally_sustainable) {
          mutable_active_part()->HeldKeysSustainOff(keys);
        } else if (multi.running() && keys.stack.most_recent_note_index()) {
          mutable_active_part()->HeldKeysSustainOn(keys);
        } else {
          if (push_it_) {
            multi.PushItNoteOff(push_it_note_);
            push_it_ = false;
            if (mode_ == UI_MODE_PUSH_IT_SELECT_NOTE) {
              mode_ = UI_MODE_PARAMETER_SELECT;
            }
          } else {
            mode_ = UI_MODE_PUSH_IT_SELECT_NOTE;
            push_it_ = true;
            push_it_note_ = kC4;
            multi.PushItNoteOn(push_it_note_);
          }
        }
      }
      break;

    case UI_SWITCH_START_STOP_TIE:
      if (recording_any) {
        if (recording_part().looped()) {
          // Do nothing
        } else {
          // Toggle slide flag on recording step
          SequencerStep* step = &mutable_recording_part()->mutable_sequencer_settings()->step[recording_part().recording_step()];
          step->set_slid(!step->is_slid());
        }
      } else {
        multi.set_next_clock_input_tick(0); // Reset song position
      }
      break;

    case UI_SWITCH_TAP_TEMPO_REST:
      if (recording_any) {
        mutable_recording_part()->toggle_seq_overwrite();
      } else {
        // Increment active part
        active_part_ = (1 + active_part_) % multi.num_active_parts();
        if (multi.recording()) {
          strcpy(buffer_, "1R");
          buffer_[0] += active_part_;
          buffer_[2] = '\0';
          display_.Print(buffer_);
        } else {
          PrintPartAndPlayMode(active_part_);
        }
        SplashString(buffer_);
      }
      break;

    default:
      break;
  }
}

void Ui::DoInitCommand() {
  multi.Init(false);
}

void Ui::DoDumpCommand() {
  storage_manager.SysExSendMulti();
}

void Ui::DoLearnCommand() {
  multi.StartLearning();
}

const uint32_t kTapDeltaMax = 1500; // 40 BPM

void Ui::TapTempo() {
  uint32_t tap_time = system_clock.milliseconds();
  uint32_t delta = tap_time - previous_tap_time_;
  if (delta < kTapDeltaMax) {
    if (delta < 250) {
      delta = 250;
    }
    ++tap_tempo_count_;
    tap_tempo_sum_ += delta;
    SetTempo(tap_tempo_count_ * 60000 / tap_tempo_sum_);
  } else {
    // Treat this as a first tap
    tap_tempo_resolved_ = false;
    tap_tempo_count_ = 0;
    tap_tempo_sum_ = 0;
  }
  previous_tap_time_ = tap_time;
}

void Ui::SetTempo(uint8_t value) {
  tap_tempo_resolved_ = true;
  multi.Set(MULTI_CLOCK_TEMPO, value);
  multi.ApplySettingAndSplash(setting_defs.get(SETTING_CLOCK_TEMPO), active_part_, value);
}

void Ui::DoEvents() {
  bool refresh_display = false;
  bool scroll_display = false;

  if (active_part_ >= multi.num_active_parts()) {
    // Handle layout change
    active_part_ = multi.num_active_parts() - 1;
  }
  if (multi.recording() && multi.recording_part() != active_part_) {
    // If recording state was changed by CC
    active_part_ = multi.recording_part();
    recording_mode_is_displaying_pitch_ = false;
  }
  
  while (queue_.available()) {
    Event e = queue_.PullEvent();
    const Mode& mode = modes_[mode_];
    splash_ = SPLASH_NONE; // Exit splash on any input
    if (e.control_type == CONTROL_ENCODER_CLICK) {
      if (in_recording_mode()) {
        OnClickRecording(e);
      } else {
        (this->*mode.on_click)(e);
      }
    } else if (e.control_type == CONTROL_ENCODER) {
      if (in_recording_mode()) {
        OnIncrementRecording(e);
      } else {
        (this->*mode.on_increment)(e);
      }
    } else if (e.control_type == CONTROL_ENCODER_LONG_CLICK) {
      OnLongClick(e);
    } else if (e.control_type == CONTROL_SWITCH) {
      OnSwitchPress(e);
    } else if (e.control_type == CONTROL_SWITCH_HOLD) {
      OnSwitchHeld(e);
    }
    refresh_display = true;
    refresh_was_automatic_ = false;
    scroll_display = true;
  }

  if (!tap_tempo_resolved_) {
    uint32_t delta = system_clock.milliseconds() - previous_tap_time_;
    if (delta > (kTapDeltaMax << 1)) {
      // If we never got a second tap ... 
      SetTempo(TEMPO_EXTERNAL);
    }
  }

  if (multi.recording()) {
    refresh_display = true;
  }

  if (mode_ == UI_MODE_LEARNING && !multi.learning()) {
    OnClickLearning(Event());
  }

  if (splash_) { // Check whether to end this splash (and maybe chain another)
    if (display_.scrolling() || queue_.idle_time() < kRefreshMsec) {
      // If scrolling, fade-out never begins, we will just exit splash after scrolling
      CrossfadeBrightness(0, display_.scrolling() ? -1 : kRefreshMsec, refresh_was_automatic_);
      return; // Splash isn't over yet
    }

    // Chaining
    if (splash_ == SPLASH_SETTING_VALUE) {
      display_.Print(splash_setting_def_->short_name);
      SplashOn(SPLASH_SETTING_NAME);
      // NB: we don't scroll the setting name
      refresh_was_automatic_ = true;
      return;
    } else if (splash_ == SPLASH_SETTING_NAME || splash_ == SPLASH_PART_STRING) {
      strcpy(buffer_, "1C");
      buffer_[0] += splash_part_;
      buffer_[2] = '\0';
      SplashString(buffer_);
      refresh_was_automatic_ = true;
      return;
    }
    // Exit splash
    splash_ = SPLASH_NONE;
    refresh_display = true;
    refresh_was_automatic_ = true;
  }

  if (!display_.scrolling() && queue_.idle_time() > kRefreshMsec) {
    factory_testing_display_ = UI_FACTORY_TESTING_DISPLAY_EMPTY;
    refresh_display = true;
  }

  if (refresh_display) {
    queue_.Touch();
    if (in_recording_mode()) {
      if (active_part().looped()) {
        PrintLoopSequencerStatus();
      } else {
        PrintStepSequencerStatus();
      }
    } else {
      (this->*modes_[mode_].refresh_display)();
    }
    if (scroll_display) {
      display_.Scroll();
    }
    display_.set_blink(
        mode_ == UI_MODE_CALIBRATION_ADJUST_LEVEL ||
        mode_ == UI_MODE_LEARNING
    );
    return;
  }
  if (display_.scrolling()) { return; }

  bool print_command = mode_ == UI_MODE_LOAD_SELECT_PROGRAM || mode_ == UI_MODE_SAVE_SELECT_PROGRAM;
  bool print_latch =
    (mode_ == UI_MODE_PARAMETER_SELECT || mode_ == UI_MODE_PARAMETER_EDIT) &&
    active_part().midi_settings().sustain_mode != SUSTAIN_MODE_OFF &&
    ActivePartHeldKeys().stack.most_recent_note_index();
  bool print_active_part = (mode_ == UI_MODE_PARAMETER_SELECT && multi.num_active_parts() > 1) || mode_ == UI_MODE_SWAP_SELECT_PART;

  // If we're not scrolling and it's not yet time to refresh, print latch or part
  bool print_any = print_command || print_latch || print_active_part;
  bool print_last_third = print_any;
  bool print_middle_third = print_latch && print_active_part;
  uint16_t begin_middle_third = kRefreshMsec / 3;
  uint16_t begin_last_third = kRefreshMsec * 2 / 3;
  if (print_last_third && queue_.idle_time() >= begin_last_third) {
    if (print_active_part) {
      PrintPartAndPlayMode(active_part_);
      display_.Print(buffer_, buffer_);
    } else if (print_latch) {
      PrintLatch();
    } else if (print_command) {
      PrintCommandName();
    }
    CrossfadeBrightness(begin_last_third, kRefreshMsec, true);
  } else if (print_middle_third && queue_.idle_time() >= begin_middle_third) {
    PrintLatch();
    CrossfadeBrightness(begin_middle_third, begin_last_third, true);
  } else {
    if (print_middle_third) CrossfadeBrightness(0, begin_middle_third, true);
    else if (print_last_third) CrossfadeBrightness(0, begin_last_third, true);
    // TODO if we just finished scrolling, ideally we would fade-in here, but
    // finishing scroll doesn't reset the idle time
    else CrossfadeBrightness(0, -1, refresh_was_automatic_);
  }
}

uint16_t Ui::GetFadeForSetting(const Setting& setting) {
return (setting.unit == SETTING_UNIT_TEMPO)
    /*
    For refresh at 1kHz
    increment
      = (bpm / 60) * (2^16 / 1000)
      = bpm * 2^16 / 60000
      = bpm * 2^11 / (60000 / 2^5)
      = bpm * 2^11 / 1875
    */
    ? (multi.tempo() << 11) / 1875
    : 0;
}

const uint8_t kNotesPerDisplayChar = 3;
// See characters.py for mask-to-segment mapping
const uint16_t kHoldDisplayMasks[2][3] = {
  {0x0400, 0x0100, 0x4000}, // Top tick marks
  {0x0800, 0x0010, 0x2000}, // Bottom tick marks
};
void Ui::PrintLatch() {
  const HeldKeys& keys = ActivePartHeldKeys();
  uint16_t masks[kDisplayWidth];
  std::fill(&masks[0], &masks[kDisplayWidth], 0);
  uint8_t note_ordinal = 0, display_pos = 0;
  uint8_t note_index = keys.stack.most_recent_note_index();
  stmlib::NoteEntry note_entry;
  bool blink = system_clock.milliseconds() % 160 < 80;
  while (note_index) {
    if (note_ordinal >= (kNotesPerDisplayChar << 1)) break;
    display_pos = note_ordinal < kNotesPerDisplayChar ? 0 : 1;

    note_entry = keys.stack.note(note_index);
    bool sustained = keys.IsSustained(note_entry);
    bool top;
    if (sustained) {
      top = keys.stop_sustained_notes_on_next_note_on ? blink : true;
    } else {
      top = keys.IsSustainable(note_index);
    }
    uint8_t index_within_char = note_ordinal % kNotesPerDisplayChar;
    uint16_t mask;
    if (top) {
      mask = kHoldDisplayMasks[0][index_within_char];
      masks[display_pos] |= mask;
    }
    if (!sustained) {
      mask = kHoldDisplayMasks[1][index_within_char];
      masks[display_pos] |= mask;
    }

    note_index = note_entry.next_ptr;
    ++note_ordinal;
  }
  display_.PrintMasks(masks);
}

void Ui::PrintDebugByte(uint8_t byte) {
  char buffer[3];
  buffer[2] = '\0';
  buffer[0] = hexadecimal[byte >> 4];
  buffer[1] = hexadecimal[byte & 0xf];
  display_.Print(buffer);
  queue_.Touch();
}

void Ui::PrintDebugInt32(int32_t value) {
  // Print a "+" or "-" followed by a hex representation value
  char buffer[11];
  buffer[10] = '\0';
  buffer[0] = value < 0 ? '-' : '+';
  value = value < 0 ? -value : value;
  for (int i = 9; i > 0; --i) {
    buffer[i] = hexadecimal[value & 0xf];
    value >>= 4;
  }
  display_.Print(buffer);
  display_.Scroll();
  queue_.Touch();
}

void Ui::PrintInt32E(int32_t value) {
  stmlib::int32E(value, buffer_, sizeof(buffer_));
  display_.Print(buffer_);
  display_.Scroll();
  queue_.Touch();
}

/* extern */
Ui ui;

}  // namespace yarns
