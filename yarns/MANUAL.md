<!-- omit from toc -->
# Introduction

Loom is an alternate firmware for the [Yarns MIDI interface by Mutable Instruments](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/original_blurb/).  This firmware is designed to make Yarns more powerful and user-friendly.

This manual assumes that the reader is familiar with the original firmware, and explains how Loom is different.  For documentation of the original behavior, [see the Mutable Instruments manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/).

<!-- omit from toc -->
# Table of contents
- [Panel UI](#panel-ui)
    - [Active part control](#active-part-control)
    - [Tap tempo](#tap-tempo)
    - [Part swap command](#part-swap-command)
    - [Save/load commands](#saveload-commands)
    - [Full display of integer setting values](#full-display-of-integer-setting-values)
    - [Other UI changes](#other-ui-changes)
- [MIDI controller input](#midi-controller-input)
    - [Layouts](#layouts)
    - [Note filtering](#note-filtering)
      - [Input transpose: `IT (INPUT TRANSPOSE OCTAVES)`](#input-transpose-it-input-transpose-octaves)
      - [Velocity filtering](#velocity-filtering)
      - [Sequencer input response: `SI (SEQ INPUT RESPONSE)`](#sequencer-input-response-si-seq-input-response)
      - [Recording behavior](#recording-behavior)
    - [Hold pedal](#hold-pedal)
      - [Display of pressed/sustained keys](#display-of-pressedsustained-keys)
      - [Hold pedal mode per part](#hold-pedal-mode-per-part)
      - [Pedal polarity](#pedal-polarity)
    - [Polyphonic voice allocation](#polyphonic-voice-allocation)
      - [Polyphonic `VOICING` options](#polyphonic-voicing-options)
      - [`NP (NOTE PRIORITY)` changes](#np-note-priority-changes)
      - [Other polyphony changes](#other-polyphony-changes)
    - [Legato and portamento](#legato-and-portamento)
      - [Legato settings](#legato-settings)
      - [Portamento setting range](#portamento-setting-range)
    - [MIDI Control Change (CC)](#midi-control-change-cc)
      - [Control change mode](#control-change-mode)
      - [Added CC types](#added-cc-types)
      - [Macro CCs that control combinations of settings](#macro-ccs-that-control-combinations-of-settings)
      - [Other CC improvements](#other-cc-improvements)
- [Clock system](#clock-system)
    - [Clock offset and scaling](#clock-offset-and-scaling)
      - [Master clock offset](#master-clock-offset)
      - [Scaling ratios for synced clocks](#scaling-ratios-for-synced-clocks)
    - [Master clock controls](#master-clock-controls)
      - [Basic transport controls](#basic-transport-controls)
      - [Song position controls](#song-position-controls)
    - [Using song position to control clocked events](#using-song-position-to-control-clocked-events)
      - [Clocked events respond to song position changes](#clocked-events-respond-to-song-position-changes)
      - [Deterministic clocking ensures consistent and predictable timing](#deterministic-clocking-ensures-consistent-and-predictable-timing)
- [Sequencer](#sequencer)
    - [Special controls while recording](#special-controls-while-recording)
    - [Play mode](#play-mode)
    - [Step sequencer](#step-sequencer)
      - [Step swing](#step-swing)
      - [Step slide](#step-slide)
      - [Step selection UI](#step-selection-ui)
      - [Other step sequencer changes](#other-step-sequencer-changes)
    - [Loop sequencer](#loop-sequencer)
      - [How it works](#how-it-works)
      - [Recording a new loop](#recording-a-new-loop)
      - [Editing a recorded loop](#editing-a-recorded-loop)
- [Arpeggiator](#arpeggiator)
    - [Using `NOTE PRIORITY` to order `ARP DIRECTION`](#using-note-priority-to-order-arp-direction)
    - [Sequencer-driven arpeggiator](#sequencer-driven-arpeggiator)
      - [Using `ARP PATTERN` to enable the sequencer-driven arpeggiator](#using-arp-pattern-to-enable-the-sequencer-driven-arpeggiator)
      - [How it works](#how-it-works-1)
    - [Sequencer-programmed arpeggiator](#sequencer-programmed-arpeggiator)
      - [General concepts for sequencer-programmed arp](#general-concepts-for-sequencer-programmed-arp)
      - [`JUMP` direction](#jump-direction)
      - [`GRID` direction](#grid-direction)
- [Modulation generators](#modulation-generators)
    - [Envelope](#envelope)
      - [Envelope modulation destinations](#envelope-modulation-destinations)
      - [Envelope ADSR settings](#envelope-adsr-settings)
      - [Envelope peak level](#envelope-peak-level)
      - [How the envelope adapts to interruptions](#how-the-envelope-adapts-to-interruptions)
    - [LFO](#lfo)
      - [LFO modulation destinations](#lfo-modulation-destinations)
      - [LFO speed and sync: `LF (LFO RATE)`](#lfo-speed-and-sync-lf-lfo-rate)
      - [LFO spread: dephase or detune](#lfo-spread-dephase-or-detune)
- [Audio oscillator](#audio-oscillator)
    - [Oscillator mode setting: `OM (OSCILLATOR MODE)` in `▽O (OSCILLATOR MENU)`](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu)
    - [Oscillator timbre settings](#oscillator-timbre-settings)
    - [Oscillator wave shape: `OS (OSCILLATOR SHAPE)`](#oscillator-wave-shape-os-oscillator-shape)



# Panel UI

### Active part control
- Used in multi-part layouts
- Display blinks the active part number and its `PLAY MODE`
- Hold `TAP` to switch the active part
- The active part is used to route part-specific UI controls:
  - When `REC` is pressed, the active part begins recording
  - When `REC` is held, the active part latches its keys
  - When viewing or editing a setting, the active part's settings are used

### Tap tempo
- If a single tap is received without follow-up, the tempo is set to use external clocking
- After setting tap tempo, display splashes the result

### Part swap command
- `*P PART SWAP SETTINGS` in main menu
- Allows using an inactive part to store an alternate configuration

### Save/load commands
- Display blinks command name when picking a preset
- Display splashes the result after a save/load is executed
- Long press on encoder exits preset selection

### Full display of integer setting values
- When display shows an integer setting value that has three characters, blink a prefix character over the left displayed digit
- Three-digit unsigned integer `127` blinks `1` over `27`
- Two-digit signed integer `-42` blinks `-` over `42`
- Two-digit labeled integer `T23` blinks `T` over `23`

### Other UI changes
- Moved configuration-type settings into a submenu, accessed by opening `▽S (SETUP MENU)`
- Print flat notes as lowercase character (instead of denoting flatness with `b`) so that octave can always be displayed
- Improved clock-sync of display fade for the `TE (TEMPO)` setting
- Channel LEDs have 64 brightness levels instead of 16
- Display has 64 brightness levels instead of 4



# MIDI controller input

### Layouts
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

### Note filtering

#### Input transpose: `IT (INPUT TRANSPOSE OCTAVES)`
- Applies octave transposition to notes received by a part
- Effectively an octave switch for the controller

#### Velocity filtering
- Part ignores notes with a velocity not between  `V> (VELOCITY MIN)` and `V< (VELOCITY MAX)`
- Present for all layouts except 4V
- Output velocity range is scaled to compxensate for the restricted range imposed by input filtering

#### Sequencer input response: `SI (SEQ INPUT RESPONSE)`
- Changes how a playing sequence responds to manual input
- `OFF`: ignores keyboard input
- `TRANSPOSE`: original firmware behavior
- `REPLACE`: retains the sequence's rhythm, but overrides its pitch
- `DIRECT`: gives full use of the keyboard, allowing accompaniment of a sequence, etc.

#### Recording behavior
- Any MIDI notes ignored by the recording part can be received by other parts

### Hold pedal

#### Display of pressed/sustained keys
- Display blinks tick marks to show the state of the active part's 6 most recently pressed keys, and how the hold pedal is affecting them
- Bottom-half tick: key is manually held, and will stop when released
- Full-height tick: key is manually held, and will be sustained when released
- Steady top-half tick: key is sustained, and will continue after the next key-press
- Flickering top-half tick: key is sustained, and will be stopped by the next key-press

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

#### Pedal polarity
- New part setting `HP (HOLD PEDAL POLARITY)` inverts a part's up/down pedal behavior
- Allows compatibility with any manufacturer's hold pedals
- Allows reversing up/down semantics for the selected `HOLD PEDAL MODE`
- [More information on negative (default) and positive pedal polarity](http://www.haydockmusic.com/reviews/sustain_pedal_polarity.html)

### Polyphonic voice allocation

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
    - Does not reassign on release
- `sR STEAL LOWEST PRIORITY RELEASE REASSIGN`
    - Steal from the lowest-priority existing note IFF the incoming note has higher priority
    - Reassigns voices to unvoiced notes on release
- `SR STEAL HIGHEST PRIORITY RELEASE REASSIGN`
    - Steal from the highest-priority existing note IFF the incoming note has higher priority
    - Reassigns on release

#### `NP (NOTE PRIORITY)` changes
- Added new `FIRST` (oldest) setting to `NOTE PRIORITY`
- Voicing respects note priority wherever applicable

#### Other polyphony changes
- Notes that steal a voice are considered legato
- Bug fix: `UNISON` allocates notes without gaps
- Improve `PRIORITY`/`UNISON` to avoid unnecessary reassignment/retrigger of voices during a partial chord change
- `PRIORITY` and `UNISON RELEASE REASSIGN` reassign voices on `NoteOff` if there are unvoiced notes
- Allow monophonic parts to use all voicing modes

### Legato and portamento

#### Legato settings
- Replaced original `LEGATO MODE` setting (three values) with two on/off settings
- `LEGATO RETRIGGER`: are notes retriggered when played legato?
- `PORTAMENTO LEGATO ONLY`: is portamento applied on all notes, or only on notes played legato?
- Enables a new behavior: notes played legato are retriggered + portamento is applied only on notes played legato

#### Portamento setting range
- `PORTAMENTO` increases constant-time portamento when turning counter-clockwise from center, and increases constant-rate when turning clockwise
- Broadened setting range from 51 to 64 values per curve shape

### MIDI Control Change (CC)

#### Control change mode
- New global setting for `CC (CONTROL CHANGE MODE)`
- `OFF`: CCs are ignored
- `ABSOLUTE`: the setting is updated to the CC value
  - For use with traditional potentiometers
- `RELATIVE DIRECT`: the setting is directly incremented by the CC value
    - For use with endless encoders
    - Uses the "twos complement" standard for translating MIDI values into a relative change
      - MIDI value 1 => setting + 1 (increment)
      - MIDI value 127 => setting - 1 (decrement)
    - Settings will increase or decrease by one value for each click of the encoder
    - Depending on how many values the setting has, the encoder may take anywhere from 1 to 127 clicks to scan the range of setting values
    - Send 0 to display current value without change
- `RELATIVE SCALED`: the setting is incremented by 0-100% of the CC value
    - Similar to `RELATIVE DIRECT`, but always takes 127 encoder clicks to scan the range of setting values, no matter how many setting values there are
    - Effectively gives all controllers the same travel distance from minimum to maximum

#### Added CC types
- Recording controls: start/stop recording, erase recorded sequence
- CC support for all new settings

#### Macro CCs that control combinations of settings
- Recording state and erase: off, on, triggered erase, immediate erase
- Sequencer mode and play mode: step sequencer, step arpeggiator, manual, loop arpeggiator, loop sequencer

#### Other CC improvements
- The result of a received CC is splashed (value, setting abbreviation, and receiving part)
- Bug fix: bipolar settings can receive a negative value via CC
- [Implementation Chart](https://docs.google.com/spreadsheets/d/1V6CRqf_3FGTrNIjcU1ixBtzRRwqjIa1PaiqOFgf6olE/edit#gid=0)



# Clock system

### Clock offset and scaling

#### Master clock offset
- Setting `C+ CLOCK OFFSET` allows fine-tuning the master clock phase by ±63 ticks
- Applied in real time if the clock is running
- Designed to aid in multitrack recording
- Arithmetically, offset is applied **after** `INPUT CLK DIV`
- If offset is negative, the sequencer will not play any notes until tick 0 is reached

#### Scaling ratios for synced clocks
- Clock divisions/multiplications are expressed as an integer ratio of the master clock
- Part clock: `C/ CLOCK RATIO` sets the frequency of the part's sequencer clock, relative to the master clock
- Output clock: `O/ OUTPUT CLOCK RATIO` sets the frequency of the clock gate output, relative to the master clock
- LFO sync clock: when turned counter-clockwise from center, `LFO RATE` sets a part's LFO frequency, relative to the master clock
- 32 clock ratios available:
  - Slower (20): 1/8, 1/7, 1/6, 1/5, 2/9, 1/4, 2/7, 1/3, 3/8, 2/5, 3/7, 4/9, 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 6/7, 8/9
  - Equal or faster (12): 1/1, 8/7, 6/5, 4/3, 3/2, 8/5, 2/1, 8/3, 3/1, 4/1, 6/1, 8/1


### Master clock controls

#### Basic transport controls
- Start master clock
  - How: press `START/STOP` while not running, or send MIDI Continue, or send MIDI Note On (if `CLOCK MANUAL START` disabled)
  - Display splashes `|>`
  - Starts from current song position
  - Bug fix: a "manual" clock start (panel button or MIDI Start/Continue) can supersede an "automatic" clock start (MIDI Note On), preventing the clock from stopping after all notes are releases
  - Bug fix: recording part responds to MIDI Start
- Stop master clock
  - How: press `START/STOP` while running, or send MIDI Stop
  - Display splashes `||`
  - Preserves song position
  - Bug fix: stopping the clock no longer stops manually held keys, though it still stops notes generated by the sequencer/arpeggiator
- Reset master clock
  - How: long press `START/STOP`, or send MIDI Start
  - Updates song position to 0 (see below)

#### Song position controls
- Cueing: the song position can be updated by MIDI Song Position Pointer
- Display splashes `<<` (rewind), `>>` (fast-forward), or `[]` (reset/stop) depending on the new song position
- Note: this was tested with a Tascam Model 12 as a MIDI source, which implements cueing by sending MIDI Song Position Pointer followed by a MIDI Continue.  If this doesn't work with your MIDI source, let me know!

### Using song position to control clocked events

#### Clocked events respond to song position changes
- Sequencers advance to a phase (sequence position) calculated from the song position
- Synced LFOs slew to the calculated phase
- Arpeggiator state is updated (based on any currently held arp chord) to the same state as if the song had played from the beginning

#### Deterministic clocking ensures consistent and predictable timing
- Each synced clock continuously recalculates what its phase should be
- Target phase is based on sync ratio and song position
- Past clock state is ignored
- Prevents temporary setting changes from causing permanent phase drift
- Allows clocks to automatically lock on after changes to song position and sync ratio



# Sequencer

### Special controls while recording
- Hold `REC` to clear sequence
- Hold `TAP` to toggle triggered-erase mode, which will clear the sequence as soon as a new note is recorded
- First `REC` press switches the display to show the pitch (or `RS`/`TI`) instead of the step number (press again to exit recording)

### Play mode
- Setting `PM (PLAY MODE)` sets how each part generates MIDI and CV/gate output
- `MANUAL`: Output is generated by the MIDI controller only
- `SEQUENCER`: Output is generated by the sequencer
  - Sequencer output can be augmented by the MIDI controller depending on the [`SEQUENCER INPUT RESPONSE` setting](#event-routing-filtering-and-transformation)
- `ARPEGGIATOR`: Output is generated by the arpeggiator
  - The arpeggiator may be be driven by either a static arp pattern or the sequencer, depending on the [`ARP PATTERN` setting](#using-arp-pattern-to-enable-the-sequencer-driven-arpeggiator)

### Step sequencer

#### Step swing
- `SWING` works with all clock ratios, not just ratio 4:1 (f.k.a. sixteenth notes in original firmware)
- Swing can be applied to either even or odd steps
  - When `SWING` is counter-clockwise, odd steps (1, 3, 5...) are swung by the selected amount
  - When `SWING` is clockwise, even steps (2, 4, 6...) are swung (original firmware behavior)

#### Step slide
- While recording, hold `START` to toggle slide on the selected step
- If the selected step has slide, the display will show a fade effect
- When a `REST` or `TIE` is recorded, slide is removed from that step. If a real note is later overdubbed into this step, slide must be re-added manually

#### Step selection UI
- Display brightens while the selected step is being played
- When using encoder to scroll through steps, wraps around if the end is reached

#### Other step sequencer changes
- Replaced the `EUCLIDEAN ROTATE` setting with a more general `STEP OFFSET` -- allows starting the step sequencer on any step
- Euclidean rhythms can be applied to the step sequencer as well as the arpeggiator
- Capacity reduced from 64 to 30 notes, to free up space in the preset storage

### Loop sequencer

#### How it works
- Real-time recording captures the start and end of notes as you play them
- Chords and overlapping notes are played back according to the part's polyphony settings
- Start/end times are recorded at 13-bit resolution (1/8192 of the loop length)
- Loop length is set by `L- (LOOP LENGTH)` in quarter notes, combined with the part's `C/ CLOCK RATIO`
- Holds 30 notes max -- past this limit, overwrites oldest note

#### Recording a new loop
- Ensure `SM (SEQ MODE)` is set to `LOOP`
- Press `REC` to begin recording
- Play notes to record them into the loop
- Display brightness fades to show the remaining time in the loop (if not playing a note), or the remaining time in the latest note
- Channel LEDs show the quarter-phase of the loop

#### Editing a recorded loop
- Play more notes to overdub
- Press `START` to erase the oldest note, or `TAP` to erase the newest note
- Scroll the encoder to shift the loop phase by 1/128: clockwise shifts notes earlier, counter-clockwise shifts notes later
- Hold `REC` to erase the loop



# Arpeggiator

### Using `NOTE PRIORITY` to order `ARP DIRECTION`
- `NOTE PRIORITY` sets the basic ordering/sorting of the arp chord (the keys held on the keyboard)
- `ARP DIRECTION` sets the algorithm used to traverse the ordered arp chord
- By combining these, traditional arp behaviors can be achieved:
  - With `NOTE PRIORITY` = `LOW`: set `ARP DIRECTION` to `LINEAR` for "up," or `BOUNCE` for "up-down"
  - With `NOTE PRIORITY` = `FIRST`: set `ARP DIRECTION` to `LINEAR` for "played order"

### Sequencer-driven arpeggiator

#### Using `ARP PATTERN` to enable the sequencer-driven arpeggiator
- Clockwise = pattern-based arpeggiator rhythms (original firmware behavior)
- Counter-clockwise = sequencer-driven arpeggiator
  - `S1`-`S8` will reset the arpeggiator state after every 1-8 plays of the entire sequence (step or loop)
    - Reset helps generate more predictable arp patterns
  - `S0` never resets the arpeggiator state

#### How it works
- Like the normal arpeggiator, the arp chord is set by holding keys on the keyboard
- Unlike the normal arpeggiator, the sequencer-driven arpeggiator advances when the loop/step sequencer encounters a new note, instead of advancing on every clock pulse
- A sequence (either loop or step) must exist to produce arpeggiator output
- The arpeggiator respects rests/ties in a step sequence
- The velocity of the arpeggiator output is calculated by multiplying the velocities of the sequencer note and the held key

### Sequencer-programmed arpeggiator

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

#### `JUMP` direction
  - Uses a combination of relative and absolute movement through the chord
  - Both colors advance the active position in the arp chord by octave-many places, wrapping around to the beginning of the chord
  - White steps emit a note from the active position in the arp chord, e.g.:
    - When the sequencer pitch is the 5th white note of octave 2, and the active position is 1 out of the arp chord's 6 notes, the active position is first incremented by 2 to become 3, and then the 3rd note of the arp chord is emitted
  - Black steps ignore the active position, instead treating the pitch ordinal as an absolute position in the arp chord, e.g.:
    - When the sequencer pitch is the 3rd black note of octave 5, the emitted note is the 3rd note of the arp chord, while the active position is incremented by 5

#### `GRID` direction
  - Simulates an X-Y coordinate system
  - The arp chord is mapped onto the grid in linear fashion, repeated as necessary to fill the grid
  - Octave sets the size of the grid: 4th octave => 4x4 grid (minimum 1x1)
  - White keys advance by 1 along the X-axis, moving left-to-right and wrapping back to the left
  - Black keys advance by 1 along the Y-axis, moving top-to-bottom and wrapping back to the top



# Modulation generators

### Envelope

#### Envelope modulation destinations
- [Oscillator gain](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu) (when `OSCILLATOR MODE` is `ENVELOPED`)
- [Oscillator timbre](#oscillator-timbre-settings)
  - `TE (TIMBRE ENV MOD)`: attenuverter for the envelope's modulation of timbre
  - `TV (TIMBRE VEL MOD)` attenuverter for velocity's modulation of the timbre envelope (velocity can polarize the timbre envelope)
- Aux CV output: `ENVELOPE` (itself modulated by [tremolo LFO](#lfo-modulation-destinations))

#### Envelope ADSR settings
- ADSR parameters: attack time, decay time, sustain level, and release time 
- ADSR parameters can be set manually and modulated by note velocity
- `ATTACK INIT`, `DECAY INIT`, `SUSTAIN INIT`, `RELEASE INIT`: initial settings for ADSR stages
- `ATTACK MOD VEL`, `DECAY MOD VEL`, `SUSTAIN MOD VEL`, `RELEASE MOD VEL`: attenuverter for velocity's modulation of the stage
- All curves are exponential
- Stage times range from 0.09 ms (4 ticks) to 5 seconds

#### Envelope peak level
- Peak is the instantaneous point where attack ends and decay begins
- `PV (PEAK VEL MOD)` sets the velocity-sensitivity of the level of the peak
- Zero: peak level is maximum (unity)
- Positive values (clockwise of center): peak level is increasingly damped by low note velocity
- Negative values (counter-clockwise): peak level is increasingly damped by high note velocity

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

### LFO

#### LFO modulation destinations
- Vibrato: oscillator pitch, pitch CV
  - `VB (VIBRATO AMOUNT)` (in `▽S (SETUP MENU)`): attenuator for bipolar vibrato LFO
    - Allows vibrato control via panel UI if your keyboard doesn't have a modulation wheel
  - `VS (VIBRATO SHAPE)` (in `▽S (SETUP MENU)`): shape of the vibrato LFO
- Tremolo: [oscillator amplitude](#oscillator-mode-setting-om-oscillator-mode-in-o-oscillator-menu), [`ENVELOPE` aux CV](#envelope-modulation-destinations)
  - `TR (TREMOLO DEPTH)` (in `▽A (AMPLITUDE MENU)`): attenuator for the unipolar tremolo LFO's reduction of gain
  - `TS (TREMOLO SHAPE)` (in `▽A (AMPLITUDE MENU)`): shape of the tremolo LFO
- Timbre: [oscillator timbre](#oscillator-timbre-settings)
  - `TL (TIMBRE LFO MOD)` (in `▽O (OSCILLATOR MENU)`): attenuator for the bipolar timbre LFO
  - `LS (TIMBRE LFO SHAPE)` (in `▽O (OSCILLATOR MENU)`): shape of the timbre LFO
- Aux CV outputs: `LFO`, `VIBRATO LFO` (unattenuated and attenuated versions of vibrato LFO)
- Shape options: triangle, down saw, up saw, square

#### LFO speed and sync: `LF (LFO RATE)`
- Sets the base LFO rate for a part
- Increases clock sync ratio when turning counter-clockwise from center
- Increases frequency when turning clockwise
- F.k.a. `VIBRATO SPEED` in original firmware

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



# Audio oscillator

### Oscillator mode setting: `OM (OSCILLATOR MODE)` in `▽O (OSCILLATOR MENU)`
- `OFF`: no oscillator output
- `DRONE`: oscillator gain is modulated by tremolo LFO, but not by envelope
- `ENVELOPED`: oscillator gain is modulated by both [tremolo LFO](#lfo-modulation-destinations) and [envelope](#envelope-modulation-destinations)

### Oscillator timbre settings
- Each oscillator shape has a timbre parameter
- `TI (TIMBRE INITIAL)`: manually sets initial timbre
- [Modulation by envelope](#envelope-modulation-destinations)
- [Modulation by LFO](#lfo-modulation-destinations)

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
