# Measured cycles of every envelope call: the firmware's own Envelope run under
# QEMU over a grid of patches (tools/oscprofile), each executed instruction
# priced by tools/pathcost.py's Cortex-M3 table, a conditional branch as taken
# or not by where execution went next.
#
#   make cycles                           build, run, report
#   python3 tools/env_profile.py [DIS]    report from build/oscprofile
#   python3 tools/env_profile.py [DIS] --metrics
#   python3 tools/env_profile.py [DIS] --uncovered FUNCTION
#
# DIS is as for tools/osc_profile.py; tools/oscprofile/measured.py lists the
# hard failures. Each case is a note through one envelope: NoteOn, held
# blocks, NoteOff, release blocks, a NoteOn while releasing, blocks after it.
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oscprofile'))
import measured

TOOL = 'env_profile'
DRIVER = measured.source('tools/oscprofile/driver.cc')
BLOCK_SAMPLES = 1 << int(re.search(
    r'kAudioBlockSizeBits\s*=\s*(\d+)',
    measured.source('yarns/drivers/dac.h')).group(1))


def driver_constant(name):
  return int(re.search(r'const int %s = (\d+);' % name, DRIVER).group(1))


# Each case's calls, in the driver's order.
CALLS = (['NoteOn'] + ['held block'] * driver_constant('kHeldBlocks')
         + ['NoteOn, legato'] + ['held block'] * driver_constant('kHeldBlocks')
         + ['NoteOff'] + ['release block'] * driver_constant('kReleaseBlocks')
         + ['NoteOn, releasing'] + ['block after it'] * driver_constant('kRetriggerBlocks'))
BLOCK_KINDS = ('held block', 'release block', 'block after it')
TARGETS = ('gain', 'TIMBRE up', 'TIMBRE down', 'CV')
BIASES = ('bias held', 'bias stepping')
PEAKS = ('panel peak', 'peak at sustain')
HAND_OFF = '_ZN5yarns8Envelope18HandOffToNextStage'
PARTS = sorted(int(d[3:]) for d in os.listdir(measured.OUT) if re.match(r'^env\d+$', d))


def describe(case):
  target, bias, attack, decay, sustain, release, peak, amount, duration = case
  return '%s, %s, A %d D %d S %d R %d, %s, exciter %d/%d' % (
      TARGETS[target], BIASES[bias], attack, decay, sustain, release,
      PEAKS[peak], amount, duration)


def main():
  arguments = sys.argv[1:]
  if not PARTS:
    measured.fail(TOOL, 'no envelope profile: run make cycles')
  image = measured.Image(arguments)
  calls, executed = [], {}
  for part in PARTS:
    fold, case_lines, executions = measured.load_run(TOOL, 'env%d' % part)
    hand_offs = fold['flags'][HAND_OFF]
    index = 0
    for line in case_lines:
      case = tuple(int(field) for field in line[1:10])
      for kind in CALLS:
        calls.append((kind, fold['blocks'][index], hand_offs[index], case))
        index += 1
    for address, count in executions.items():
      executed[address] = executed.get(address, 0) + count
  names = image.check_identity(TOOL, executed)

  blocks = [c for c in calls if c[0] in BLOCK_KINDS]
  steady = max(c for c in blocks if c[2] == 0)
  any_block = max(blocks, key=lambda c: c[1])
  most_runs = 1 + max(c[2] for c in blocks)
  note_ons = [c for c in calls if c[0].startswith('NoteOn')]
  note_on = max(note_ons, key=lambda c: c[1])
  # The block a note arrives in: NoteOn, then the block rendered after it.
  arrivals = [(calls[i][1] + calls[i + 1][1], calls[i][3])
              for i, c in enumerate(calls) if c[0].startswith('NoteOn')]
  arrival = max(arrivals)

  if '--metrics' in arguments:
    print('steady_block %d' % steady[1])
    print('any_block %d' % any_block[1])
    print('runs_per_block %d' % most_runs)
    print('note_on %d' % note_on[1])
    print('arrival %d' % arrival[0])
    return
  if '--uncovered' in arguments:
    measured.report_unexecuted(image, names, executed,
                               arguments[arguments.index('--uncovered') + 1])
    return

  print('  %d cases, %d calls' % (len(calls) // len(CALLS), len(calls)))
  print('  %-22s %8s %8s  %s' % ('call', 'worst', 'mean', 'where the worst was'))
  for kind in sorted(set(CALLS), key=CALLS.index):
    these = [c for c in calls if c[0] == kind]
    worst = max(these, key=lambda c: c[1])
    print('  %-22s %8d %8.0f  %s' % (kind, worst[1],
                                     sum(c[1] for c in these) / float(len(these)),
                                     describe(worst[3])))
  print('  ---')
  print('  steady block, no stage hand-off   %5d cycles, %5.1f a sample  %s'
        % (steady[1], steady[1] / float(BLOCK_SAMPLES), describe(steady[3])))
  print('  any block                         %5d cycles, %5.1f a sample  %s'
        % (any_block[1], any_block[1] / float(BLOCK_SAMPLES), describe(any_block[3])))
  print('  stage runs in one block, at most  %5d' % most_runs)
  print('  NoteOn and the block after it     %5d cycles  %s'
        % (arrival[0], describe(arrival[1])))
  print('  A call whole, every rare path as often as it ran. A Cortex-M3 table')
  print('  without flash wait states: a lower bound, good for deltas.')
  measured.report_unexecuted(image, names, executed)


main()
