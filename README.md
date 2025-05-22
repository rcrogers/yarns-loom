# Loom
An alternate firmware for the [Yarns MIDI interface by Mutable Instruments](https://mutable-instruments.net/modules/yarns/), designed to make Yarns more powerful and user-friendly.

### Features
- [Cue sequencer with MIDI Song Position](yarns/MANUAL.md#set-song-position-for-master-clock)
- [Looper-style sequencer with real-time polyphonic recording](yarns/MANUAL.md#loop-sequencer)
- [Sequencer-programmed arpeggiator movement](yarns/MANUAL.md#sequencer-programmed-arpeggiator)
- [26 oscillator shapes with timbre/amplitude mod matrix](yarns/MANUAL.md#oscillator-timbre-settings)
- [ADSR envelopes with velocity control](yarns/MANUAL.md#envelope)
- [Expanded support for MIDI CCs](yarns/MANUAL.md#midi-control-change-cc)
- [New paraphonic layouts with 4-voice paraphonic part and 6 overall voices](yarns/MANUAL.md#layouts)
- [New polyphonic voicing algorithms](yarns/MANUAL.md#polyphonic-voice-allocation)
- [Hold pedal can be used for latching, sostenuto, and more](yarns/MANUAL.md#hold-pedal)
  - **[Check the manual for more! →](yarns/MANUAL.md)**

### Caveats
- Installation of this firmware is at your own risk
- Presets saved in this firmware will not load with the manufacturer's firmware, and vice versa.  Users are advised to run `INIT` from the main menu after switching firmware
- Some of Yarns' original features have been downgraded to make room for new features (e.g. the sequencer holds 30 notes instead of the original 64)

### Installing
1. Download `yarns.syx` from the [latest release's assets](https://github.com/rcrogers/yarns-loom/releases/latest)
2. [Follow the manufacturer's instructions for installing new firmware](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/#firmware)

### Community
- [Discussion thread on ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=255378)
- Pull requests, feature ideas, and bug reports are welcome (responses subject to my availability)
- License: MIT License

### Kudos
- Thanks to Dylan Bolink for beta testing, bug reports, support, and countless great ideas
- Thanks to Mutable forum user Airell for the idea of per-part latching
- Thanks to Mutable forum user sdejesus13 for encouraging the exploration of clock-based recording
- And above all, thanks to Émilie Gillet for making incredible open-source modules!
