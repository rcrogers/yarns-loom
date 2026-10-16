// Host driver for the DISPLAY: yarns/drivers/display.cc, the real one,
// bit-banging into tools/uitest/gpio_stub.cc, which decodes the wire back into
// the segment word standing at each character position.
//
// The display is the last part of the module with no model off target, and it
// has shipped a defect: `1a956a45` found SetBlinkFrames calling itself -- the
// first Print overflowed the stack and the module stuck at boot -- and found
// the blink frames about to override the prefix flash. Both were caught by
// reading, not by a check, because there was none.
//
// -DAPPLICATION, because that is the build the module runs: without it
// RefreshSlow has no scrolling, no blink counter and no prefix flash, which is
// most of what there is to test.
//
// Usage: ./uitest <mode> [args] ; one line per RefreshSlow tick.
//   frames  <short> [prefix]     both sides of the blink, per position
//   blink   <short>              the same with set_blink(true) after Print
//   unblink <short>              set_blink(true) then (false), which must
//                                put the glyph's own frames back
//   trace   <short> <long> <n>   per tick: blink phase, scrolling, scroll step,
//                                whether the short buffer is displayed, the
//                                frame flag, and each position
//   scroll  <short> <long> <n>   the same, with Scroll() armed
//   prefix  <short> <prefix>     a whole prefix-flash cycle, the same shape
//   glyph   <chars>              each character's segments
#define TEST 1
#define private public
#include "yarns/drivers/display.h"
#include "tools/uitest/gpio_stub.h"
#include "yarns/resources.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace yarns;

namespace {

Display display;

// One RefreshSlow tick, then enough RefreshFast to light every position at
// full brightness. RefreshFast walks one position per PWM cycle and blanks it
// past the brightness threshold, so a whole PWM period per position is what
// guarantees each one is drawn.
// Returns the frame the DRAW ran under: RefreshSlow advances the counter, so
// the state before the tick is not the one RefreshFast reads.
const int kPwmMax = 64;
bool Tick() {
  display.RefreshSlow();
  const bool frame_high = display.frame_high();
  for (int i = 0; i < kPwmMax * kDisplayWidth; ++i) display.RefreshFast();
  return frame_high;
}

// Everything that decides what this tick shows, then what it showed. The
// first three select the buffer (RefreshSlow's job), the fourth is the frame
// swap (RefreshFast's), and a check needs both to say whether the swap reached
// somewhere it should not have.
void PrintTick(bool frame_high) {
  printf("%u %d %u %d",
         display.blink_counter_,
         display.scrolling_ ? 1 : 0,
         display.scrolling_step_,
         display.displayed_buffer_ == display.short_buffer_ ? 1 : 0);
  printf(" %d", frame_high ? 1 : 0);
  for (uint8_t i = 0; i < kDisplayWidth; ++i) printf(" %u", g_display_segments[i]);
  printf("\n");
}

// The two sides of the frame blink, as the panel shows them. frame_high()
// flips on its own counter, so the tick loop is run until it has been seen
// both ways rather than the member being poked.
void PrintBothFrames() {
  uint16_t high[kDisplayWidth] = {0}, low[kDisplayWidth] = {0};
  bool seen_high = false, seen_low = false;
  for (int t = 0; t < kFrameBlinkMask + 2 && !(seen_high && seen_low); ++t) {
    const bool is_high = Tick();
    for (uint8_t i = 0; i < kDisplayWidth; ++i) {
      (is_high ? high : low)[i] = g_display_segments[i];
    }
    (is_high ? seen_high : seen_low) = true;
  }
  for (uint8_t i = 0; i < kDisplayWidth; ++i) {
    printf("%u %u%c", high[i], low[i], i + 1 == kDisplayWidth ? '\n' : ' ');
  }
}

}  // namespace

int main(int argc, char** argv) {
  const char* mode = argc > 1 ? argv[1] : "frames";
  const char* short_string = argc > 2 ? argv[2] : "AB";

  display.Init();

  if (!strcmp(mode, "frames") || !strcmp(mode, "blink") ||
      !strcmp(mode, "unblink")) {
    const char prefix = (argc > 3 && argv[3][0]) ? argv[3][0] : '\0';
    display.Print(short_string, short_string, UINT16_MAX, 0, prefix);
    if (!strcmp(mode, "blink")) display.set_blink(true);
    if (!strcmp(mode, "unblink")) {
      display.set_blink(true);
      display.set_blink(false);
    }
    PrintBothFrames();
    return 0;
  }

  // A trailing `blink` on either trace runs set_blink(true) after the Print, so
  // a check can diff the two streams: blinking the whole field must reach the
  // short name and nothing else.
  if (!strcmp(mode, "trace") || !strcmp(mode, "scroll")) {
    const char* long_string = argc > 3 ? argv[3] : short_string;
    const int ticks = argc > 4 ? atoi(argv[4]) : 400;
    display.Print(short_string, long_string);
    if (!strcmp(mode, "scroll")) display.Scroll();
    if (argc > 5 && !strcmp(argv[5], "blink")) display.set_blink(true);
    for (int t = 0; t < ticks; ++t) PrintTick(Tick());
    return 0;
  }

  // A whole prefix-flash cycle: the flash runs on blink_counter_ over
  // kBlinkMask while the frame swap runs on frame_counter_ over
  // kFrameBlinkMask, so a check needs both counters' worth of ticks and the
  // frame each one drew under.
  if (!strcmp(mode, "prefix")) {
    const char prefix = argc > 3 && argv[3][0] ? argv[3][0] : 'P';
    display.Print(short_string, short_string, UINT16_MAX, 0, prefix);
    if (argc > 4 && !strcmp(argv[4], "blink")) display.set_blink(true);
    for (int t = 0; t < kBlinkMask * 6; ++t) PrintTick(Tick());
    return 0;
  }

  // What a character's segments are, so a check can name glyphs rather than
  // transcribing chr_characters.
  if (!strcmp(mode, "glyph")) {
    for (const char* p = short_string; *p; ++p) {
      printf("%u\n", chr_characters[static_cast<uint8_t>(*p)]);
    }
    return 0;
  }

  fprintf(stderr, "unknown mode: %s\n", mode);
  return 1;
}
