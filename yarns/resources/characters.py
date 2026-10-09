#!/usr/bin/python2.5
#
# Copyright 2014 Emilie Gillet.
#
# Author: Emilie Gillet (emilie.o.gillet@gmail.com)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 
# See http://creativecommons.org/licenses/MIT/ for more information.
#
# -----------------------------------------------------------------------------
#
# Characters definitions.
#
#  --a----
# |\  |  /|
# f g h j b
# |  \|/  |
#  -p- -k-
# |  /|\  |
# e n m l c
# |/  |  \|
#  ----d--

characters = []

MASKS = {
  'a': 0x8000,
  'b': 0x4000,
  'c': 0x2000,
  'd': 0x1000,
  'e': 0x0800,
  'f': 0x0400,
  'g': 0x0200,
  'h': 0x0100,
  'j': 0x80,
  'k': 0x40,
  'l': 0x20,
  'm': 0x10,
  'n': 0x08,
  'p': 0x04,
}

CZ = 'afpkj'

low_pass = 'pl'
peaking = 'dnlm'
notch = 'pmk'
band_pass = 'nl'
high_pass = 'nk'

characters = {
  'A': 'afepkbc',
  'B': 'adhmbck',
  'C': 'afed',
  'D': 'adhmbc',
  'E': 'afedkp',
  'F': 'afepk',
  'G': 'afedck',
  'H': 'febcpk',
  'I': 'adhm',
  'J': 'bcde',
  'K': 'efpjl',
  'L': 'def',
  'M': 'efgjbc',
  'N': 'efglcb',
  'O': 'abcdef',
  'P': 'abpkef',
  'Q': 'abcdefl',
  'R': 'abpkefl',
  'S': 'afpkcd',
  'T': 'ahm',
  'U': 'bcdef',
  'V': 'fenj',
  'W': 'fenlcb',
  'X': 'gjln',
  'Y': 'gjm',
  'Z': 'ajnd',
  
  'a': 'abpkecd',
  'b': 'fedlp',
  'c': 'pked',
  'd': 'bcdnk',
  'e': 'nped',
  'f': 'afpe',
  'g': 'agkbcd',
  'h': 'fpkec',
  'i': 'mpkd',
  'j': 'kcd',
  'k': 'hmjl',
  'l': 'jm',
  'm': 'epkmc',
  'n': 'mkc',
  'o': 'pkecd',
  'p': 'afpje',
  'q': 'afpkbl',
  'r': 'mk',
  's': 'kld',
  't': 'fedp',
  'u': 'edc',
  'v': 'en',
  'w': 'enlc',
  'x': 'gjln',
  'y': 'gkbcd',
  'z': 'pnd',
  
  '0': 'abcdefjn',
  '1': 'bcj',
  '2': 'abknd',
  '3': 'abcdk',
  '4': 'fpkbc',
  '5': 'afpld',
  '6': 'afpkcde',
  '7': 'ajm',
  '8': 'abcdefpk',
  '9': 'abcpkfd',

  '!': 'hm',
  '"': 'fh',
  '#': 'pkdhmbc',
  '$': 'afpkcdhm',
  '%': 'jnfc',
  '&': 'aghpeld',
  '\'': 'h',
  '(': 'afed',
  ')': 'abcd',
  '*': 'ghjmnlpk',
  '+': 'hmpk',
  ',': 'n',
  '-': 'pk',
  '.': 'm',
  '/': 'jn',
  ':': 'hm',
  ';': 'hn',
  '<': 'jl',
  '>': 'gn',
  '?': 'fabkm',
  '=': 'pkd',
  '@': 'kmcbafed',
  '[': 'afed',
  ']': 'abcd',
  '\\': 'gl',
  '^': 'nl',
  '_': 'd',
  '`': 'g',
  '{': 'pgnad',
  '|': 'hm',
  '}': 'ajldk',
  '~': 'pk',
  
  # LRDU
  '\x80': 'jlbc',
  '\x81': 'efgn',
  '\x82': 'agj',
  '\x83': 'dnlm',
  
  # LRDU arrow
  '\xC6': 'gjlnad',    # Exciter amount: X bracketed top and bottom, a height
  '\xC7': 'gjlnfebc',  # Exciter duration: X bracketed left and right, a width

  '\x84': 'jkl',
  '\x85': 'gpn',
  '\x86': 'ghj',
  '\x87': 'nml',
  
  # Waveforms
  '\x88': 'efgl',  # Saw
  '\x89': 'pjb',   # CSaw
  '\x8A': 'ml',    # Baby saw
  '\x8B': 'nl',    # Triangle
  '\x8C': 'efabc', # Square
  '\x8D': 'epkc',   # Baby square
  '\x8E': 'dhm',   # Pulse
  '\x8F': 'efgkc', # ADSR
  
  # Spinner
  '\x90': 'abcdefn',
  '\x91': 'abcdefp',
  '\x92': 'abcdefg',
  '\x93': 'abcdefh',
  '\x94': 'abcdefj',
  '\x95': 'abcdefk',
  '\x96': 'abcdefl',
  '\x97': 'abcdefm',

  # Spinner 2
  '\x98': 'ab',
  '\x99': 'abc',
  '\x9A': 'bcd',
  '\x9B': 'cde',
  '\x9C': 'de',
  '\x9D': 'def',
  '\x9E': 'efa',
  '\x9F': 'fab',

  # Filter types
  '\xA0': low_pass,
  '\xA1': high_pass,
  '\xA2': notch,

  # CZ waveforms
  '\xB0': CZ + low_pass,
  '\xB1': CZ + peaking,
  '\xB2': CZ + band_pass,
  '\xB3': CZ + high_pass,

  '\xC0': 'plcb', # sqrt
  '\xC1': 'fhak', # pi

  # Rotated transfer function symbols (180 degrees)
  '\xC2': 'agp',  # rotated 's' (sine)
  '\xC3': 'gj',   # rotated '^' (tri)
  '\xC4': 'abjk', # rotated 'e' (expo)

  '\xC5': 'fpkc', # down stair-step
  
  '\xFF': 'abcdefghjklmnp',
  
  'null': 'null'
}

# The other frame of a blinking glyph, whole rather than a difference, so it
# can be any pattern instead of only a subset of the first. Declared beside the
# glyph, so nothing downstream has to know which characters are special -- the
# display asks a character for its other frame, the same way it asks what lights.
blinking = {
  '\xC6': 'gjln',  # Exciter amount: the height rails drop away, the X holds
  '\xC7': 'gjln',  # Exciter duration: likewise the width rails
}

character_table = []
for i in xrange(256):
  segments = characters.get(chr(i), '')
  character_table.append(sum(MASKS[segment] for segment in set(segments)))

# (code, frame) pairs, ended by a zero code. Only a couple of glyphs blink, so
# a short list costs a great deal less than a second 256-entry table, and it is
# read once per Print rather than per refresh.
blink_table = []
for code in sorted(blinking):
  blink_table.append(ord(code))
  blink_table.append(sum(MASKS[segment] for segment in set(blinking[code])))
blink_table.append(0)

characters = [('characters', character_table),
              ('blinking_characters', blink_table)]
