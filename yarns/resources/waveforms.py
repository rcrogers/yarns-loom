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
# Waveform definitions.

import numpy

WAVETABLE_SIZE = 256

# Waveforms for the trigger shaper ---------------------------------------------

numpy.random.seed(666)

def trigger_scale(x):
  if x.min() > 0:
    x = (x - x.min()) / (x.max() - x.min()) * 32767.0
  else:
    abs_max = numpy.abs(x).max()
    x = x / abs_max * 32767.0
  return numpy.round(x)


t = numpy.arange(0, WAVETABLE_SIZE + 1) / float(WAVETABLE_SIZE)

exponential = numpy.exp(-4.0 * t)
ring = numpy.exp(-3.0 * t) * numpy.cos(8.0 * t * numpy.pi)
steps = numpy.sign(numpy.sin(4.0 * t * numpy.pi)) * (2 ** (-numpy.round(t * 2.0)))
noise = numpy.random.randn(WAVETABLE_SIZE + 1, 1).ravel() * ((1 - t) ** 2)

waveforms = [('exponential', trigger_scale(exponential))]
waveforms += [('ring', trigger_scale(ring))]
waveforms += [('steps', trigger_scale(steps))]
waveforms += [('noise', trigger_scale(noise))]



"""----------------------------------------------------------------------------
Band-limited waveforms
----------------------------------------------------------------------------"""

def dither(x, order=0, type=numpy.int16):
  for i in xrange(order):
    x = numpy.hstack((numpy.zeros(1,), numpy.cumsum(x)))
  x = numpy.round(x)
  for i in xrange(order):
    x = numpy.diff(x)
  if any(x < numpy.iinfo(type).min) or any(x > numpy.iinfo(type).max):
    print 'Clipping occurred!'
  x[x < numpy.iinfo(type).min] = numpy.iinfo(type).min
  x[x > numpy.iinfo(type).max] = numpy.iinfo(type).max
  return x.astype(type)

def scale(array, min=-32766, max=32766, center=True, dither_level=2):
  if center:
    array -= array.mean()
  mx = numpy.abs(array).max()
  array = (array + mx) / (2 * mx)
  array = array * (max - min) + min
  return dither(array, order=dither_level)

# Sine wave.
sine_input = numpy.arange(WAVETABLE_SIZE + 1) / float(WAVETABLE_SIZE)
sine = -numpy.sin(2 * numpy.pi * sine_input) * 127.5 + 127.5

# Band limited waveforms.
SAMPLE_RATE=40000
num_zones = 15
bl_pulse_tables = []

wrap = numpy.fmod(
    numpy.arange(WAVETABLE_SIZE + 1) + WAVETABLE_SIZE / 2,
    WAVETABLE_SIZE)
    
quadrature = numpy.fmod(
    numpy.arange(WAVETABLE_SIZE + 1) + WAVETABLE_SIZE / 4,
    WAVETABLE_SIZE)
    
fill = numpy.fmod(
    numpy.arange(WAVETABLE_SIZE + 1),
    WAVETABLE_SIZE)

waveforms.append(('sine', scale(sine[quadrature])))

sizzle_input = sine_input * 4
sizzle = -numpy.sin(numpy.exp(sizzle_input)) * 127.5 + 127.5
waveforms.append(('sizzle', scale(sizzle)))

for zone in range(num_zones):
  f0 = 440.0 * 2.0 ** ((18 + 8 * zone - 69) / 12.0)
  if zone == num_zones - 1:
    f0 = SAMPLE_RATE / 2.0 - 1
  else:
    f0 = min(f0, SAMPLE_RATE / 2.0)
  period = SAMPLE_RATE / f0
  m = 2 * numpy.floor(period / 2) + 1.0
  i = numpy.arange(-WAVETABLE_SIZE / 2, WAVETABLE_SIZE / 2) / \
      float(WAVETABLE_SIZE)
  pulse = numpy.sin(numpy.pi * i * m) / (m * numpy.sin(numpy.pi * i) + 1e-9)
  pulse[WAVETABLE_SIZE / 2] = 1.0
  pulse = pulse[fill]

  bl_pulse_tables.append(('bandlimited_comb_%d' % zone,
                          scale(pulse[quadrature])))

waveforms.extend(bl_pulse_tables)