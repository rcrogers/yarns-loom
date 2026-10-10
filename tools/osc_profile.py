# Measured cycles per audio block for every oscillator shape: the firmware's
# own render code run under QEMU over a grid of notes (tools/oscprofile), each
# executed instruction priced by tools/pathcost.py's Cortex-M3 table, a
# conditional branch as taken or not by where execution went next.
#
#   make profile                          build, run, report
#   python3 tools/osc_profile.py [DIS]    report from build/oscprofile
#   python3 tools/osc_profile.py [DIS] --metrics
#   python3 tools/osc_profile.py [DIS] --branches OSC_SHAPE_...
#   python3 tools/osc_profile.py [DIS] --uncovered FUNCTION
#   python3 tools/osc_profile.py --shape-count
#
# DIS, a disassembly of build/yarns/yarns.elf, is what the measured code is
# checked against; without it, the one made when the profile ran. Pass the
# current one and a profile older than the build fails instead of answering.
# tools/oscprofile/measured.py lists the other hard failures.
#
# How often a rare path runs is measured, never assumed. A worst block is the
# worst the grid reached, so the report lists every instruction of the entered
# functions that no case executed: those are unmeasured.
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oscprofile'))
import measured
import pathcost

TOOL = 'osc_profile'
SHAPES = re.findall(r'(OSC_SHAPE_\w+)', re.sub(r'//[^\n]*', '', re.search(
    r'enum OscillatorShape \{(.*?)\};', measured.source('yarns/oscillator.h'),
    re.DOTALL).group(1)))
BLOCK_SAMPLES = 1 << int(re.search(
    r'kAudioBlockSizeBits\s*=\s*(\d+)',
    measured.source('yarns/drivers/dac.h')).group(1))
MIDDLE_C_PITCH = 60 << 7
# The driver's enums, in order.
SWEEPS = ('TIMBRE rising', 'TIMBRE falling', 'TIMBRE held low', 'TIMBRE held high')
GAINS = ('gain full', 'gain rising', 'gain falling')
STACK_ACCESS = re.compile(r'^(?:ldr|str)\S*\s+\S+,\s*\[sp')


def describe(case):
  pitch, sweep, gain, block = case
  return 'MIDI %5.1f, %s, %s, block %d' % (
      pitch / 128.0, SWEEPS[sweep], GAINS[gain], block)


def per_sample(cycles):
  return cycles / float(BLOCK_SAMPLES)


def main():
  arguments = sys.argv[1:]
  if '--shape-count' in arguments:
    print(len(SHAPES))
    return
  image = measured.Image(arguments)

  rows, runs, executed = [], {}, {}
  for index, name in enumerate(SHAPES):
    fold, case_lines, executions = measured.load_run(TOOL, 'shape%d' % index)
    cases = [(int(pitch), int(sweep), int(gain), block)
             for _, _, pitch, sweep, gain, blocks in case_lines
             for block in range(int(blocks))]
    blocks = list(zip(fold['blocks'], cases))
    steady = [(c, case) for c, case in blocks if case[3] > 0]
    first = [(c, case) for c, case in blocks if case[3] == 0]
    middle_c = lambda pairs: max(c for c, case in pairs if case[0] == MIDDLE_C_PITCH)
    samples = len(blocks) * BLOCK_SAMPLES
    stack = sum(n for a, n in executions.items()
                if STACK_ACCESS.match(pathcost.mnemonic(image.text_of[a]) + ' ' +
                                      image.text_of[a].split('\t')[-1]))
    rows.append({'name': name, 'steady': max(steady), 'first': max(first)[0],
                 'c4_steady': middle_c(steady), 'c4_first': middle_c(first),
                 'least': min(c for c, _ in blocks),
                 'mean': sum(c for c, _ in blocks) / float(len(blocks)),
                 'stack': stack / float(samples)})
    runs[name] = (fold, samples)
    for address, count in executions.items():
      executed[address] = executed.get(address, 0) + count

  names = image.check_identity(TOOL, executed)

  if '--metrics' in arguments:
    for row in rows:
      print('%s %.4f %.4f %.4f %.4f' % (
          row['name'], per_sample(row['steady'][0]), per_sample(row['first']),
          per_sample(row['c4_steady']), per_sample(row['c4_first'])))
    return

  if '--branches' in arguments:
    line_of = measured.source_lines()
    name = arguments[arguments.index('--branches') + 1]
    fold, samples = runs[name]
    print('  %s: each conditional branch it ran, taken and fell through --'
          ' per sample on average, and the most in one block' % name)
    for address in sorted(set(int(k.split()[0], 16) for k in fold['branch_taken'])):
      key = '%x ' % address
      print('  %8x %-24s taken %8.4f (max %2d)  fell %8.4f (max %2d)' % (
          address, line_of.get(address) or '?',
          fold['branch_taken'].get(key + 'taken', 0) / float(samples),
          fold['branch_block_max'].get(key + 'taken', 0),
          fold['branch_taken'].get(key + 'fell', 0) / float(samples),
          fold['branch_block_max'].get(key + 'fell', 0)))
    return

  if '--uncovered' in arguments:
    measured.report_unexecuted(image, names, executed,
                               arguments[arguments.index('--uncovered') + 1])
    return

  print('  %-28s %7s %7s %7s %7s %7s %7s %7s' % (
      'shape, cycles a sample', 'worst', '1st blk', 'C4', 'C4 1st', 'least',
      'mean', 'stack'))
  for row in sorted(rows, key=lambda r: -r['steady'][0]):
    print('  %-28s %7.1f %7.1f %7.1f %7.1f %7.1f %7.1f %7.2f' % (
        row['name'].replace('OSC_SHAPE_', ''), per_sample(row['steady'][0]),
        per_sample(row['first']), per_sample(row['c4_steady']),
        per_sample(row['c4_first']), per_sample(row['least']),
        per_sample(row['mean']), row['stack']))
  print('  ---')
  print('  One render call over its %d samples: per-block setup, every sample, and'
        % BLOCK_SAMPLES)
  print('  every rare path as often as it ran. worst = the dearest block after the')
  print('  first, over every pitch, TIMBRE and gain case; 1st blk = the dearest first')
  print('  block after set_shape; C4 = those at MIDI 60; least, mean = over every')
  print('  block; stack = sp loads and stores executed a sample. A Cortex-M3 table')
  print('  without flash wait states: a lower bound, good for deltas.')
  print('  Where the worst blocks were:')
  for row in sorted(rows, key=lambda r: -r['steady'][0])[:5]:
    print('    %-24s %s' % (row['name'].replace('OSC_SHAPE_', ''),
                            describe(row['steady'][1])))
  measured.report_unexecuted(image, names, executed)


main()
