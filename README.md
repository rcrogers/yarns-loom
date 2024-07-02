# Loom
An alternative firmware for the [Yarns MIDI interface by Mutable Instruments](https://mutable-instruments.net/modules/yarns/), with an emphasis on dynamic voicing, flexible composition, and expressive control.

### Features
- [Looper-style sequencer](yarns/MANUAL.md#looper-style-sequencing-mode-with-real-time-recording)
- [Arpeggiator movement can be programmed via sequencer](yarns/MANUAL.md#sequencer-driven-arpeggiator)
- [Braids-inspired oscillator waveforms](yarns/MANUAL.md#oscillator-controls)
- [ADSR envelopes with velocity control](yarns/MANUAL.md#amplitude-dynamics-envelope-and-tremolo)
- [Deeper support for MIDI CCs](yarns/MANUAL.md#expanded-support-for-control-change-events)
- [Tremolo](yarns/MANUAL.md#amplitude-dynamics-envelope-and-tremolo)
- [New layouts, including a layout with a 3-voice paraphonic part](yarns/MANUAL.md#layouts)
- [More options for polyphonic voicing](yarns/MANUAL.md#polyphonic-voice-allocation-note-priority-and-voicing)
- [New ways to use the hold pedal](yarns/MANUAL.md#hold-pedal)
- [Input octave switch](yarns/MANUAL.md#event-routing-filtering-and-transformation)
- **[→ Check the manual for more!](yarns/MANUAL.md)**

### Caveats
- Installation of this firmware is at your own risk
- Presets saved in this firmware will not load with the manufacturer's firmware, and vice versa.  Users are advised to run `INIT` from the main menu after switching firmware
- Some changes are not documented
- Some of Yarns' stock capabilities have been downgraded to accommodate new features (e.g. the sequencer holds 30 notes instead of the original 64)

### Installation
1. Download `yarns.syx` from the [latest release's assets](https://github.com/rcrogers/yarns-loom/releases/latest)
2. [Follow the manufacturer's instructions for installing new firmware](https://pichenettes.github.io/mutable-instruments-documentation/modules/yarns/manual/#firmware)

### Community
- [Discussion thread on the Mutable Instruments forums](https://forum.mutable-instruments.net/t/loom-alternative-firmware-for-yarns-looper-paraphony-and-more/17723)
- Forks, pull requests, feature ideas, and bug reports are welcome (though I can't guarantee a timely response)
- License: MIT License

### Acknowledgements
- Thanks to [forum user `bloc`](https://forum.mutable-instruments.net/t/loom-alternative-firmware-for-yarns-looper-paraphony-and-more/17723/3) for beta testing, bug reports, support, and many great ideas
- Thanks to [forum user `Airell`](https://forum.mutable-instruments.net/t/yarns-firmware-wish-list/8051/39) for the idea of per-part latching
- Thanks to [forum user `sdejesus13`](https://forum.mutable-instruments.net/t/yarns-firmware-wish-list/8051/24) for encouraging the exploration of clock-based recording
- And above all, thanks to Émilie Gillet for making a great open-source module!
