# Loom
An alternate firmware for the [Yarns synthesizer module ↗](https://mutable-instruments.net/modules/yarns/), designed to make Yarns more powerful and user-friendly.

### Features
- Cue sequencer with MIDI Song Position [→](yarns/MANUAL.md#controls-for-master-clock)
- Looper-style sequencer with real-time polyphonic recording [→](yarns/MANUAL.md#loop-sequencer)
- Sequencer-programmed arpeggiator movement [→](yarns/MANUAL.md#sequencer-programmed-arpeggiator-movement)
- 26 oscillator shapes with timbre/amplitude mod matrix [→](yarns/MANUAL.md#oscillator-timbre-settings)
- ADSR envelopes with velocity shaping [→](yarns/MANUAL.md#envelope)
- Expanded support for MIDI CCs [→](yarns/MANUAL.md#control-change-cc)
- New paraphonic layouts with 4-voice paraphonic part and 6 overall voices [→](yarns/MANUAL.md#new-layouts)
- New polyphonic voicing algorithms [→](yarns/MANUAL.md#polyphonic-voice-allocation)
- Hold pedal can be used for latching, sostenuto, and more [→](yarns/MANUAL.md#hold-pedal)
  - **[Check the manual for more!](yarns/MANUAL.md)**

### Caveats
- Installation of this firmware is at your own risk
- Presets saved in this firmware will not load with the manufacturer's firmware, and vice versa
  - Run `INIT` from the main menu after switching firmware
- Some of Yarns' original features have been downgraded to make room for new features
  - E.g. the sequencer holds 30 notes instead of the original 64

### Installation
1. Download `yarns-loom.syx` from the [latest release's assets](https://github.com/rcrogers/yarns-loom/releases/latest)
2. Follow the [instructions for installing Yarns firmware ↗](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/#firmware)

### Community
- [Discussion thread on ModWiggler ↗](https://www.modwiggler.com/forum/viewtopic.php?t=255378)
- GitHub: feel free to create pull requests, feature ideas, and bug reports
- License: MIT License

### Kudos
- Thanks to Dylan Bolink for beta testing, bug reports, support, and countless great ideas
- Thanks to Mutable forum user Airell for the idea of per-part latching
- Thanks to Mutable forum user sdejesus13 for encouraging the exploration of clock-based recording
- And above all, thanks to Mutable Instruments for making incredible open-source modules!
