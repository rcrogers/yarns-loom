# Loom
An alternate firmware for the [Yarns synthesizer module&nbsp;↗](https://mutable-instruments.net/modules/yarns/), aimed at making Yarns more powerful and user-friendly.

### Features
- Cue sequencer with MIDI Song Position&nbsp;[→](yarns/MANUAL.md#master-clock-controls)
- Looper-style sequencer with real-time polyphonic recording&nbsp;[→](yarns/MANUAL.md#loop-sequencer)
- Sequencer-programmed arpeggiator movement&nbsp;[→](yarns/MANUAL.md#sequencer-programmed-arpeggiator)
- 26 oscillator shapes with timbre/amplitude mod matrix&nbsp;[→](yarns/MANUAL.md#oscillator-timbre)
- ADSR envelopes with velocity shaping&nbsp;[→](yarns/MANUAL.md#envelope)
- Expanded support for MIDI CCs&nbsp;[→](yarns/MANUAL.md#control-change-messages)
- New paraphonic layouts with 4-voice paraphonic part and 6 overall voices&nbsp;[→](yarns/MANUAL.md#new-layouts)
- New polyphonic voicing algorithms&nbsp;[→](yarns/MANUAL.md#polyphonic-voice-allocation)
- Hold pedal can be used for latching, sostenuto, and more&nbsp;[→](yarns/MANUAL.md#hold-pedal)
  - **[Check the manual for more!](yarns/MANUAL.md)**

### Caveats
- Installation of this firmware is at your own risk
- Presets saved in this firmware will not load with the manufacturer's firmware, and vice versa
  - Run `INIT` from the main menu after switching firmware
- Some of Yarns' original features have been downgraded to make room for new features
  - E.g. the sequencer holds 30 notes instead of the original 64

### Installation
1. Download `yarns-loom.syx` from the [latest release's assets](https://github.com/rcrogers/yarns-loom/releases/latest)
2. Follow the [instructions for installing Yarns firmware&nbsp;↗](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/#firmware)

### Community
- [Discussion thread on ModWiggler&nbsp;↗](https://www.modwiggler.com/forum/viewtopic.php?t=255378)
- GitHub: feel free to create pull requests, feature ideas, and bug reports
- License: MIT License

### Kudos
- Thanks to Dylan Bolink and Fabio Bernardi for beta testing, bug reports, and great ideas
- Thanks to Mutable forum user Airell for the idea of per-part latching
- Thanks to Mutable forum user sdejesus13 for encouraging the exploration of clock-based recording
- And above all, thanks to Mutable Instruments for making incredible open-source modules!
