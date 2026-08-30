// WHAT THE PANEL SHOWS, read off the wire.
//
// The display was the last part of the module with nothing off target, and it
// has shipped for it. `1a956a45` found two defects by reading, because there
// was no check to find them with: SetBlinkFrames called itself, so the first
// Print overflowed the stack and the module stuck at boot; and the blink
// frames were about to override the prefix flash, which has its own other side
// already. Both are pinned below.
//
// Nothing here reaches into the driver. tools/uitest/gpio_stub.cc watches the
// four pins display.cc bit-bangs -- data, shift clock, storage latch, and one
// enable per character -- and reconstructs the segment word standing at each
// position, which is what an eye would read.
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DIR = __dirname;
const BIN = path.join(DIR, 'uitest');
if (!fs.existsSync(BIN)) {
  console.error('tools/uitest/uitest is missing. Build it first:\n' +
                '  sh tools/uitest/build.sh');
  process.exit(1);
}
const run = (args) =>
  execSync(`./uitest ${args}`, { cwd: DIR, encoding: 'utf8', maxBuffer: 1e8 });

let failures = 0;
function check(name, ok, detail) {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}  [${detail}]`);
  if (!ok) failures++;
}

// Asked, not transcribed: chr_characters is generated, and a check that copies
// it cannot notice the two disagreeing.
const glyph = (chars) => run(`glyph ${chars}`).trim().split('\n').map(Number);

// `<high> <low>` per position, space separated.
const frames = (args) => {
  const v = run(args).trim().split(/\s+/).map(Number);
  return [[v[0], v[1]], [v[2], v[3]]];
};

// The exciter's two glyphs are the only ones that name a second frame
// (resources/characters.py: \xC6 AMOUNT, \xC7 DURATION). Passed as raw bytes so
// the driver sees the code the firmware would.
const AMOUNT_GLYPH = '$(printf "\\xc6")';
const [A, B] = glyph('AB');

// A glyph that names no other frame is its own other frame, so most of the
// display most of the time does not blink at all.
{
  const [p0, p1] = frames('frames AB');
  check('a plain glyph does not blink',
    p0[0] === A && p0[1] === A && p1[0] === B && p1[1] === B,
    `A ${p0[0]}/${p0[1]}, B ${p1[0]}/${p1[1]}`);
}

// A glyph that names one alternates with it, whole -- the frame is any
// pattern, not a subset of the first.
{
  const [, p1] = frames(`frames "A${AMOUNT_GLYPH}"`);
  check('a glyph that names a second frame alternates with it',
    p1[0] !== p1[1] && p1[0] !== 0 && p1[1] !== 0,
    `${p1[0]} / ${p1[1]}`);
}

// Blinking the whole field is the case where the other frame is blank.
{
  const [p0, p1] = frames('blink AB');
  check('set_blink alternates every position with blank',
    p0[0] === A && p0[1] === 0 && p1[0] === B && p1[1] === 0,
    `A ${p0[0]}/${p0[1]}, B ${p1[0]}/${p1[1]}`);
}

// And turning it off restores what each glyph names, rather than leaving the
// field showing its own segments on both sides.
{
  const [p0, p1] = frames(`unblink "A${AMOUNT_GLYPH}"`);
  check('set_blink(false) restores the glyphs\' own frames',
    p0[0] === A && p0[1] === A && p1[0] !== p1[1],
    `A ${p0[0]}/${p0[1]}, exciter ${p1[0]}/${p1[1]}`);
}

// THE ONE THAT SHIPPED CLOSE. RefreshSlow points displayed_buffer_ away from
// the short buffer for the prefix flash and for a scrolling long name, and
// both already have their own other side; the frame swap applied there too
// would blank it. RefreshFast gates on exactly that -- `displayed_buffer_ ==
// short_buffer_` -- so the check is stated on the same condition from the
// other side: WHEREVER THE SHORT BUFFER IS NOT WHAT IS DISPLAYED, THE TWO
// FRAMES MUST SHOW THE SAME THING.
//
// Ticks are grouped by what SELECTS the buffer -- the blink phase, and the
// scroll's step -- because those two are what RefreshSlow reads. Within a
// group the buffer is fixed, so any difference across the frame flag is the
// swap reaching in. Grouping is what makes this independent of how long a face
// happens to stand: the scroll steps every 260 ticks against a 480-tick frame
// period, so a face can easily sit inside one half, and a check that waited to
// see it in both would be waiting on a coincidence.
function theFrameSwapStaysOnTheShortName(name, args) {
  const rows = run(args).trim().split('\n').map((l) => l.split(' ').map(Number));
  const byBuffer = new Map();
  let offShortName = 0;
  for (const [blinkPhase, scrolling, step, isShort, , ...positions] of rows) {
    if (isShort) continue;
    offShortName++;
    // What actually selects the buffer: the scroll step while scrolling, the
    // blink phase otherwise. Keying on both would cut the scroll into groups
    // of one tick, where no difference can show.
    const key = scrolling ? `scroll:${step}` : `phase:${blinkPhase}`;
    if (!byBuffer.has(key)) byBuffer.set(key, new Map());
    const seen = byBuffer.get(key);
    for (let i = 0; i < positions.length; ++i) {
      const at = `${i}`;
      if (!seen.has(at)) seen.set(at, new Set());
      seen.get(at).add(positions[i]);
    }
  }
  let swapped = 0;
  for (const seen of byBuffer.values()) {
    for (const faces of seen.values()) if (faces.size > 1) swapped++;
  }
  check(name, swapped === 0 && offShortName > 0,
    swapped ? `${swapped} position(s) changed with the frame off the short name`
            : `${offShortName} ticks off the short name, none swapped`);
}

theFrameSwapStaysOnTheShortName(
  'the prefix flash is not blanked by the frame swap', 'prefix AB P');
theFrameSwapStaysOnTheShortName(
  'a scrolling name is not blanked by the frame swap',
  'scroll AB ABCDEFGH 2000');

// SetBlinkFrames called itself once, and the first Print overflowed the stack.
// Every case above ran a Print, so reaching this line is the check.
check('Print returns', true, 'every case above called it');

console.log(failures ? `\n${failures} failure(s)` : '\nALL PASS');
process.exit(failures ? 1 : 0);
