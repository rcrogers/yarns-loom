<!-- omit from toc -->
# Introduction

Loom is an alternate firmware for the [Yarns MIDI interface by Mutable Instruments](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/original_blurb/).  This firmware is designed to make Yarns more powerful and user-friendly.

This manual assumes that the reader is familiar with the original firmware, and explains how Loom is different.  For documentation of the original behavior, see the [Mutable Instruments manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/) and [firmware changelog](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/firmware/).

<!-- omit from toc -->
# Contents
- [Panel interface](#panel-interface)
    - [Submenus for settings](#submenus-for-settings)
    - [Active part control](#active-part-control)
    - [Part swap command](#part-swap-command)
    - [Save/load commands](#saveload-commands)
    - [Full display of integer setting values](#full-display-of-integer-setting-values)
    - [Tap tempo](#tap-tempo)
    - [Other changes to panel interface](#other-changes-to-panel-interface)
- [MIDI controller input](#midi-controller-input)
    - [MIDI Control Change (CC)](#midi-control-change-cc)
    - [New layouts](#new-layouts)
    - [Note processing](#note-processing)
    - [Hold pedal](#hold-pedal)
    - [Polyphonic voice allocation](#polyphonic-voice-allocation)
    - [Legato and portamento](#legato-and-portamento)
    - [Play mode](#play-mode)
- [Clocking](#clocking)
    - [Clock offset and scaling](#clock-offset-and-scaling)
    - [Controls for master clock](#controls-for-master-clock)
    - [Using master song position to cue synced events](#using-master-song-position-to-cue-synced-events)
- [Sequencer](#sequencer)
    - [Special controls while recording](#special-controls-while-recording)
    - [Step sequencer](#step-sequencer)
    - [Loop sequencer](#loop-sequencer)
- [Arpeggiator](#arpeggiator)
    - [Using `NOTE PRIORITY` to order `ARP DIRECTION`](#using-note-priority-to-order-arp-direction)
    - [Sequencer-driven arpeggiator](#sequencer-driven-arpeggiator)
    - [Sequencer-programmed arpeggiator](#sequencer-programmed-arpeggiator)
- [Modulation generators](#modulation-generators)
    - [Envelope](#envelope)
    - [Low-frequency oscillator (LFO)](#low-frequency-oscillator-lfo)
- [Audio oscillator](#audio-oscillator)
    - [Oscillator mode setting: `OM (OSCILLATOR MODE)` in `▽O (OSCILLATOR MENU)`](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu)
    - [Oscillator timbre settings](#oscillator-timbre-settings)
    - [Oscillator wave shape: `OS (OSCILLATOR SHAPE)`](#oscillator-wave-shape-os-oscillator-shape)



# Panel interface

### Submenus for settings
- `▽S (SETUP MENU)`: configuration, MIDI input/output
- `▽O (OSCILLATOR MENU)`: [oscillator mode](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu) and [timbre](#oscillator-timbre-settings)
- `▽A (AMPLITUDE MENU)`: [envelope](#envelope-adsr-settings-in-a-amplitude-menu) and [tremolo](#modulation-destinations-for-lfo-output)

### Active part control
- Used in multi-part layouts
- Display blinks the active part number and its [play mode](#play-mode)
- Hold `TAP` button to switch the active part
- The active part is used to route part-specific panel controls:
  - When `REC` button is pressed, the active part begins recording
  - When `REC` button is held, the active part latches its keys
  - When viewing or editing a setting, the active part's settings are used
- Replaces the `PART` setting from the original firmware

### Part swap command
- `*P PART SWAP SETTINGS` in main menu
- The selected part (selected by turning encoder) swaps its settings with the [active part](#active-part-control)
- Allows storing an alternate configuration in an inactive part

### Save/load commands
- Display blinks command name when picking a preset
- Display splashes the result after a save/load is executed
- Hold encoder to exit preset selection

### Full display of integer setting values
- When display shows an integer setting value that has three characters, blink a prefix character over the left displayed digit
- Three-digit unsigned integer `127` blinks `1` over `27`
- Two-digit signed integer `-42` blinks `-` over `42`
- Two-digit labeled integer `T23` blinks `T` over `23`

### Tap tempo
- If a single tap is received without follow-up, the tempo is set to use `EXTERNAL` clocking
- After setting tap tempo, display splashes the result

### Other changes to panel interface
- Print flat notes as lowercase character (instead of denoting flatness with `b`) so that octave can always be displayed
- Improved clock-sync of display fade for the `TE (TEMPO)` setting
- Channel LEDs have 64 brightness levels instead of 16
- Display has 64 brightness levels instead of 4



# MIDI controller input

### MIDI Control Change (CC)

<!-- omit from toc -->
#### Control change mode
- Global setting `CC (CONTROL CHANGE MODE)` sets how a CC's value is interpreted
- `OFF`: CCs are ignored
- `ABSOLUTE`: the CC's value becomes the new setting value
  - CC's value is down-scaled to match the setting's range
  - For use with traditional potentiometers
- `RELATIVE DIRECT`: the CC's value is added to (or subtracted from) the setting's current value
    - For use with endless encoders
    - Uses the "twos complement" standard for translating the CC's value into an increment
      - MIDI value 1 => setting + 1 (increment)
      - MIDI value 127 => setting - 1 (decrement)
    - Settings will increase or decrease by one value for each click of the encoder
    - Depending on how many values the setting has, the encoder may take anywhere from 2 to 128 clicks to scan the range of setting values
    - Send 0 to display current value without change
- `RELATIVE SCALED`: the CC's value is added to (or subtracted from) the value of a "virtual potentiometer"
    - Similar to `RELATIVE DIRECT`, but always takes 128 encoder clicks to scan the range of setting values, no matter how many setting values there are
    - Gives all encoders the same travel distance from minimum to maximum

<!-- omit from toc -->
#### Added CC types
- CC support for all new settings: see [Loom MIDI CC Implementation Chart](https://docs.google.com/spreadsheets/d/1V6CRqf_3FGTrNIjcU1ixBtzRRwqjIa1PaiqOFgf6olE/edit#gid=0)
- Recording controls
  - Start recording
  - Stop recording
  - Erase recorded sequence

<!-- omit from toc -->
#### Macro CCs that control combinations of settings
- Recording state and erase: off, on, triggered erase, immediate erase
- Sequencer mode and play mode: step sequencer, step arpeggiator, manual, loop arpeggiator, loop sequencer

<!-- omit from toc -->
#### Other CC improvements
- When CC is received, display splashes the result (value, setting abbreviation, and receiving part)
- Bug fix: bipolar settings can receive a negative value via CC



### New layouts
- `2+2` 3-part layout: 2-voice polyphonic part + two monophonic parts
- `2+1` 2-part layout: 2-voice polyphonic part + monophonic part with aux CV
- `*2` 3-part layout: 4-voice paraphonic part + monophonic part with aux CV + monophonic part without aux CV
  - Paraphonic part has 4x audio oscillators
  - Audio mode is always on for the paraphonic part
  - Output channels:
    1. CV: Part 1's 4x oscillators mixed to 1 audio output, Gate: Part 4's gate
    2. Part 2, monophonic CV/gate
    3. Part 2, modulation configurable via `3>`
    4. Part 3, monophonic CV/gate
- `3M` 3-part layout: 3 monophonic parts, plus clock on gate 4 and bar/reset on CV 4
- `*1` 2-part layout: 4-voice paraphonic part + monophonic part with aux CV
  - Output channels:
    1. CV: Part 1's 4x oscillators mixed to 1 audio output, Gate: Part 1's gate
    2. Part 2, monophonic CV/gate
    3. Part 1's aux CV, configurable via `CV`
    4. Part 2's aux CV, configurable via `CV`

### Note processing

<!-- omit from toc -->
#### Input transpose: `IT (INPUT TRANSPOSE OCTAVES)`
- Applies octave transposition to notes received by a part
- Effectively an octave switch for the controller

<!-- omit from toc -->
#### Filtering by velocity
- Part ignores notes with a velocity not between  `V> (VELOCITY MIN)` and `V< (VELOCITY MAX)`
- Settings are present for all layouts except `4V`
- Output velocity range is scaled to compensate for the restricted range imposed by input filtering

<!-- omit from toc -->
#### Sequencer input response: `SI (SEQ INPUT RESPONSE)`
- Sets how a part's sequencer responds to MIDI input when not recording
- `OFF`: ignores MIDI input
- `TRANSPOSE`: original firmware behavior
- `REPLACE`: sequencer controls note timing, but MIDI input overrides note pitch
- `DIRECT`: MIDI input is directly voiced, allowing accompaniment of a sequence

<!-- omit from toc -->
#### Recording behavior
- Any MIDI notes ignored by the recording part can be received by other parts

### Hold pedal

<!-- omit from toc -->
#### Display of pressed/sustained keys
- Display blinks tick marks to show the state of the [active part](#active-part-control)'s 6 most recently pressed keys, and how the hold pedal is affecting them
- Bottom-half tick: key is manually held, and will stop when released
- Full-height tick: key is manually held, and will be sustained when released
- Steady top-half tick: key is sustained, and will continue after the next key-press
- Flickering top-half tick: key is sustained, and will be stopped by the next key-press

<!-- omit from toc -->
#### Hold pedal mode per part
  - New part setting `HM (HOLD PEDAL MODE` sets each part's response to the hold pedal
  - `OFF`: pedal has no effect
  - `SUSTAIN`: sustains key-releases while pedal is down, and stops sustained notes on pedal-up
    - Original firmware behavior
  - `SOSTENUTO`: while pedal is down, sustains key-releases only on keys that were pressed before pedal-down; stops sustained notes on pedal-up
  - `LATCH`: sustains key-releases while pedal is down; stops sustained notes on key-press regardless of pedal state
    - Identical to button-controlled latching (triggered by holding `REC`)
  - `MOMENTARY LATCH`: like `LATCH`, but stop sustained notes on pedal-up, instead of on key-press
  - `CLUTCH`: while pedal is down, sustains key-releases only on keys that were pressed before pedal-down (like `SOSTENUTO`); while pedal is up, stops sustained notes on key-press (like `LATCH`)
    - Notes triggered while the pedal is down are not sustained and do not cause sustained notes to be stopped, which allows temporarily augmenting a sustained chord
  - `FILTER`: while pedal is down, ignores key-presses and sustains key-releases; while pedal is up, stops sustained notes on key-press
    - In combination with setting an opposite `HOLD PEDAL POLARITY` on two different parts, this allows the use of the pedal to select which part is controlled by the keyboard, while also supporting latching

<!-- omit from toc -->
#### Pedal polarity
- New part setting `HP (HOLD PEDAL POLARITY)` inverts a part's up/down pedal behavior
- Allows compatibility with any manufacturer's hold pedals
- Allows reversing up/down semantics for the selected `HOLD PEDAL MODE`
- [More information on negative (default) and positive pedal polarity](http://www.haydockmusic.com/reviews/sustain_pedal_polarity.html)

### Polyphonic voice allocation

<!-- omit from toc -->
#### Polyphonic `VOICING` options
- `sM STEAL LOWEST PRIORITY RELEASE MUTE` (`POLY` in original firmware)
    - Steal from the lowest-priority existing note IFF the incoming note has higher priority
    - Does not reassign voices to unvoiced notes on release
- `PRIORITY ORDER` (`SORTED` in original firmware)
    - Voice 1 always receives the note that has priority 1, voice 2 the note with priority 2, etc.
- `UR UNISON RELEASE REASSIGN` (`U1` in original firmware)
- `UM UNISON RELEASE MUTE` (`U2` in original firmware)
- `SM STEAL HIGHEST PRIORITY RELEASE MUTE` (`STEAL MOST RECENT` in original firmware)
    - Steal from the highest-priority existing note IFF the incoming note has higher priority
    - Does not reassign voices to unvoiced notes on release
- `sR STEAL LOWEST PRIORITY RELEASE REASSIGN`
    - Steal from the lowest-priority existing note IFF the incoming note has higher priority
    - Reassigns voices to unvoiced notes on release
- `SR STEAL HIGHEST PRIORITY RELEASE REASSIGN`
    - Steal from the highest-priority existing note IFF the incoming note has higher priority
    - Reassigns voices to unvoiced notes on release

<!-- omit from toc -->
#### `NP (NOTE PRIORITY)` changes
- Added new `FIRST` (oldest) setting to `NOTE PRIORITY`
- Polyphonic voicing respects note priority where applicable
- [Arpeggiator respects note priority](#using-note-priority-to-order-arp-direction)

<!-- omit from toc -->
#### Other polyphony changes
- Notes that steal a voice are considered legato
- Bug fix: `UNISON` allocates notes without gaps
- Bug fix: prevent unneeded reassignment/retrigger in `PRIORITY ORDER`/`UNISON` during a partial chord change
- `PRIORITY ORDER` and `UNISON RELEASE REASSIGN` reassign voices on `NoteOff` if there are unvoiced notes
- Allow monophonic parts to use all voicing modes

### Legato and portamento

<!-- omit from toc -->
#### Legato settings
- Replaced original `LEGATO MODE` setting (three values) with two on/off settings
- `LEGATO RETRIGGER`: are notes retriggered when played legato?
- `PORTAMENTO LEGATO ONLY`: is portamento applied on all notes, or only on notes played legato?
- Enables a new behavior: notes played legato are retriggered + portamento is applied only on notes played legato

<!-- omit from toc -->
#### Portamento setting range
- `PORTAMENTO` increases constant-time portamento when turning counter-clockwise from center, and increases constant-rate when turning clockwise
- Broadened setting range from 51 to 64 values per curve shape

### Play mode
- Setting `PM (PLAY MODE)` sets how each part generates MIDI and CV/gate output
- `MANUAL`: output is generated by MIDI input only
- `SEQUENCER`: output is generated by the [sequencer](#sequencer)
  - Sequencer output can be augmented by MIDI input, depending on the [sequencer input response](#sequencer-input-response-si-seq-input-response)
- `ARPEGGIATOR`: output is generated by the [arpeggiator](#arpeggiator)
  - The arpeggiator may be be driven by either a static arp pattern or the sequencer, depending on the [`ARP PATTERN` setting](#using-arp-pattern-to-enable-the-sequencer-driven-arpeggiator)
- Explicit `PLAY MODE` replaces the implicit controls of the original firmware (sequencer enabled if sequence has been recorded; arpeggiator enabled if arp range > 0)


# Clocking

### Clock offset and scaling

<!-- omit from toc -->
#### Master clock offset
- Setting `C+ CLOCK OFFSET` allows fine-tuning the master clock phase by ±63 ticks
- Applied in real time if the clock is running
- Designed to aid in multitrack recording
- Arithmetically, offset is applied **after** `INPUT CLK DIV`
- If offset is negative, the sequencer will not play any notes until tick 0 is reached

<!-- omit from toc -->
#### Sync ratios for synced events
- Clock divisions/multiplications are expressed as an integer ratio of the master clock tempo
- Part sequencer: `C/ CLOCK RATIO` sets the tempo of a part's sequencer, relative to the master clock tempo
- Output clock: `O/ OUTPUT CLOCK RATIO` sets the tempo of the clock gate output, relative to the master clock tempo
- LFO sync: [`LF LFO RATE`](#lfo-speed-and-sync-lf-lfo-rate) sets the part's base LFO tempo, relative to the master clock tempo
- 32 clock ratios available:
  - Slower than master clock: 1/8, 1/7, 1/6, 1/5, 2/9, 1/4, 2/7, 1/3, 3/8, 2/5, 3/7, 4/9, 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 6/7, 8/9 (20 total)
  - Faster than (or equal to) master clock: 1/1, 8/7, 6/5, 4/3, 3/2, 8/5, 2/1, 8/3, 3/1, 4/1, 6/1, 8/1 (12 total)


### Controls for master clock

<!-- omit from toc -->
#### Start/stop master clock
- Start master clock
  - Press `START/STOP` button while not running, or send MIDI Continue, or send MIDI Note On (if `CLOCK MANUAL START` disabled)
  - Display splashes `|>`
  - Starts from current song position
  - Bug fix: a "manual" clock start (`START/STOP` button or MIDI Start/Continue) supersedes an "automatic" clock start (MIDI Note On), preventing the clock from stopping after all notes are released
  - Bug fix: recording part responds to MIDI Start
- Stop master clock
  - Press `START/STOP` button while running, or send MIDI Stop
  - Display splashes `||`
  - Preserves song position
  - Bug fix: stopping the clock no longer stops manually held keys, though it still stops notes generated by the sequencer/arpeggiator

<!-- omit from toc -->
#### Set master clock's song position
- Reset song position
  - Hold `START/STOP` button, or start via MIDI Start
  - Display splashes `[]`
  - Updates song position to 0
- Cue to arbitrary song position
  - Send MIDI Song Position Pointer with the desired song position
  - Display splashes `<<` if moving earlier, `>>` if moving later, or `[]` if moving to 0
  - Note: this was tested with a Tascam Model 12 as a MIDI clock source.  If this doesn't work with your MIDI clock source, let me know!

### Using master song position to cue synced events

<!-- omit from toc -->
#### Deterministic clocking ensures predictable timing
- All synced events have a [sync ratio](#sync-ratios-for-synced-events) that establishes their relationship with the master clock
  - E.g.: given that an LFO has sync ratio 1/2, and the master song position is the 11th beat, the LFO knows that it should be halfway through its 6th cycle
- Past clock state is ignored, preventing temporary setting changes from causing permanent phase drift
- Allows synced clocks to maintain a consistent response to a given song position and sync ratio

<!-- omit from toc -->
#### How synced events respond to song position
- If you change the master song position (or a [sync ratio](#sync-ratios-for-synced-events)), synced events change phase accordingly
- Sequencers rewind or fast-forward to a recalculated position
- Synced LFOs slew to a recalculated phase
- Arpeggiator state is updated (based on any currently held arp chord) to the same state as if the song had played from the beginning

# Sequencer

### Special controls while recording
- Hold `REC` button to clear sequence
- Hold `TAP` button to toggle triggered-erase mode, which will clear the sequence as soon as a new note is recorded
- First press of `REC` button switches the display to show the pitch (or `RS`/`TI`) instead of the step number.  Press `REC` button a second time to exit recording

### Step sequencer

<!-- omit from toc -->
#### Step swing
- `SWING` setting works with all clock ratios
  - Swing is hardcoded for ratio 4:1 (f.k.a. sixteenth notes) in original firmware
- Swing can be applied to either even or odd steps
  - When `SWING` is counter-clockwise, odd steps (1, 3, 5...) are swung by the selected amount
  - When `SWING` is clockwise, even steps (2, 4, 6...) are swung (original firmware behavior)

<!-- omit from toc -->
#### Step selection interface
- Display brightens while the selected step is being played
- When using encoder to scroll through steps, wraps around if the end is reached

<!-- omit from toc -->
#### Step slide interface
- While recording, hold `START/STOP` button to toggle slide on the selected step
- If the selected step has slide, the display will show a fade effect
- When a `REST` or `TIE` is recorded, slide is removed from that step. If a real note is later overdubbed into this step, slide must be re-added manually

<!-- omit from toc -->
#### Other step sequencer changes
- General `STEP OFFSET` replaces the `EUCLIDEAN ROTATE` from the original firmware
  - Allows starting the step sequencer on any step
  - If using arpeggiator, arp state is pre-advanced to match
- Euclidean rhythms affect the step sequencer as well as the arpeggiator
  - Sequencer always advances, but euclidean rhythm will make some steps emit a rest instead of a note
- Max sequence length reduced from 64 to 30 steps, to free up space in the preset storage

### Loop sequencer

<!-- omit from toc -->
#### How it works
- Real-time recording captures the start and end of notes as you play them
- Chords and overlapping notes are played back according to the part's polyphony settings
- Start/end times are recorded at 13-bit resolution (1/8192 of the loop length)
- Loop length is set by `L- (LOOP LENGTH)` in quarter notes, combined with the part's `C/ CLOCK RATIO`
- Holds 30 notes max -- past this limit, overwrites oldest note

<!-- omit from toc -->
#### Recording a new loop
- Set `SM (SEQ MODE)` to `LOOP`
- Press `REC` to begin recording
- Send MIDI notes to the part to record them into the loop
- Display brightness fades to show loop progress (if not playing a note), or the progress of the currently playing note
- Channel LEDs show the quarter-phase of the loop

<!-- omit from toc -->
#### Editing a recorded loop
- Play more notes to overdub
- Press `START/STOP` button to erase the oldest note, or `TAP` button to erase the newest note
- Scroll the encoder to shift the loop phase by 1/128
  - Clockwise shifts notes earlier
  - Counter-clockwise shifts notes later
- Hold `REC` to erase the loop



# Arpeggiator

### Using `NOTE PRIORITY` to order `ARP DIRECTION`
- `NOTE PRIORITY` sets the basic ordering/sorting of the arp chord (the keys held on the keyboard)
- `ARP DIRECTION` sets the algorithm used to traverse the ordered arp chord
- By combining these, traditional arp behaviors can be achieved:
  - With `NOTE PRIORITY` = `LOW`: set `ARP DIRECTION` to `LINEAR` for "up," or `BOUNCE` for "up-down"
  - With `NOTE PRIORITY` = `FIRST`: set `ARP DIRECTION` to `LINEAR` for "played order"

### Sequencer-driven arpeggiator

<!-- omit from toc -->
#### Using `ARP PATTERN` to enable the sequencer-driven arpeggiator
- Clockwise = pattern-based arpeggiator rhythms (original firmware behavior)
- Counter-clockwise = sequencer-driven arpeggiator
  - `S1`-`S8` will reset the arpeggiator state after every 1-8 plays of the entire sequence (step or loop)
    - Reset helps generate more predictable arp patterns
  - `S0` never resets the arpeggiator state

<!-- omit from toc -->
#### How it works
- Like the normal arpeggiator, the arp chord is set by holding keys on the keyboard
- Unlike the normal arpeggiator, the sequencer-driven arpeggiator advances when the loop/step sequencer encounters a new note, instead of advancing on every clock pulse
- A sequence (either loop or step) must exist to produce arpeggiator output
- The arpeggiator respects rests/ties in a step sequence
- The velocity of the arpeggiator output is calculated by multiplying the velocities of the sequencer note and the held key

### Sequencer-programmed arpeggiator

<!-- omit from toc -->
#### General concepts for sequencer-programmed arp
  - The `JUMP` and `GRID` `ARP DIRECTION`s can interpret the sequencer pitch as a movement instruction
  - The arpeggiator always has some **active position** within the held arp chord, e.g. "the 3rd key of the chord"
  - Changes in the active position ("**movement**") are determined by the pitch of notes emitted by the sequencer 
    - If `ARP PATTERN` is not sequencer-driven, the sequencer pitch data is replaced by the position in the 16-step pattern rhythm 
  - Sequencer pitch is interpreted based on its:
    - Key **color** (is the key black or white?)
    - Shown **octave number** (with C as the first note of the octave)
    - **Pitch ordinal** within octave and color, e.g.
      - When the sequencer pitch is the 2nd white note of octave 5, the pitch ordinal is 2
      - When the sequencer pitch is the 4th black note of octave 2, the pitch ordinal is 4
  - The octave and color are used for different purposes by `JUMP` vs `GRID` (see below)
  - The pitch ordinal sets the minimum arp chord size for which the arpeggiator will emit a note
    - For a sequencer pitch with pitch ordinal N, the arpeggiator emits a note only if there are N or more keys in the arp chord, e.g.:
      - When the sequencer pitch is the 3rd white key of its octave, a note is emitted only if there are 3+ keys in the arp chord
      - When the sequencer pitch is the 1st black key of its octave, a note is emitted only if there are 1+ keys in the arp chord
    - Allows dynamic control of the arpeggiator's rhythmic pattern by varying the size of the arp chord

<!-- omit from toc -->
#### `JUMP` direction
  - Uses a combination of relative and absolute movement through the chord
  - Both colors advance the active position in the arp chord by octave-many places, wrapping around to the beginning of the chord
  - White steps emit a note from the active position in the arp chord, e.g.:
    - When the sequencer pitch is the 5th white note of octave 2, and the active position is 1 out of the arp chord's 6 notes, the active position is first incremented by 2 to become 3, and then the 3rd note of the arp chord is emitted
  - Black steps ignore the active position, instead treating the pitch ordinal as an absolute position in the arp chord, e.g.:
    - When the sequencer pitch is the 3rd black note of octave 5, the emitted note is the 3rd note of the arp chord, while the active position is incremented by 5

<!-- omit from toc -->
#### `GRID` direction
  - Simulates an X-Y coordinate system
  - The arp chord is mapped onto the grid in linear fashion, repeated as necessary to fill the grid
  - Octave sets the size of the grid: 4th octave => 4x4 grid (minimum 1x1)
  - White keys advance by 1 along the X-axis, moving left-to-right and wrapping back to the left
  - Black keys advance by 1 along the Y-axis, moving top-to-bottom and wrapping back to the top



# Modulation generators

### Envelope

<!-- omit from toc -->
#### Envelope ADSR settings (in `▽A (AMPLITUDE MENU)`)
- ADSR parameters: attack time, decay time, sustain level, and release time 
- Manual offset for ADSR parameters: `ATTACK INIT`, etc.
- Bipolar modulation of ADSR parameters by note velocity: `ATTACK MOD VEL`, etc.
- All curves are exponential
- Stage times range from 0.089 ms (4 samples) to 5 seconds

<!-- omit from toc -->
#### Modulating envelope's peak level
- "Peak" is the instantaneous point where attack ends and decay begins
- `PV (PEAK VEL MOD)` sets the velocity-sensitivity of the level of the peak
- Zero: peak level is always maximum (unity, i.e. the maximum sustain level)
- Positive values (clockwise of center): peak level is increasingly damped by low note velocity
- Negative values (counter-clockwise): peak level is increasingly damped by high note velocity

<!-- omit from toc -->
#### How the envelope adapts to interruptions
- Envelope adjusts to notes that start/end before the expected completion of a stage
- Problem: attack/release is closer to target than expected
  - Cause: note begins during release, or note ends while attack is rising toward sustain
  - Solution: shorten stage duration in proportion to remaining distance, maintaining nominal curve shape
- Problem: attack/release is farther from target than expected
  - Cause: note ends while decay is falling toward sustain
  - Solution: curve stays at maximum steepness until it catches up to the expected start value
- Problem: sustain level changes without a note release
  - Cause: legato play
  - Solution: use a decay stage to transition to new sustain level
- Problem: attack is going downward
  - Cause: after an early release from a high peak, new note begins with a low peak
  - Solution: skip to decay

<!-- omit from toc -->
#### Modulation destinations for envelope output
- [Oscillator gain](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu), when `OSCILLATOR MODE` is `ENVELOPED`
- [Oscillator timbre](#oscillator-timbre-settings), when oscillator is enabled
  - `TE (TIMBRE ENV MOD)`: attenuverter for the envelope's modulation of timbre
  - `TV (TIMBRE VEL MOD)` attenuverter for velocity's modulation of the timbre envelope (velocity can polarize the timbre envelope)
- Aux CV output: `ENVELOPE` (itself modulated by [tremolo LFO](#modulation-destinations-for-lfo-output))

### Low-frequency oscillator (LFO)

<!-- omit from toc -->
#### LFO speed and sync: `LF (LFO RATE)`
- Sets the base LFO rate for a part
- Counter-clockwise: increases [sync ratio](#sync-ratios-for-synced-events) relative to master clock
- Clockwise: increases free-running frequency from 0.125 Hz to 16 Hz
- F.k.a. `VIBRATO SPEED` in original firmware

<!-- omit from toc -->
#### LFO spread: dephase or detune
- Within a part, related LFOs can have a phase or frequency offset from each other
- `LT (LFO SPREAD TYPES)`: for each voice in the part, dephase/detune between the voice's LFO destinations (vibrato, tremolo, timbre)
- `LV (LFO SPREAD VOICES)`: dephase/detune LFOs between the part's voices
    - Only available in polyphonic/paraphonic layouts
- Counter-clockwise from center: dephase LFOs
    - Each LFO's phase is progressively more offset, by an amount ranging from 0° to 360° depending on the setting
    - Ideal for quadrature and three-phase modulation
    - When dephasing, the LFOs always share a common frequency
- Clockwise from center: detune LFOs
    - Each LFO's frequency is a multiple of the last, with that multiple being between 1x and 2x depending on the setting
    - Facilitates unstable, meandering modulation
- In polyphonic/paraphonic layouts, `LFO SPREAD TYPES` and `LFO SPREAD VOICES` can be used simultaneously, for up to 12 distinct LFOs

<!-- omit from toc -->
#### Modulation destinations for LFO output
- Vibrato: oscillator pitch, pitch CV
  - `VB (VIBRATO AMOUNT)` (in `▽S (SETUP MENU)`): attenuator for bipolar vibrato LFO
    - Allows vibrato control via panel interface if your keyboard doesn't have a modulation wheel
  - `VS (VIBRATO SHAPE)` (in `▽S (SETUP MENU)`): shape of the vibrato LFO
- Tremolo: [oscillator amplitude](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu), [`ENVELOPE` aux CV](#modulation-destinations-for-envelope-output)
  - `TR (TREMOLO DEPTH)` (in `▽A (AMPLITUDE MENU)`): attenuator for the unipolar tremolo LFO's reduction of gain
  - `TS (TREMOLO SHAPE)` (in `▽A (AMPLITUDE MENU)`): shape of the tremolo LFO
- Timbre: [oscillator timbre](#oscillator-timbre-settings)
  - `TL (TIMBRE LFO MOD)` (in `▽O (OSCILLATOR MENU)`): attenuator for the bipolar timbre LFO
  - `LS (TIMBRE LFO SHAPE)` (in `▽O (OSCILLATOR MENU)`): shape of the timbre LFO
- Aux CV outputs: `LFO`, `VIBRATO LFO` (unattenuated and attenuated versions of vibrato LFO)
- Shape options: triangle, down saw, up saw, square

# Audio oscillator

### Oscillator mode setting: `OM (OSCILLATOR MODE)` in `▽O (OSCILLATOR MENU)`
- `OFF`: no oscillator output
- `DRONE`: oscillator gain is modulated by tremolo LFO, but not by envelope
- `ENVELOPED`: oscillator gain is modulated by both [tremolo LFO](#modulation-destinations-for-lfo-output) and [envelope](#modulation-destinations-for-envelope-output)

### Oscillator timbre settings
- Each oscillator shape has a timbre parameter
- `TI (TIMBRE INITIAL)`: manually sets timbre offset
- [Modulation by envelope](#modulation-destinations-for-envelope-output)
- [Modulation by LFO](#modulation-destinations-for-lfo-output)

### Oscillator wave shape: `OS (OSCILLATOR SHAPE)`
- `*-` Filtered noise
  - Timbre: filter cutoff (resonance is set by note pitch)
  - Shapes: low-pass, notch, band-pass, high-pass
- `┌┐CZ` Phase distortion, resonant pulse
  - Timbre: filter cutoff
  - Shapes: low-pass, peaking, band-pass, high-pass
- `|⟍CZ` Phase distortion, resonant saw
  - Timbre: filter cutoff
  - Shapes: low-pass, peaking, band-pass, high-pass
- `-◝` state-variable filter, low-pass
  - Timbre: filter cutoff (resonance is constant)
  - Shapes: pulse, saw
- `-W` Pulse-width modulation
  - Timbre: pulse width
  - Shapes: pulse, saw
- `|⟍┌┐` Saw-pulse morph
  - Timbre: morph from saw to pulse
- `-$` Hard sync
  - Timbre: detunes the synced oscillator
  - Shapes: sine, pulse, saw
- `-F` Wavefolder
  - Timbre: folding amount
  - Shapes: sine, triangle
- `┴┴` Dirac comb
  - Timbre: harmonic content
- `ST` Compressed sine (`tanh`)
  - Timbre: compression amount
- `SX` Exponential sine
  - Timbre: exponentiation amount
- `FM` Frequency modulation
  - Timbre: modulation index
  - Shapes: 26 preset modulator ratios
    - 11 integer ratios, ordered from harmonic to inharmonic
    - 8 irrational numbers based on the inverse Minkowski question-mark function
    - 7 irrational divisions/multiples of pi
