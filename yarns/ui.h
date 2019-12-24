// Copyright 2013 Emilie Gillet.
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

#ifndef YARNS_UI_H_
#define YARNS_UI_H_

#include "stmlib/stmlib.h"

#include "stmlib/ui/event_queue.h"

#include "yarns/drivers/channel_leds.h"
#include "yarns/drivers/display.h"
#include "yarns/drivers/encoder.h"
#include "yarns/drivers/switches.h"

#include "yarns/settings.h"
#include "yarns/storage_manager.h"

namespace yarns {
  
const uint8_t kNumPrograms = 8;

enum UiMode {
  UI_MODE_PARAMETER_SELECT,
  UI_MODE_PARAMETER_EDIT,
  UI_MODE_MAIN_MENU,
  UI_MODE_LOAD_SELECT_PROGRAM,
  UI_MODE_SAVE_SELECT_PROGRAM,
  UI_MODE_CALIBRATION_SELECT_VOICE,
  UI_MODE_CALIBRATION_SELECT_NOTE,
  UI_MODE_CALIBRATION_ADJUST_LEVEL,
  UI_MODE_RECORDING,
  UI_MODE_OVERDUBBING,
  UI_MODE_PUSH_IT_SELECT_NOTE,
  UI_MODE_LEARNING,
  UI_MODE_FACTORY_TESTING,
  UI_MODE_SPLASH,
  UI_MODE_LAST
};

enum MainMenuEntry {
  MAIN_MENU_LOAD,
  MAIN_MENU_SAVE,
  MAIN_MENU_INIT,
  MAIN_MENU_LEARN,
  MAIN_MENU_DUMP,
  MAIN_MENU_CALIBRATE,
  MAIN_MENU_EXIT,
  MAIN_MENU_LAST
};

enum UiSwitch {
  UI_SWITCH_REC,
  UI_SWITCH_START_STOP,
  UI_SWITCH_TIE = UI_SWITCH_START_STOP,
  UI_SWITCH_TAP_TEMPO,
  UI_SWITCH_REST = UI_SWITCH_TAP_TEMPO,
};

enum UiFactoryTestingDisplay {
  UI_FACTORY_TESTING_DISPLAY_EMPTY,
  UI_FACTORY_TESTING_DISPLAY_NUMBER,
  UI_FACTORY_TESTING_DISPLAY_CLICK,
  UI_FACTORY_TESTING_DISPLAY_SW_1,
  UI_FACTORY_TESTING_DISPLAY_SW_2,
  UI_FACTORY_TESTING_DISPLAY_SW_3,
};

class Ui {
 public:
  typedef void (Ui::*CommandFn)();
  typedef void (Ui::*HandlerFn)(const stmlib::Event& event);
  typedef void (Ui::*PrintFn)();
  
  Ui() { }
  ~Ui() { }
  
  void Init();
  void Poll();
  void PollSwitch(const UiSwitch ui_switch, uint32_t& press_time, bool& long_press_event_sent);
  void PollFast() {
    display_.RefreshFast();
  }
  void DoEvents();
  void FlushEvents();

  void Print(const char* text) {
    display_.Print(text, text);
  }

  void PrintDebugByte(uint8_t byte) {
    char buffer[3];
    char hexadecimal[] = ";";
    buffer[2] = '\0';
    buffer[0] = hexadecimal[byte >> 4];
    buffer[1] = hexadecimal[byte & 0xf];
    Print(buffer);
  }
  
  inline const Setting& setting() {
    return settings.setting(settings.menu()[setting_index_]);
  }
  inline bool calibrating() const {
    return mode_ == UI_MODE_CALIBRATION_SELECT_NOTE ||
        mode_ == UI_MODE_CALIBRATION_ADJUST_LEVEL;
  }
  inline bool factory_testing() const {
    return mode_ == UI_MODE_FACTORY_TESTING;
  }
  inline uint8_t calibration_voice() const { return calibration_voice_; }
  inline uint8_t calibration_note() const { return calibration_note_; }
  
  void StartFactoryTesting() {
    mode_ = UI_MODE_FACTORY_TESTING;
  }
  
 private:
  void RefreshDisplay();
  void TapTempo();
  inline Part* mutable_recording_part() {
    return mutable_active_part();
  }
  inline const Part& recording_part() const {
    return active_part();
  }
  inline Part* mutable_active_part() {
    return multi.mutable_part(settings.Get(GLOBAL_ACTIVE_PART));
  }
  inline const Part& active_part() const {
    return multi.part(settings.Get(GLOBAL_ACTIVE_PART));
  }
  
  // Generic Handler.
  void OnClick(const stmlib::Event& event);
  void OnLongClick(const stmlib::Event& e);
  void OnIncrement(const stmlib::Event& event);
  void OnSwitchPress(const stmlib::Event& event);
  void OnSwitchHeld(const stmlib::Event& event);
  
  // Specialized Handler.
  void OnClickMainMenu(const stmlib::Event& e);
  void OnClickLoadSave(const stmlib::Event& e);
  void OnClickCalibrationSelectVoice(const stmlib::Event& e);
  void OnClickCalibrationSelectNote(const stmlib::Event& e);
  void OnClickSelectRecordingPart(const stmlib::Event& e);
  void OnClickDeleteSequence(const stmlib::Event& e);
  void OnClickRecording(const stmlib::Event& e);
  void OnClickOverdubbing(const stmlib::Event& e);
  void OnClickLearning(const stmlib::Event& event);
  void OnClickFactoryTesting(const stmlib::Event& event);

  void OnIncrementParameterSelect(const stmlib::Event& e);
  void OnIncrementParameterEdit(const stmlib::Event& e);
  void OnIncrementCalibrationAdjustment(const stmlib::Event& e);
  void OnIncrementRecording(const stmlib::Event& e);
  void OnIncrementOverdubbing(const stmlib::Event& e);
  void OnIncrementPushItNote(const stmlib::Event& e);
  void OnIncrementFactoryTesting(const stmlib::Event& event);
  
  // Print functions.
  void PrintParameterName();
  void PrintParameterValue();
  void PrintMenuName();
  void PrintProgramNumber();
  void PrintCalibrationVoiceNumber();
  void PrintCalibrationNote();
  void PrintRecordingPart();
  void PrintDeleteSequence();
  void PrintRecordingStatus();
  void PrintNote(int16_t note);
  void PrintPushItNote();
  void PrintLearning();
  void PrintFactoryTesting();
  void PrintVersionNumber();
  void PrintRecordingStep(SequencerStep step);
  void PrintArpeggiatorMovementStep(SequencerStep step);
  void PrintSequencerPlayModeAndActivePart();
  
  void DoInitCommand();
  void DoDumpCommand();
  void DoLearnCommand();

  struct Command {
    const char* name;
    UiMode next_mode;
    CommandFn function;
  };
  
  struct Mode {
    HandlerFn on_increment;
    HandlerFn on_click;
    PrintFn refresh_display;
    UiMode next_mode;
    int8_t* incremented_variable;
    int8_t min_value;
    int8_t max_value;
  };
  
  static const Command commands_[MAIN_MENU_LAST];
  static Mode modes_[UI_MODE_LAST];
  
  stmlib::EventQueue<32> queue_;
  
  ChannelLeds leds_;
  Display display_;
  Encoder encoder_;
  Switches switches_;
  char buffer_[32];
  
  bool rec_long_press_event_sent_;
  uint32_t rec_press_time_;
  bool start_stop_long_press_event_sent_;
  uint32_t start_stop_press_time_;
  bool tap_tempo_long_press_event_sent_;
  uint32_t tap_tempo_press_time_;
  bool encoder_long_press_event_sent_;
  uint32_t encoder_press_time_;
  
  UiMode mode_;
  UiMode previous_mode_;
  
  int8_t setting_index_;
  int8_t command_index_;
  int8_t calibration_voice_;
  int8_t calibration_note_;
  int8_t program_index_;
  int8_t active_program_;
  bool push_it_;
  int16_t push_it_note_;
  uint8_t displayed_recording_step_index_;
  
  UiFactoryTestingDisplay factory_testing_display_;
  int8_t factory_testing_number_;
  uint16_t factory_testing_leds_counter_;
  
  uint32_t tap_tempo_sum_;
  uint32_t tap_tempo_count_;
  uint32_t previous_tap_time_;
  
  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace yarns

#endif // YARNS_UI_H_
