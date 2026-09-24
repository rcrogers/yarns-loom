// A dump, as a 16-bit mono WAV. The ear is the arbiter for every voicing
// question in the oscillator plans and nothing here could produce a file to
// listen to -- so a claim about what a change SOUNDS like had to wait for a
// flash.
//
//   ./osctest dump shape=4 hold=1 warp=1 timbre=0 sweep=4 pitch_raw=12288 \
//     vibrato=4 blocks=2000 | head -n 128000 | node wav.js rate=45000 > a.wav
//
// `head` is not optional for an A/B: `dump` renders the gain profile twice, and
// the second render fades to silence. One profile is blocks * 64 samples.
function opt(k, d) { const a = process.argv.find(s => s.startsWith(k + '=')); return a ? Number(a.split('=')[1]) : d; }
const x = require('fs').readFileSync(0, 'utf8').trim().split('\n').map(Number);
const rate = opt('rate', 45000);
const data = Buffer.alloc(x.length * 2);
for (let i = 0; i < x.length; i++) data.writeInt16LE(Math.max(-32768, Math.min(32767, x[i] | 0)), i * 2);
const header = Buffer.alloc(44);
header.write('RIFF', 0);
header.writeUInt32LE(36 + data.length, 4);
header.write('WAVEfmt ', 8);
header.writeUInt32LE(16, 16);
header.writeUInt16LE(1, 20);          // PCM
header.writeUInt16LE(1, 22);          // mono
header.writeUInt32LE(rate, 24);
header.writeUInt32LE(rate * 2, 28);   // byte rate
header.writeUInt16LE(2, 32);          // block align
header.writeUInt16LE(16, 34);
header.write('data', 36);
header.writeUInt32LE(data.length, 40);
process.stdout.write(Buffer.concat([header, data]));
