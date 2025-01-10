# Loom
An alternative firmware for the [Yarns MIDI interface by Mutable Instruments](https://mutable-instruments.net/modules/yarns/), with an emphasis on dynamic voicing, flexible composition, and expressive control.

### Features
- [Cue playback with MIDI Song Position](yarns/MANUAL.md#song-position)
- [Looper-style sequencer](yarns/MANUAL.md#loop-sequencer-mode-with-real-time-recording)
- [Arpeggiator movement can be programmed via sequencer](yarns/MANUAL.md#sequencer-driven-arpeggiator)
- [Braids-inspired oscillator waveforms with timbre shaping](yarns/MANUAL.md#oscillator-controls)
- [ADSR envelopes with velocity control](yarns/MANUAL.md#amplitude-dynamics-envelope-and-tremolo)
- [Deeper support for MIDI CCs](yarns/MANUAL.md#midi-control-change-messages)
- [New layouts, including a layout with a 3-voice paraphonic part](yarns/MANUAL.md#layouts)
- [More options for polyphonic voicing](yarns/MANUAL.md#polyphonic-voice-allocation-note-priority-and-voicing)
- [New ways to use the hold pedal](yarns/MANUAL.md#hold-pedal)
  - **[Check the manual for more! →](yarns/MANUAL.md)**

### Caveats
- Installation of this firmware is at your own risk
- Presets saved in this firmware will not load with the manufacturer's firmware, and vice versa.  Users are advised to run `INIT` from the main menu after switching firmware
- Some changes are not documented
- Some of Yarns' stock capabilities have been downgraded to accommodate new features (e.g. the sequencer holds 30 notes instead of the original 64)

### Installation
1. Download `yarns.syx` from the [latest release's assets](https://github.com/rcrogers/yarns-loom/releases/latest)
2. [Follow the manufacturer's instructions for installing new firmware](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/#firmware)

### Community
- [Discussion thread on ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=255378&sid=a5dd52cfbd6f9d763a0e8cf741cf1742)
- Forks, pull requests, feature ideas, and bug reports are welcome (though I can't guarantee a timely response)
- License: MIT License

### Acknowledgements
- Thanks to Dylan Bolink for beta testing, bug reports, support, and many great ideas
- Thanks to Mutable forum user Airell for the idea of per-part latching
- Thanks to Mutable forum user sdejesus13 for encouraging the exploration of clock-based recording
- And above all, thanks to Émilie Gillet for making a great open-source module!
