#### Introduction

This manual explains how Loom differs from a stock Yarns.  For documentation about Yarns' native capabilities (which Loom largely retains), [check the manufacturer's manual!](https://mutable-instruments.net/modules/yarns/manual/)

#### Table of contents
- [Panel controls and display](#panel-controls-and-display)
    - [Global control and display of the active part](#global-control-and-display-of-the-active-part)
    - [Tap tempo changes](#tap-tempo-changes)
    - [Other changes](#other-changes)
- [Synth voice](#synth-voice)
    - [Oscillator controls](#oscillator-controls)
    - [Oscillator synthesis models](#oscillator-synthesis-models)
    - [Amplitude dynamics: envelope and tremolo](#amplitude-dynamics-envelope-and-tremolo)
- [Sequencer](#sequencer)
    - [General recording controls](#general-recording-controls)
    - [Play mode](#play-mode)
    - [Step sequencer](#step-sequencer)
      - [Swing](#swing)
      - [Slide](#slide)
      - [Improved step selection](#improved-step-selection)
      - [Other changes](#other-changes-1)
    - [Loop sequencer mode with real-time recording](#loop-sequencer-mode-with-real-time-recording)
    - [Arpeggiator](#arpeggiator)
      - [Interaction between `ARP DIRECTION` and `NOTE PRIORITY`](#interaction-between-arp-direction-and-note-priority)
      - [Sequencer-driven arpeggiator](#sequencer-driven-arpeggiator)
        - [Using `ARP PATTERN` to enable the sequencer-driven arpeggiator](#using-arp-pattern-to-enable-the-sequencer-driven-arpeggiator)
        - [How it works](#how-it-works)
      - [Programmable `ARP DIRECTIONS`](#programmable-arp-directions)
        - [General concepts](#general-concepts)
        - [`JUMP` direction](#jump-direction)
        - [`GRID` direction](#grid-direction)
- [MIDI](#midi)
    - [Layouts](#layouts)
    - [Event routing, filtering, and transformation](#event-routing-filtering-and-transformation)
    - [MIDI Control Change messages](#midi-control-change-messages)
      - [Control change mode](#control-change-mode)
      - [Added CCs](#added-ccs)
      - [Macro CCs that control combinations of settings](#macro-ccs-that-control-combinations-of-settings)
      - [Other CC improvements](#other-cc-improvements)
    - [Clocking](#clocking)
      - [Clock ratios](#clock-ratios)
      - [Clock offset](#clock-offset)
      - [Predictable phase relationships between clocked events](#predictable-phase-relationships-between-clocked-events)
      - [Improved interaction between keyboard and clock start/stop](#improved-interaction-between-keyboard-and-clock-startstop)
    - [Song position](#song-position)
      - [What the song position controls](#what-the-song-position-controls)
      - [Using the song position](#using-the-song-position)
    - [Hold pedal](#hold-pedal)
      - [Display of pressed/sustained keys](#display-of-pressedsustained-keys)
      - [Hold pedal mode per part](#hold-pedal-mode-per-part)
      - [Pedal polarity](#pedal-polarity)
    - [Polyphonic voice allocation (`NOTE PRIORITY` and `VOICING`)](#polyphonic-voice-allocation-note-priority-and-voicing)
      - [Note priority](#note-priority)
      - [Polyphonic `VOICING` options](#polyphonic-voicing-options)
      - [Other changes](#other-changes-2)
    - [Legato and portamento](#legato-and-portamento)
    - [LFOs](#lfos)
      - [Frequency](#frequency)
      - [Vibrato shape](#vibrato-shape)
      - [Spread: detune or dephase a part's LFOs](#spread-detune-or-dephase-a-parts-lfos)

# Panel controls and display

### Global control and display of the active part
- Display periodically flashes the active part and its `PLAY MODE`
- Hold Tap/Rest to switch the active part (the new active part will flash briefly on the screen)
- The active part is used for:
  - Selecting the recording part when pressing `REC`
  - Front-panel latching (hold `REC`)
  - Editing part-specific settings

### Tap tempo changes
- If a single tap is received without follow-up, the tempo is set to use external clocking
- After setting a tempo, the result flashes on the screen

### Other changes
- Moved configuration-type settings into a submenu, accessed by opening `▽S (SETUP MENU)`
- Print flat notes as lowercase character (instead of denoting flatness with `b`) so that octave can always be displayed
- Improved clock-sync of display fade for the `TE(MPO)` setting
- Splash on save/load

# Synth voice

### Oscillator controls
- Configured via the `▽O (OSCILLATOR MENU)`
- `OM (OSCILLATOR MODE)` switches the oscillator between `OFF`, `DRONE`, and `ENVELOPED`
- `OS (OSCILLATOR SHAPE)` sets the waveform (see below)
- Each wave shape has a timbral parameter that can be modulated by several sources
  - `TI (TIMBRE INITIAL)` sets initial timbre
  - `TL (TIMBRE LFO MOD)`: attenuator for the bipolar LFO's modulation of timbre
  - `LS (TIMBRE LFO SHAPE)` sets the shape of the timbre LFO (triangle, down saw, up saw, square)
  - `TE (TIMBRE ENV MOD)`: attenuverter for the envelope's modulation of timbre
  - `TV (TIMBRE VEL MOD)` attenuverter for velocity's modulation of the timbre envelope (velocity can polarize the timbre envelope)

### Oscillator synthesis models
- Filtered noise: `TIMBRE` sets filter cutoff, voice pitch sets filter resonance
  - Low-pass, notch, band-pass, high-pass
- Phase distortion, resonant pulse: `TIMBRE` sets filter cutoff
  - Low-pass, peaking, band-pass, high-pass
- Phase distortion, resonant saw: `TIMBRE` sets filter cutoff
  - Low-pass, peaking, band-pass, high-pass
- State-variable filter, low-pass: `TIMBRE` sets filter cutoff (resonance is fixed)
  - Pulse, saw
- Pulse-width modulation: `TIMBRE` sets pulse width
  - Pulse, saw
- Saw-pulse morph: `TIMBRE` morphs toward pulse
- Hard sync: `TIMBRE` sets detuning of the secondary oscillator
  - Sine, pulse, saw
- Wavefolder: `TIMBRE` sets fold gain
  - Sine, triangle
- Dirac comb: `TIMBRE` sets harmonic content
- Compressed sine (`tanh`): `TIMBRE` sets compression amount
- Exponential sine: `TIMBRE` sets exponentiation amount
- Frequency modulation: `TIMBRE` sets modulation index
  - 11 integer ratios (ordered from harmonic to inharmonic)
  - 8 ratios based on the inverse Minkowski question-mark function
  - 7 ratios that are integer divisions/multiples of pi

### Amplitude dynamics: envelope and tremolo
- Configured via the `▽A (AMPLITUDE MENU)`
- Tremolo can be applied to envelope and oscillator
  - Tremolo uses the same LFO frequency as vibrato
  - `TR (TREMOLO DEPTH)`: attenuator for the unipolar tremolo LFO's reduction of gain
  - `TS (TREMOLO SHAPE)`: the shape of the tremolo LFO (triangle, down saw, up saw, square)
- ADSR envelope with velocity modulation
  - Envelope controls voice amplitude when the `OSCILLATOR MODE` is `ENVELOPED`
  - Envelope is available as an aux CV output (`ENVELOPE`) in all layouts
  - `ATTACK INIT`, `DECAY INIT`, `SUSTAIN INIT`, `RELEASE INIT`: initial settings for ADSR segments
    - Segment times range from 1 ms (2 ticks) to 5 seconds
  - `ATTACK MOD VEL`, `DECAY MOD VEL`, `SUSTAIN MOD VEL`, `RELEASE MOD VEL`: attenuverter for velocity's modulation of the segment
  - The envelope's segments and their sensitivity to velocity are set by `ATTACK TIME INIT`, `ATTACK TIME MOD`, etc.
  - Peak attack amplitude can be velocity-scaled via `PV (PEAK VEL MOD)`
    - Positive values: peak attack amplitude is lower when velocity is lower
    - Negative values: peak attack amplitude is lower when velocity is higher
    - Zero: peak attack amplitude is always maximum
  
# Sequencer

### General recording controls
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

#### Swing
- `SWING` works with all clock ratios, not just ratio 4:1 (f.k.a. sixteenth notes in stock firmware)
- Swing can be applied to either even or odd steps
  - When `SWING` is counter-clockwise, odd steps (1, 3, 5...) are swung by the selected amount
  - When `SWING` is clockwise, even steps (2, 4, 6...) are swung (stock Yarns behavior)

#### Slide
- While in recording mode, hold `START` to toggle slide on the selected step
- If the selected step has slide, the display will show a fade effect
- When a `REST` or `TIE` is recorded, slide is removed from that step. If a real note is later overdubbed into this step, slide must be re-added manually

#### Improved step selection
- Display brightens while the selected step is being played
- When using encoder to scroll through steps, wraps around if the end is reached

#### Other changes
- Replaced the `EUCLIDEAN ROTATE` setting with a more general `STEP OFFSET` -- allows starting the step sequencer on any step
- Euclidean rhythms can be applied to the step sequencer as well as the arpeggiator
- Capacity reduced from 64 to 30 notes, to free up space in the preset storage

### Loop sequencer mode with real-time recording
- To enable, ensure `SM (SEQ MODE)` is set to `LOOP`
- To use, press `REC` to enter real-time recording mode
  - Play notes to record them into the loop
  - Press `START` to delete the oldest note, or `TAP` for the newest
  - Display brightness shows the progression of the loop, or the note being played
  - Channel LEDs show the quarter-phase of the loop
  - Scroll the encoder to shift the loop phase by 1/128: clockwise shifts notes earlier, counter-clockwise shifts notes later
- Loop length is set by the `L- (LOOP LENGTH)` in quarter notes, combined with the part's clock settings
- Note start/end times are recorded at 13-bit resolution (1/8192 of the loop length)
- Holds 30 notes max -- past this limit, overwrites oldest note

### Arpeggiator

#### Interaction between `ARP DIRECTION` and `NOTE PRIORITY`
- `NOTE PRIORITY` sets the basic ordering/sorting of the arp chord (the keys held on the keyboard)
- `ARP DIRECTION` sets the algorithm used to traverse the ordered arp chord
- By combining these, traditional arp behaviors can be achieved:
  - With `NOTE PRIORITY` = `LOW`: set `ARP DIRECTION` to `LINEAR` for "up," or `BOUNCE` for "up-down"
  - With `NOTE PRIORITY` = `FIRST`: set `ARP DIRECTION` to `LINEAR` for "played order"

#### Sequencer-driven arpeggiator

##### Using `ARP PATTERN` to enable the sequencer-driven arpeggiator
- Clockwise = pattern-based arpeggiator rhythms (stock Yarns)
- Counter-clockwise = sequencer-driven arpeggiator
  - `S1`-`S8` will reset the arpeggiator state after every 1-8 plays of the entire sequence (step or loop)
    - Reset helps generate more predictable arp patterns
  - `S0` never resets the arpeggiator state

##### How it works
- Like the normal arpeggiator, the arp chord is controlled by holding keys on the keyboard
- Unlike the normal arpeggiator, the sequencer-driven arpeggiator advances when the loop/step sequencer encounters a new note, instead of advancing on every clock pulse
- A sequence (either loop or step) must exist to produce arpeggiator output
- The arpeggiator respects rests/ties in a step sequence
- The velocity of the arpeggiator output is calculated by multiplying the velocities of the sequencer note and the held key

#### Programmable `ARP DIRECTIONS` 

##### General concepts
  - The arpeggiator always has some **active position**, e.g. "the 3rd key of the chord"
  - Changes in the active position ("**movement**") are determined by the pitch of notes emitted by the sequencer 
    - If `ARP PATTERN` is not sequencer-driven, the sequencer pitch data is replaced by the position in the 16-step pattern rhythm 
  - Sequencer pitch is interpreted based on its:
    - Key **color** (is the key black or white?)
    - Displayed **octave number** (with C as the first note of the octave)
    - **Pitch ordinal** within octave and color, e.g.
      - When the sequencer pitch is the 2nd white note of octave 5, the pitch ordinal is 2
      - When the sequencer pitch is the 4th black note of octave 2, the pitch ordinal is 4
  - The octave and color are used for different purposes by each direction
  - The pitch ordinal sets the minimum arp chord size for which the arpeggiator will emit a note
    - For a sequencer pitch with pitch ordinal N, the arpeggiator emits a note only if there are N or more keys in the arp chord, e.g.:
      - When the sequencer pitch is the 3rd white key of its octave, a note is emitted only if there are 3+ keys in the arp chord
      - When the sequencer pitch is the 1st black key of its octave, a note is emitted only if there are 1+ keys in the arp chord
    - Allows dynamic control of the arpeggiator's rhythmic pattern by varying the size of the arp chord

##### `JUMP` direction
  - Uses a combination of relative and absolute movement through the chord
  - Both colors advance the active position in the arp chord by octave-many places, wrapping around to the beginning of the chord
  - White steps emit a note from the active position in the arp chord, e.g.:
    - When the sequencer pitch is the 5th white note of octave 2, and the active position is 1 out of the arp chord's 6 notes, the active position is first incremented by 2 to become 3, and then the 3rd note of the arp chord is emitted
  - Black steps ignore the active position, instead treating the pitch ordinal as an absolute position in the arp chord, e.g.:
    - When the sequencer pitch is the 3rd black note of octave 5, the emitted note is the 3rd note of the arp chord, while the active position is incremented by 5

##### `GRID` direction
  - Simulates an X-Y coordinate system
  - The arp chord is mapped onto the grid in linear fashion, repeated as necessary to fill the grid
  - Octave sets the size of the grid: 4th octave => 4x4 grid (minimum 1x1)
  - White keys advance by 1 along the X-axis, moving left-to-right and wrapping back to the left
  - Black keys advance by 1 along the Y-axis, moving top-to-bottom and wrapping back to the top
  

# MIDI

### Layouts
- `2+2` 3-part layout: 2-voice polyphonic part + two monophonic parts
- `2+1` 2-part layout: 2-voice polyphonic part + monophonic part with aux CV
- `*2` 3-part layout: 3-voice paraphonic part + monophonic part with aux CV + monophonic part without aux CV
  - Paraphonic part can use the new [envelopes](#amplitude-dynamics-envelope-and-tremolo)
  - Audio mode is always on for the paraphonic part
  - Output channels:
    1. CV: Part 1's 3 voices mixed to 1 audio output, Gate: Part 4's gate
    2. Part 2, monophonic CV/gate
    3. Part 2, modulation configurable via `3>`
    4. Part 3, monophonic CV/gate
- `3M` 3-part layout: 3 monophonic parts, plus clock on gate 4 and bar/reset on CV 4
- `*1` 2-part layout: 3-voice paraphonic part + monophonic part with aux CV
  - Output channels:
    1. CV: Part 1's 3 voices mixed to 1 audio output, Gate: Part 1's gate
    2. Part 2, monophonic CV/gate
    3. Part 1's aux CV, configurable via `CV`
    4. Part 2's aux CV, configurable via `CV`
    



### Event routing, filtering, and transformation
- New `SI (SEQ INPUT RESPONSE)` setting changes how a playing sequence responds to manual input
  - `OFF` ignores keyboard input
  - `TRANSPOSE` is the stock firmware behavior
  - `REPLACE` retains the sequence's rhythm, but overrides its pitch
  - `DIRECT` gives full use of the keyboard, allowing accompaniment of a sequence, etc.
- New `IT (INPUT TRANSPOSE OCTAVES)` setting to apply transposition to notes received by a part
  - Effectively an octave switch for the controller
- Parts can filter notes by velocity
  - Added UI for previously hidden settings `V> (VELOCITY MIN)` and `V< (VELOCITY MAX)`
  - Present for all layouts except 4V
  - Output velocity range is scaled to compensate for the restricted range imposed by input filtering
- Recording 
  - Any MIDI events ignored by the recording part can be received by other parts
  - Recording part now responds to MIDI start

### MIDI Control Change messages

#### Control change mode
- New global setting for `CC (CONTROL CHANGE MODE)`
- `OFF`: CCs are ignored
- `ABSOLUTE`: the setting is updated to the CC value (for use with traditional potentiometers)
- `RELATIVE DIRECT`: the setting is directly incremented by the CC value
    - For use with endless encoders
    - Uses the "twos complement" standard for translating MIDI values into a relative change
      - MIDI value 1 => setting + 1 (increment)
      - MIDI value 127 => setting - 1 (decrement)
    - Settings will increase or decrease by one value for each click of the encoder
    - Depending on how many values the setting has, the encoder may take anywhere from 1 to 127 clicks to scan the range of setting values
- `RELATIVE SCALED`: the setting is incremented by 0-100% of the CC value
    - Similar to `RELATIVE DIRECT`, but always takes 127 encoder clicks to scan the range of setting values, no matter how many setting values there are
    - Effectively gives all controllers the same travel distance from minimum to maximum

#### Added CCs
- Recording control: start/stop recording mode, delete a recording
- CC support for all new settings

#### Macro CCs that control combinations of settings
- Recording state and erase: off, on, triggered erase, immediate erase
- Sequencer mode and play mode: step sequencer, step arpeggiator, manual, loop arpeggiator, loop sequencer

#### Other CC improvements
- The result of a received CC is briefly displayed (value, setting abbreviation, and receiving part)
- Fixed settings to accept a negative value via CC
- [Implementation Chart](https://docs.google.com/spreadsheets/d/1V6CRqf_3FGTrNIjcU1ixBtzRRwqjIa1PaiqOFgf6olE/edit#gid=0)

### Clocking

#### Clock ratios
- Clock divisions/multiplications are expressed as a ratio of the master clock
- `CLOCK RATIO` sets the frequency of the part's clock relative to the master clock
- `OUTPUT CLOCK RATIO` sets the frequency of the clock output gate relative to the master clock
- Available ratios: 1/8, 1/7, 1/6, 1/5, 2/9, 1/4, 2/7, 1/3, 3/8, 2/5, 3/7, 4/9, 1/2, 4/7, 3/5, 2/3, 3/4, 4/5, 6/7, 8/9, 1/1, 8/7, 6/5, 4/3, 3/2, 8/5, 2/1, 8/3, 3/1, 4/1, 6/1, 8/1
- These ratios are also used for `LFO RATE` when the LFO is clock-synced

#### Clock offset
- Setting `C+ CLOCK OFFSET` allows fine-tuning the master clock phase by ±63 ticks
- Applied in real time if the clock is running
- Designed to aid in multitrack recording
- Arithmetically, offset is applied **after** `INPUT CLK DIV`
- If offset is negative, the sequencer will not play any notes until tick 0 is reached

#### Predictable phase relationships between clocked events
- Sequencers' phases are always calculated from the master clock state
- Allows sequencers to return to predictable phase relationships even after a stint in disparate time signatures

#### Improved interaction between keyboard and clock start/stop
- An explicit clock start (from panel switch or MIDI) can supersede an implicit clock start (from keyboard)
- Stopping the clock no longer stops manually held keys, though it still stops notes generated by the sequencer/arpeggiator

### Song position

#### What the song position controls
- All clock-based events are synchronized to the song position
- Step sequencer will begin on the target step
- The loop sequencer and synced LFOs will advance to the correct phase of their loops
- Arpeggiator state is updated (based on any currently held arp chord) to the same state as if the song had played from the beginning

#### Using the song position
- The song position is saved when the clock stops
  - Display flashes `||` (pause)
- Cueing: the song position can be updated by a MIDI Song Position Pointer message
  - Display flashes `<<` (rewind), `>>` (fast-forward), or `[]` (reset/stop) depending on the new song position
  - Note: this was tested with a Tascam Model 12 as a MIDI source, which implements cueing by sending MIDI Song Position Pointer followed by a MIDI Continue.  If this doesn't work with your MIDI source, let me know!
- Reset song position: long press Start/Stop button, or start via MIDI Start
- Play from song position: short press Start/Stop button, or MIDI Continue

### Hold pedal

#### Display of pressed/sustained keys
- Screen periodically shows tick marks to show the state of the active part's 6 most recently pressed keys, and how the hold pedal is affecting them
- Bottom-half tick: key is manually held, and will stop when released
- Full-height tick: key is manually held, and will be sustained when released
- Steady top-half tick: key is sustained, and will continue after the next key-press
- Blinking top-half tick: key is sustained, and will be stopped by the next key-press

#### Hold pedal mode per part
  - New part setting `HM (HOLD PEDAL MODE` sets each part's response to the hold pedal
  - `OFF`: pedal has no effect
  - `SUSTAIN`: sustains key-releases while pedal is down, and stops sustained notes on pedal-up
    - Matches the behavior of the pedal in the stock firmware
  - `SOSTENUTO`: while pedal is down, sustains key-releases only on keys that were pressed before pedal-down; stops sustained notes on pedal-up
  - `LATCH`: uses the semantics of the button-controlled latching in stock Yarns -- sustains key-releases while pedal is down; stops sustained notes on key-press regardless of pedal state
    - Matches the behavior of the front-panel latching (triggered by holding `REC`)
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

### Polyphonic voice allocation (`NOTE PRIORITY` and `VOICING`)

#### Note priority
- Added new `FIRST` (oldest) setting to `NOTE PRIORITY`
- Voicing respects note priority wherever applicable

#### Polyphonic `VOICING` options
- `sM STEAL LOWEST PRIORITY RELEASE MUTE` (previously `POLY`)
    - Steal from the lowest-priority existing note IFF the incoming note has higher priority
    - Does not reassign voices to unvoiced notes on release
- `PRIORITY ORDER` (previously `SORTED`)
    - Voice 1 always receives the note that has priority 1, voice 2 the note with priority 2, etc.
- `UNISON RELEASE REASSIGN` (previously `U1`)
- `UNISON RELEASE MUTE` (previously `U2`)
- `SM STEAL HIGHEST PRIORITY RELEASE MUTE` (previously `STEAL MOST RECENT`)
    - Steal from the highest-priority existing note IFF the incoming note has higher priority
    - Does not reassign on release
- `sR STEAL LOWEST PRIORITY RELEASE REASSIGN`
    - Steal from the lowest-priority existing note IFF the incoming note has higher priority
    - Reassigns voices to unvoiced notes on release
- `SR STEAL HIGHEST PRIORITY RELEASE REASSIGN`
    - Steal from the highest-priority existing note IFF the incoming note has higher priority
    - Reassigns on release

#### Other changes
- Notes that steal a voice are considered legato
- Fixed unison to allocate notes without gaps
- Improve unison etc. to avoid unnecessary reassignment/retrigger of voices during a partial chord change
- Unison etc. reassign voices on `NoteOff` if there are held notes that don't yet have a voice
- Allow monophonic parts to use all voicing modes

### Legato and portamento
- Replaced `LEGATO MODE` setting (three values) with two on/off settings, `LEGATO RETRIGGER` (are notes retriggered when played legato?) and `PORTAMENTO LEGATO ONLY` (is portamento applied on all notes, or only on notes played legato?)
  - Enables a new behavior: notes played legato are retriggered + portamento is applied only on notes played legato
- `PORTAMENTO` setting has a shared zero at center
  - Increases constant-time portamento when turning counter-clockwise of center, and increases constant-rate when turning clockwise
- Broadened setting range from 51 to 64 values per curve shape

### LFOs

#### Frequency
  - `LFO RATE` (formerly `VIBRATO SPEED`) has a shared zero at center
  - Increases clock sync ratio when turning counter-clockwise of center
  - Increases frequency when turning clockwise

#### Vibrato shape
- `VS (VIBRATO SHAPE)` (in `▽S (SETUP MENU)`) sets the shape of the vibrato LFO (triangle, down saw, up saw, square)

#### Spread: detune or dephase a part's LFOs
- `LV (LFO SPREAD VOICES)`: dephase or detune LFOs between the part's voices
    - Only available in polyphonic/paraphonic layouts
- `LT (LFO SPREAD TYPES)`: for each voice in the part, dephase or detune between the vibrato, timbre, and tremolo LFOs
- Turning these settings counter-clockwise from center dephases the LFOs
    - Each LFO's phase is progressively more offset, by an amount ranging from 0° to 360° depending on the setting
    - Ideal for quadrature and three-phase modulation
    - When dephasing, the LFOs always share a common frequency
- Turning clockwise from center detunes the LFOs
    - Each LFO's frequency is a multiple of the last, with that multiple being between 1x and 2x depending on the setting
    - Facilitates unstable, meandering modulation
