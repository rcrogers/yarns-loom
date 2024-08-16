# Loom
An alternative firmware for the [Yarns MIDI interface by Mutable Instruments](https://mutable-instruments.net/modules/yarns/), with an emphasis on dynamic voicing, flexible composition, and expressive control.

### Features
- [Looper-style sequencer with real-time note recording](yarns/MANUAL.md#loop-sequencer-mode-with-real-time-recording)
- [Arpeggiator movement can be programmed via sequencer notes](yarns/MANUAL.md#sequencer-driven-arpeggiator)
- [Velocity-controlled ADSR envelopes](yarns/MANUAL.md#amplitude-dynamics-envelope-and-tremolo)
- [New oscillator wave shapes, with amplitude and timbre modulated by LFO and envelope](yarns/MANUAL.md#oscillator-controls)
- [Enhanced support for MIDI Control Change events](yarns/MANUAL.md#expanded-support-for-control-change-events)
- [New layouts, including a layout with a 3-voice paraphonic part](yarns/MANUAL.md#layouts)
- [More options for polyphonic mapping of notes to voices](yarns/MANUAL.md#polyphonic-voice-allocation-note-priority-and-voicing)
- [The hold pedal can be used for sostenuto, latching, and more](yarns/MANUAL.md#hold-pedal)
- [Input octave switch](yarns/MANUAL.md#event-routing-filtering-and-transformation)
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
- [Discussion thread on ModWiggler](https://www.modwiggler.com/forum/viewtopic.php?t=255378)
- Forks, pull requests, feature ideas, and bug reports are welcome (subject to my availability)
- License: MIT License

### Acknowledgements
- Thanks to forum user `bloc` for beta testing, bug reports, support, and many great ideas
- Thanks to forum user `Airell` for the idea of per-part latching
- Thanks to forum user `sdejesus13` for encouraging the exploration of clock-based recording
- And above all, thanks to Émilie Gillet for making a great open-source module!
