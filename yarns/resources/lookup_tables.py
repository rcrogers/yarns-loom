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
# Lookup table definitions.

import itertools
import math
from fractions import Fraction

import numpy

lookup_tables_32 = []
lookup_tables = []
lookup_tables_signed = []
lookup_tables_8 = []
# lookup_tables_8_unsigned = []

"""----------------------------------------------------------------------------
LFO and portamento increments.
----------------------------------------------------------------------------"""

refresh_rate = 4000
audio_rate = 45000
excursion = 1 << 32


def lfo():
  min_frequency = 1.0 / 8.0  # Hertz
  max_frequency = 16.0  # Hertz
  
  num_values = 64
  min_increment = excursion * min_frequency / refresh_rate
  max_increment = excursion * max_frequency / refresh_rate

  rates = numpy.linspace(numpy.log(min_increment), numpy.log(max_increment), num_values)
  lookup_tables_32.append(
      ('lfo_increments', numpy.exp(rates).astype(int))
  )
lfo()



# Create lookup table for portamento.
def portamento():
  num_values = 64
  max_time = 5.0  # seconds

  gamma = 0.25
  min_time = 1.001 / refresh_rate
  min_increment = excursion / (max_time * refresh_rate)
  max_increment = excursion / (min_time * refresh_rate)
  rates = numpy.linspace(numpy.power(max_increment, -gamma), numpy.power(min_increment, -gamma), num_values)
  values = numpy.power(rates, -1/gamma).astype(int)
  lookup_tables_32.append(
      ('portamento_increments', values)
  )
portamento()



"""----------------------------------------------------------------------------
Envelope curves
-----------------------------------------------------------------------------"""

def envelope():
  # p = 1.83 is good, 2.5 also good, 2.7 gets a uniform spread of shifts
  def make_expo(linear, p = 1.95):
    return 1.0 - numpy.exp(-4 * linear)
    # # Alternate curve shape: quarter circle
    # return (1 - (1 - linear) ** p) ** (1 / p)

  def expo():
    # Map a phase to an exponential value
    num_expo_values = 256.0
    env_linear = numpy.arange(0, num_expo_values + 1) / num_expo_values
    env_linear[-1] = env_linear[-2]
    env_expo = make_expo(env_linear)
    lookup_tables.append(('env_expo', env_expo / env_expo.max() * 65535.0))

    # def make_expo_inverse(expo_value):
    #     return -numpy.log(1.0 - expo_value) / 4.0

    # env_inverse_expo = make_expo_inverse(env_expo)
    # lookup_tables.append(('env_inverse_expo', env_inverse_expo / env_inverse_expo.max() * 65535.0))
  expo()

  # Like the above, but with 7-bit phase and 7-bit value instead of 8-bit phase and 16-bit value
  # env_linear = numpy.arange(0, 128.0 + 1) / 128
  # env_expo_7bit = make_expo(env_linear)
  # lookup_tables_8.append(('env_expo_7bit', env_expo_7bit / env_expo_7bit.max() * 127.0))


  # # Array of length 128 that maps a 7-bit exponential value to a 16-bit phase
  # env_expo_to_phase = numpy.interp(
  #     numpy.arange(0, 128),
  #     numpy.linspace(0, 127, len(env_expo)),
  #     env_expo * 65535.0
  # )
  # lookup_tables_16.append(('env_expo_to_phase', env_expo_to_phase.astype(int)))

  env_shift_samples = 16.0

  def shifts():

    # For a linear slope y = x, with each shift representing an equal slice of time between x = 1 and x = 1, what is the cumulative value from the piecewise shifted slope?
    def get_cumulative_value_from_slope_shifts(x): # x between 0 and 1
      y = 0
      for i, shift in enumerate(expo_slope_shift):
        shifted_slope = 1 * 2 ** shift
        shift_x_start = i / len(expo_slope_shift)
        shift_x_end = (i + 1) / len(expo_slope_shift)
        if x < shift_x_start:
          break
        if x < shift_x_end:
          y += shifted_slope * (x - shift_x_start)
          break
        y += shifted_slope * (shift_x_end - shift_x_start)
      return y

    env_shift_linear = numpy.arange(1, env_shift_samples + 1) / env_shift_samples
    env_shift_expo = make_expo(env_shift_linear)
    env_shift_expo /= env_shift_expo.max()
    dx = 1 / env_shift_samples
    y_actual = 0
    shift = None
    errors = []
    expo_slope_shift = []
    # expo_slope_shift = [2, 1, 0, -1, -2, -3, -4, -5]
    assert(len(env_shift_expo) == env_shift_samples)
    for idx, y_ideal in enumerate(env_shift_expo):
      dy_ideal = y_ideal - (0 if idx == 0 else env_shift_expo[idx - 1])
      dy_actual = y_ideal - y_actual
      dy_weighted = (dy_ideal + dy_actual) / 2
      slope = dy_weighted / dx
      # slope = dy_actual / dx

      # slope **= 0.915 # avg -1.22%, final 0.05%
      # slope = (slope ** 0.98) * 0.97 # avg 0.66%, final 0.000%
      # print(slope)
      ideal_slope_power = math.log(slope, 2) if slope else -float('inf')
      # ideal_slope_power -= 0.05 # avg 1.38%, final 0.17%
      # ideal_slope_power *= 0.91 # avg -1.21%, final 0.1%
      shift = round(ideal_slope_power)
      shift = -32 if shift < -32 else shift
      # shift = expo_slope_shift[idx]
      y_actual += 2 ** shift * dx
      print('y_actual', y_actual, 'y_ideal', y_ideal)
      error = 100 * (y_actual - y_ideal) / y_ideal
      errors.append(error)
      print(
        'idx',
        str.rjust(str(idx), 4),
        # 'ideal slope power',
        # str.rjust(format(
        #   round(ideal_slope_power, 3)
        # , '.3f'), 6),
        'slope power',
        str.rjust(str(shift), 3),
        'error %',
        str.rjust(format(
          round(error, 3)
        , '.3f'), 6)
      )

      # Slope shift should be monotonically decreasing
      if idx > 0:
        assert(shift <= expo_slope_shift[idx-1])
      expo_slope_shift.append(shift)

    print('\navg abs error pct', sum(abs(e) for e in errors) / len(errors))

    lookup_tables_8.append(
        ('expo_slope_shift', expo_slope_shift)
    )
  shifts()
  
  # raise Exception('stop here')

  max_time = 10.0  # seconds
  num_duration_values = 128
  gamma = 0.25
  envelope_rate = audio_rate
  # min_time = env_shift_samples / envelope_rate
  min_time = 4.0 / envelope_rate
  # print('min_time', min_time)
  min_samples = min_time * envelope_rate
  max_samples = max_time * envelope_rate

  min_increment = excursion / max_samples
  max_increment = excursion / min_samples
  rates = numpy.linspace(numpy.power(max_increment, -gamma), numpy.power(min_increment, -gamma), num_duration_values)
  values = list(numpy.power(rates, -1/gamma).astype(int))
  values.append(values[-1])
  lookup_tables_32.append(
      ('envelope_phase_increments', values)
  )

  # sample_counts = excursion / numpy.array(values)
  # lookup_tables_32.append(
  #   ('envelope_sample_counts', sample_counts)
  # )
envelope()

# Create table for pitch.
def pitch():
  a4_midi = 69
  a4_pitch = 440.0
  highest_octave = 116
  notes = numpy.arange(
      highest_octave * 128.0,
      (highest_octave + 12) * 128.0 + 16,
      16)
  pitches = a4_pitch * 2 ** ((notes - a4_midi * 128) / (128 * 12))
  increments = excursion / audio_rate * pitches

  lookup_tables_32.append(
      ('oscillator_increments', increments.astype(int)))
pitch()

"""----------------------------------------------------------------------------
Arpeggiator patterns
----------------------------------------------------------------------------"""

def XoxTo16BitInt(pattern):
  uint16 = 0
  i = 0
  for char in pattern:
    if char == 'o':
      uint16 += (2 ** i)
      i += 1
    elif char == '-':
      i += 1
  assert i == 16
  return uint16


def ConvertPatterns(patterns):
  return [XoxTo16BitInt(pattern) for pattern in patterns]


lookup_tables.append(
  ('arpeggiator_patterns', ConvertPatterns([
      'oooo oooo oooo oooo',
      'o-o- o-o- o-o- o-o-',
      'o-o- oooo o-o- oooo',
      'o-o- oo-o o-o- oo-o',
      'o-o- o-oo o-o- o-oo',
      'o-o- o-o- oo-o -o-o',
      'o-o- o-o- o--o o-o-',
      'o-o- o--o o-o- o--o',
      
      'o--o ---- o--o ----',
      'o--o --o- -o-- o--o',
      'o--o --o- -o-- o-o-',
      'o--o --o- o--o --o-',
      'o--o o--- o-o- o-oo',
      
      'oo-o -oo- oo-o -oo-',
      'oo-o o-o- oo-o o-o-',
      
      'ooo- ooo- ooo- ooo-',
      'ooo- oo-o o-oo -oo-',
      'ooo- o-o- ooo- o-o-',
      
      'oooo -oo- oooo -oo-',
      'oooo o-oo -oo- ooo-',
      
      'o--- o--- o--o -o-o',
      'o--- --oo oooo -oo-',
      'o--- ---- o--- o-oo'])))
      

      
"""----------------------------------------------------------------------------
Euclidean patterns
----------------------------------------------------------------------------"""

def Flatten(l):
  if hasattr(l, 'pop'):
    for item in l:
      for j in Flatten(item):
        yield j
  else:
    yield l


def EuclideanPattern(k, n):
  pattern = [[1]] * k + [[0]] * (n - k)
  while k:
    cut = min(k, len(pattern) - k)
    k, pattern = cut, [pattern[i] + pattern[k + i] for i in xrange(cut)] + \
      pattern[cut:k] + pattern[k + cut:]
  return pattern


table = []
for num_steps in xrange(1, 33):
  for num_notes in xrange(32):
    num_notes = min(num_notes, num_steps)
    bitmask = 0
    for i, bit in enumerate(Flatten(EuclideanPattern(num_notes, num_steps))):
      if bit:
        bitmask |= (1 << i)
    table.append(bitmask)

lookup_tables_32 += [('euclidean', table)]



"""----------------------------------------------------------------------------
Just intonation tuning table
----------------------------------------------------------------------------"""

intervals = [
    (1, 1),
    (9, 8),
    (5, 4),
    (4, 3),
    (3, 2),
    (5, 3),
    (15, 8),
    (256, 243),
    (16, 15),
    (10, 9),
    (32, 27),
    (6, 5),
    (81, 64),
    (45, 32),
    (1024, 729),
    (64, 45),
    (729, 512),
    (128, 81),
    (8, 5),
    (27, 16),
    (16, 9),
    (9, 5),
    (243, 128),
    # (7, 4),
    # (7, 5),
    # (7, 6),
    # (10, 7),
    # (11, 7),
    # (15, 14),
    # (21, 11)
    ]

consonant_intervals = []
for interval in intervals:
  p, q = interval
  midi_ratio = int(numpy.round(1536 * numpy.log2(float(p) / q)))
  # The larger the number in the numerator and denominator of the fraction
  # representing the interval, the more dissonant it is.
  consonance_score = (numpy.log2(p * q) ** 2)
  consonant_intervals.append((midi_ratio, consonance_score))

consonance_table = [0] * 1536
for i in xrange(1536):
  nearest = numpy.argmin(
      [min(abs(i - p), abs(i - p - 1536)) for (p, _) in consonant_intervals])
  index, consonance_score = consonant_intervals[nearest]
  consonance_score += min((i - index) ** 2, (i - index - 1536) ** 2)
  consonance_table[i] = consonance_score

lookup_tables.append(('consonance', consonance_table))



"""----------------------------------------------------------------------------
List of 22 shrutis with different notation schemes.

The most common notation scheme is in the 3th column.
----------------------------------------------------------------------------"""

shrutis = [
  # Swara ref 1, Swara ref 2, Swara, Swara (carnatic, common), Just, Ratio
  ('S', 'sa', 's', 'C', 1),
  ('r1', 'ra', 'r1', 'pC#', 256.0/243.0),
  ('r2', 'ri', 'r2', 'C#', 16.0/15.0),
  ('R1', 'ru', 'r3', '?', 10.0/9.0),
  ('R2', 're', 'r4', 'D', 9.0/8.0),
  ('g1', 'ga', 'g1', 'pD#', 32.0/27.0),
  ('g2', 'gi', 'g2', 'D#', 6.0/5.0),
  ('G1', 'gu', 'g3', 'E', 5.0/4.0),
  ('G2', 'ge', 'g4', 'pE', 81.0/64.0),
  ('m1', 'ma', 'm1', 'F', 4.0/3.0),
  ('m2', 'mi', 'm2', '?', 27.0/20.0),
  ('M1', 'mu', 'm3', 'F#', 45.0/32.0),
  ('M2', 'me', 'm4', 'pF#', 729.0/512.0),
  ('P', 'pa', 'p', 'G', 3.0/2.0),
  ('d1', 'dha', 'd1', 'pG#', 128.0/81.0),
  ('d2', 'dhi', 'd2', 'G#', 8.0/5.0),
  ('D1', 'dhu', 'd3', 'A', 5.0/3.0),
  ('D2', 'dhe', 'd4', 'pA', 27.0/16.0),
  ('n1', 'na', 'n1', 'A#', 16.0/9.0),
  ('n2', 'ni', 'n2', '?', 9.0/5.0),
  ('N1', 'nu', 'n3', 'B', 15.0/8.0),
  ('N2', 'ne', 'n4', 'pB', 243.0/128.0),
  ('!', '!', '!', '!', 100),
]

def DecodeShrutiChart(line):
  values = [
      's',
      'r1',
      'r2',
      'r3',
      'r4',
      'g1',
      'g2',
      'g3',
      'g4',
      'm1',
      'm2',
      'm3',
      'm4',
      'p',
      'd1',
      'd2',
      'd3',
      'd4',
      'n1',
      'n2',
      'n3',
      'n4'
  ]
  decoded = []
  for i, x in enumerate(line):
    if x != '-':
      decoded.append(values[i])
  return ' '.join(decoded)


"""----------------------------------------------------------------------------
A recommended key on the keyboard for each of the swara.

From:
http://commons.wikimedia.org/wiki/Melakarta_ragams_(svg)
----------------------------------------------------------------------------"""

recommended_keys = {
  's': 0,
  'r1': 1,
  'r2': 1,
  'r3': 2,
  'r4': 2,
  'g1': 3,
  'g2': 3,
  'g3': 4,
  'g4': 4,
  'm1': 5,
  'm2': 6,
  'm3': 6,
  'm4': 6,
  'p': 7,
  'd1': 8,
  'd2': 8,
  'd3': 9,
  'd4': 9,
  'n1': 10,
  'n2': 10,
  'n3': 11,
  'n4': 11
}


shruti_dictionary = {}
for entry in shrutis:
  for name in entry[:-1]:
    shruti_dictionary[name] = entry[-1]


def Compute(scale):
  """Translate a list of 12 note/swaras names into pitch corrections."""
  values = [shruti_dictionary.get(x) for x in scale.split(' ')]
  while 100 in values:
    for i, v in enumerate(values):
      if v == 100:
        values[i] = values[i- 1]
  equal = 2 ** (numpy.arange(12.0) / 12.0)
  shifts = numpy.round((numpy.log2(values / equal) * 12 * 128)).astype(int)
  silences = numpy.where(shifts > 127)
  if len(silences[0]):
    shifts[silences[0]] = 32767
  return shifts


def LayoutRaga(raga, silence_other_notes=False):
  """Find a good assignments of swaras to keys for a raga."""
  raga = raga.lower()
  scale = numpy.zeros((12,))
  mapping = ['' for i in range(12)]
  for swara in raga.split(' '):
    key = recommended_keys.get(swara)
    mapping[key] = swara

  # Fill unassigned notes
  for i, n in enumerate(mapping):
    if n == '':
      if silence_other_notes:
        mapping[i] = '!'
      else:
        candidates = []
        for swara, key in recommended_keys.items():
          if key == i:
            candidates.append(swara)
        for candidate in candidates:
          if candidate[0] != mapping[i - 1]:
            mapping[i] = candidate
            break
        else:
          mapping[i] = candidates[0]
    
  scale = [shruti_dictionary.get(swara) for swara in mapping]
  return Compute(' '.join(mapping))


altered_e_b = [0, 0, 0, 0, -64, 0, 0, 0, 0, 0, 0, -64]
altered_e = [0, 0, 0, 0, -64, 0, 0, 0, 0, 0, 0, 0]
altered_e_a = [0, 0, 0, 0, -64, 0, 0, 0, 0, -64, 0, 0]

scales = [
    ('pythagorean', Compute('C pC# D pD# pE F pF# G pG# pA A# pB')),
    ('1/4 eb', numpy.array(altered_e_b, dtype=int)),
    ('1/4 e', numpy.array(altered_e, dtype=int)),
    ('1/4 ea', numpy.array(altered_e_a, dtype=int)),
    ('bhairav',
     LayoutRaga(DecodeShrutiChart('sr-----g-m---pd-----n-'), True)),
    ('gunakri',
     LayoutRaga(DecodeShrutiChart('s-r------m---p-d------'), True)),
    ('marwa',
     LayoutRaga(DecodeShrutiChart('s-r----g---m----d---n-'), True)),
    ('shree',
     LayoutRaga(DecodeShrutiChart('sr-----g---m-pd-----n-'), True)),
    ('purvi',
     LayoutRaga(DecodeShrutiChart('s-r----g---m-p-d----n-'), True)),
    ('bilawal',
     LayoutRaga(DecodeShrutiChart('s---r--g-m---p---d--n-'), True)),
    ('yaman',
     LayoutRaga(DecodeShrutiChart('s---r---g---mp---d---n'), True)),
    ('kafi',
     LayoutRaga(DecodeShrutiChart('s--r-g---m---p--d-n---'), True)),
    ('bhimpalasree',
     LayoutRaga(DecodeShrutiChart('s---r-g--m---p---d-n--'), True)),
    ('darbari',
     LayoutRaga(DecodeShrutiChart('s---rg---m---pd---n---'), True)),
    ('bageshree',
     LayoutRaga(DecodeShrutiChart('s--r-g---m---p--d-n---'), True)),
    ('rageshree',
     LayoutRaga(DecodeShrutiChart('s---r--g-m---p--d-n---'), True)),
    ('khamaj',
     LayoutRaga(DecodeShrutiChart('s---r--g-m---p---dn--n'), True)),
    ('mi\'mal',
     LayoutRaga(DecodeShrutiChart('s---rg---m---p--d-n-n-'), True)),
    ('parameshwari',
     LayoutRaga(DecodeShrutiChart('sr---g---m------d-n---'), True)),
    ('rangeshwari',
     LayoutRaga(DecodeShrutiChart('s---rg---m---p------n-'), True)),
    ('gangeshwari',
     LayoutRaga(DecodeShrutiChart('s------g-m---pd---n---'), True)),
    ('kameshwari',
     LayoutRaga(DecodeShrutiChart('s---r------m-p--d-n---'), True)),
    ('pa. kafi',
     LayoutRaga(DecodeShrutiChart('s---rg---m---p---dn---'), True)),
    ('natbhairav',
     LayoutRaga(DecodeShrutiChart('s---r--g-m---pd-----n-'), True)),
    ('m.kauns',
     LayoutRaga(DecodeShrutiChart('s---r---gm----d---n---'), True)),
    ('bairagi',
     LayoutRaga(DecodeShrutiChart('sr-------m---p----n---'), True)),
    ('b.todi',
     LayoutRaga(DecodeShrutiChart('sr---g-------p----n---'), True)),
    ('chandradeep',
     LayoutRaga(DecodeShrutiChart('s----g---m---p----n---'), True)),
    ('kaushik todi',
     LayoutRaga(DecodeShrutiChart('s----g---m-m--d-------'), True)),
    ('jogeshwari',
     LayoutRaga(DecodeShrutiChart('s----g-g-m------d-n---'), True)),
    ('rasia',
     LayoutRaga(DecodeShrutiChart('s---r---g---mp---d---n'), True)),
]

for scale, values in scales:
  lookup_tables_signed.append(('scale_%s' % scale, values))


"""----------------------------------------------------------------------------
Integer ratios for clock sync and FM
----------------------------------------------------------------------------"""

lookup_tables_string = []

MAXITER = 151
def minkowski_inv(x):
    if x > 1 or x < 0:
        return math.floor(x) + minkowski_inv(x - math.floor(x))
 
    if x == 1 or x == 0:
        return x
 
    cont_frac = [0]
    current = 0
    count = 1
    i = 0
 
    while True:
        x *= 2
 
        if current == 0:
            if x < 1:
                count += 1
            else:
                cont_frac.append(0)
                cont_frac[i] = count
 
                i += 1
                count = 1
                current = 1
                x -= 1
        else:
            if x > 1:
                count += 1
                x -= 1
            else:
                cont_frac.append(0)
                cont_frac[i] = count
 
                i += 1
                count = 1
                current = 0
 
        if x == math.floor(x):
            cont_frac[i] = count
            break
 
        if i == MAXITER:
            break
 
    ret = 1.0 / cont_frac[i]
    for j in range(i - 1, -1, -1):
        ret = cont_frac[j] + 1.0 / ret
 
    return 1.0 / ret

# print(minkowski_inv(7.0 / 5)) # sqrt(2)
# print(minkowski_inv(2.0 / 5)) # sqrt(2) - 1
# print(minkowski_inv(5.0 / 3)) # golden ratio
# print('\n')

# Build normal form of carrier/modulator ratio, then use it to categorize
# ratios, and calculate a correction factor for the carrier to keep a consistent
# fundamental frequency
# Thanks to Barry Truax: https://www.sfu.ca/~truax/fmtut.html
def build_harmonic_fm(cm_ratio):
  nf_car = car = cm_ratio.numerator
  nf_mod = mod = cm_ratio.denominator
  while not (
    (nf_car == 1 and nf_mod == 1) or
    nf_mod >= 2.0 * nf_car
  ):
    nf_car = abs(nf_car - nf_mod)
  family = Fraction(nf_car, nf_mod)
  carrier_correction = car / float(nf_car)
  # print(cm_ratio, family, nf_car, carrier_correction)
  return (family, cm_ratio, carrier_correction)

fm_mc_ratio_names = []
fm_carrier_corrections = []
fm_mc_ratios = [] # Modulator/carrier ratios

harmonic_fms = map(
  build_harmonic_fm,
  numpy.unique([Fraction(*x) for x in itertools.product(
    range(1, 10),
    range(1, 10),
  )]))
def sorter(fm_tuple):
  family, cm_ratio, _carrier_correction = fm_tuple
  return (family.numerator, family.denominator, cm_ratio)
harmonic_fms = sorted(harmonic_fms, key=sorter)
seen_family = {}
used_harmonic_cm_ratios = []
for (family, cm_ratio, carrier_correction) in harmonic_fms:
  family = str(family)
  # print((family, cm_ratio, carrier_correction))
  if seen_family.has_key(family):
    # Among the ratios that share a given normal form, the set of sidebands is shared, but the energy skews toward higher sidebands as the carrier frequency increases.  This is timbrally interesting, but the effect is broadly similar to increasing the FM index.  Additionally, the carrier correction required by non-normal-form ratios only works when the FM index is greater than zero.  Finally, there are 15 normal-form ratios, plus 40 non-normal-form ratios.  There's more bang for the buck in using normal forms only.
    continue
  seen_family[family] = True
  similar_ratios = list(r for r in used_harmonic_cm_ratios if (r / cm_ratio) % 1 == 0)
  if len(similar_ratios) > 0: # Sidebands are a subset of a previous ratio's
    continue
  if (cm_ratio != 1):
    used_harmonic_cm_ratios.append(cm_ratio)

  mc_ratio = Fraction(cm_ratio.denominator, cm_ratio.numerator)
  fm_mc_ratio_names.append(
    str(mc_ratio.numerator) + str(mc_ratio.denominator) + ' FM ' + str(mc_ratio.numerator) + '/' + str(mc_ratio.denominator)
  )
  fm_mc_ratios.append(float(mc_ratio))
  # fm_carrier_corrections.append(128 * 12 * numpy.log2(carrier_correction))

inv_minkowski_count = 0
for cm_ratio in reversed(used_harmonic_cm_ratios):
  inv_mink = minkowski_inv(float(cm_ratio))
  if ((inv_mink * 10) % 1 == 0):
    continue # Skip integer ratios
  inv_minkowski_count += 1
  # print([str(cm_ratio), 'inv_mink', inv_mink])
  # print([str(cm_ratio), '1 / inv_mink', 1 / inv_mink])
  # print('')
  fm_mc_ratios.append(1.0 / inv_mink)
  fm_mc_ratio_names.append('?' + str(inv_minkowski_count) + ' FM 1/?-1(' + str(cm_ratio.numerator) + '/' + str(cm_ratio.denominator) + ')')

for (name, mc_ratio) in ([
  # 0.5 * 2 ** (16 / 1200.0), # 1/75 octave? Vibrato-like
  # 1.0 * 2 ** (16 / 1200.0),
  # 2 * 2 ** (16 / 1200.0),
  ('\\xC1""4 FM \\xC1""/4', numpy.pi / 4), # 4 : pi
  ('\\xC1""3 FM \\xC1""/3', numpy.pi / 3), # 3 : pi
  ('\\xC1""2 FM \\xC1""/2', numpy.pi / 2), # 2 : pi
  (' \\xC1"" FM \\xC1""*1', numpy.pi), # 1 : pi
  ('2\\xC1"" FM \\xC1""*2', numpy.pi * 2), # 1 : 2pi
  ('3\\xC1"" FM \\xC1""*3', numpy.pi * 3), # 1 : 3pi
  ('3\\xC1"" FM \\xC1""*3/2', numpy.pi * 3 / 2), # 2 : 3pi
  # ('\\xC0""2 FM', numpy.sqrt(2) / 2),
  # ('1\\xC0"" FM', numpy.sqrt(2) * 1), # Great bell
  # ('2\\xC0"" FM', numpy.sqrt(2) * 2),
  # ('3\\xC0"" FM', numpy.sqrt(2) * 3),
  # ('4\\xC0"" FM', numpy.sqrt(2) * 4),
  # ('1\\xC0"" FM', numpy.sqrt(3)),
  # ('2\\xC0"" FM', numpy.sqrt(3) * 2), # Even better bell
] + [
  # Metallic means
  # ('M' + str(n) + ' FM METALLICMEAN(' + str(n) + ')', (n + numpy.sqrt(n ** 2 + 4)) / 2) for n in range(1, 10)
]):
  fm_mc_ratios.append(mc_ratio)
  fm_mc_ratio_names.append(name)

# for n in [2, 3, 5, 6, 7, 8]:
#   fm_modulator_intervals.append(128 * 12 * numpy.log2(numpy.sqrt(n)))
#   fm_ratio_names.append('\\xC0""' + str(n))

lookup_tables_string.append(('fm_ratio_names', fm_mc_ratio_names))
# lookup_tables_signed.append(('fm_carrier_corrections', fm_carrier_corrections))

fm_modulator_intervals = [128 * 12 * numpy.log2(r) for r in fm_mc_ratios]
print('FM modulator intervals: ', fm_modulator_intervals)
lookup_tables_signed.append(('fm_modulator_intervals', fm_modulator_intervals))

FM_INDEX_SCALING_BASE = 40
# print('FM ratios: ', fm_ratios)
fm_index_scales = numpy.array([FM_INDEX_SCALING_BASE / r for r in fm_mc_ratios])
# print('FM index scale for ratios: ', fm_index_scales)

# Convert to q4.4, downshifted by 2
# lookup_tables_8_unsigned.append(('fm_index_scales_q4_4', fm_index_scales * 16 / 4))

fm_index_upshifts_f = numpy.log2(fm_index_scales)
lookup_tables_8.append(('fm_index_2x_upshifts', numpy.round(fm_index_upshifts_f * 2)))


clock_ratio_ticks = []
clock_ratio_names = []
for ratio in numpy.unique([Fraction(*x) for x in itertools.product(range(1, 10), range(1, 10))]):
  ticks = (24.0 / ratio)
  if ticks % 1 != 0:
    print('Skipping non-integer clock ratio', ratio, 'ticks', ticks)
    continue
  if ratio < 1.0 / 8:
    print('Skipping very slow clock ratio', ratio, 'ticks', ticks)
    continue
  clock_ratio_ticks.append(float(ticks))
  clock_ratio_names.append(
    str(ratio.numerator) + str(ratio.denominator) + ' ' + str(ratio.numerator) + '/' + str(ratio.denominator)
  )
lookup_tables.append(('clock_ratio_ticks', clock_ratio_ticks))
lookup_tables_string.append(('clock_ratio_names', clock_ratio_names))

"""----------------------------------------------------------------------------
SVF coefficients
----------------------------------------------------------------------------"""

cutoff = 440.0 * 2 ** ((numpy.arange(0, 257) - 69) / 12.0)
f = cutoff / audio_rate
f[f > 1 / 8.0] = 1 / 8.0
f = 2 * numpy.sin(numpy.pi * f)
resonance = numpy.arange(0, 257) / 260.0
damp = numpy.minimum(2 * (1 - resonance ** 0.25),
       numpy.minimum(2, 2 / f - f * 0.5))

lookup_tables.append(
    ('svf_cutoff', f * 32767.0)
)

lookup_tables.append(
    ('svf_damp', damp * 32767.0)
)

lookup_tables.append(
    ('svf_scale', ((damp / 2) ** 0.5) * 32767.0)
)