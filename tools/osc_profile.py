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
#
# How often a rare path runs is measured, never assumed. A worst block is the
# worst the grid reached, so the report lists every instruction of the entered
# functions that no case executed: those are unmeasured.
#
# Hard failures:
#   - a shape traced against another image than build/oscprofile's
#   - a trace transition that does not follow from the branch before it
#   - a block the driver rendered that the trace did not measure
#   - an executed function that is not instruction for instruction the
#     firmware's
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import pathcost

OUT = os.path.join(ROOT, 'build/oscprofile')


def source(relative_path):
  return open(os.path.join(ROOT, relative_path), encoding='utf8').read()


SHAPES = re.findall(r'(OSC_SHAPE_\w+)', re.sub(r'//[^\n]*', '', re.search(
    r'enum OscillatorShape \{(.*?)\};', source('yarns/oscillator.h'),
    re.DOTALL).group(1)))
BLOCK_SAMPLES = 1 << int(re.search(
    r'kAudioBlockSizeBits\s*=\s*(\d+)', source('yarns/drivers/dac.h')).group(1))
MIDDLE_C_PITCH = 60 << 7
# The driver's enums, in order.
SWEEPS = ('TIMBRE rising', 'TIMBRE falling', 'TIMBRE held low', 'TIMBRE held high')
GAINS = ('gain full', 'gain rising', 'gain falling')
STACK_ACCESS = re.compile(r'^(?:ldr|str)\S*\s+\S+,\s*\[sp')
IMAGE = pathcost.image_digest(os.path.join(OUT, 'profile.dis'))


def fail(message):
  sys.exit('osc_profile: ' + message)


def load_shape(index):
  directory = os.path.join(OUT, 'shape%d' % index)
  try:
    fold = json.load(open(os.path.join(directory, 'fold.json')))
  except (IOError, ValueError):
    fail('no profile for %s: run make profile' % SHAPES[index])
  if fold.get('image') != IMAGE:
    fail('%s was profiled against another image: re-run it' % SHAPES[index])
  if fold['violation_count']:
    fail('%s: %d trace transitions do not follow from their branch, e.g. %s'
         % (SHAPES[index], fold['violation_count'], fold['violations'][0]))
  cases = []
  for line in open(os.path.join(directory, 'qemu_out.txt')):
    _, _, pitch, sweep, gain, blocks = line.split()
    cases += [(int(pitch), int(sweep), int(gain), block)
              for block in range(int(blocks))]
  if len(cases) != len(fold['blocks']):
    fail('%s: the driver rendered %d blocks, the trace measured %d'
         % (SHAPES[index], len(cases), len(fold['blocks'])))
  executions = {}
  for pc, extent in fold['tb_extent'].items():
    for address in extent:
      address = int(address, 16)
      executions[address] = executions.get(address, 0) + fold['tb_counts'][pc]
  return fold, cases, executions


def normalized(text):
  """An instruction as its link address leaves it: a branch by its symbolic
  target, everything else by its encoding."""
  fields = text.split('\t')
  operation = '\t'.join(fields[2:]).split(';')[0].strip()
  if pathcost.classify(text)[0] == 'fall':
    return fields[1].strip() + ' ' + operation
  return re.sub(r'\b[0-9a-f]+ (<[^>]+>)', r'\1', operation)


def check_identity(profile, firmware, executed):
  """-> the names of the functions the trace ran, each the firmware's own."""
  owner = {}
  for name, body in profile.items():
    for address, _ in body:
      owner[address] = name
  names = set(owner[a] for a in executed if a in owner)
  differ = []
  for name in sorted(names):
    if name not in firmware:
      differ.append(name + ' (not in the firmware)')
    elif ([normalized(t) for _, t in profile[name] if not pathcost.is_data(t)] !=
          [normalized(t) for _, t in firmware[name] if not pathcost.is_data(t)]):
      differ.append(name)
  if differ:
    fail('measured code differs from the firmware: ' + ', '.join(differ))
  return names


def source_lines(dis_path):
  line_of, current = {}, None
  for row in open(dis_path, encoding='utf8', errors='replace'):
    match = re.match(r'^/?\S*?([\w.]+\.(?:cc|h|c)):(\d+)', row.strip())
    if match:
      current = '%s:%s' % match.groups()
      continue
    match = re.match(r'^\s*([0-9a-f]+):\t', row)
    if match:
      line_of[int(match.group(1), 16)] = current
  return line_of


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
  given = [a for a in arguments if os.path.isfile(a)]
  profile_dis = os.path.join(OUT, 'profile.dis')
  profile = pathcost.parse(profile_dis)
  firmware = pathcost.parse(given[0] if given else os.path.join(OUT, 'firmware.dis'))
  text_of = dict(pair for body in profile.values() for pair in body)
  symbols = dict((parts[2], int(parts[0], 16)) for parts in
                 (l.split() for l in open(os.path.join(OUT, 'profile.sym')))
                 if len(parts) == 3)
  harness = (symbols['_harness_start'], symbols['_harness_end'])

  rows, runs, executed = [], {}, {}
  for index, name in enumerate(SHAPES):
    fold, cases, executions = load_shape(index)
    blocks = list(zip(fold['blocks'], cases))
    steady = [(c, case) for c, case in blocks if case[3] > 0]
    first = [(c, case) for c, case in blocks if case[3] == 0]
    middle_c = lambda pairs: max(c for c, case in pairs if case[0] == MIDDLE_C_PITCH)
    samples = len(blocks) * BLOCK_SAMPLES
    stack = sum(n for a, n in executions.items()
                if STACK_ACCESS.match(pathcost.mnemonic(text_of[a]) + ' ' +
                                      text_of[a].split('\t')[-1]))
    rows.append({'name': name, 'steady': max(steady), 'first': max(first)[0],
                 'c4_steady': middle_c(steady), 'c4_first': middle_c(first),
                 'least': min(c for c, _ in blocks),
                 'mean': sum(c for c, _ in blocks) / float(len(blocks)),
                 'stack': stack / float(samples)})
    runs[name] = (fold, samples)
    for address, count in executions.items():
      executed[address] = executed.get(address, 0) + count

  names = check_identity(profile, firmware, executed)

  if '--metrics' in arguments:
    for row in rows:
      print('%s %.4f %.4f %.4f %.4f' % (
          row['name'], per_sample(row['steady'][0]), per_sample(row['first']),
          per_sample(row['c4_steady']), per_sample(row['c4_first'])))
    return

  line_of = source_lines(profile_dis)
  if '--branches' in arguments:
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

  def unexecuted(name):
    return [(a, t) for a, t in profile[name]
            if not pathcost.is_data(t) and a not in executed
            and not harness[0] <= a < harness[1]]

  if '--uncovered' in arguments:
    wanted = arguments[arguments.index('--uncovered') + 1]
    for name in sorted(n for n in names if wanted in n):
      by_line = {}
      for address, text in unexecuted(name):
        by_line.setdefault(line_of.get(address) or '?', []).append(text.split('\t', 2)[-1])
      print('  %s: %d instructions no case ran' % (name, sum(map(len, by_line.values()))))
      for line, texts in sorted(by_line.items()):
        print('    %-28s %d: %s' % (line, len(texts), texts[0].strip()[:50]))
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
  uncovered = [(name, unexecuted(name)) for name in sorted(names)]
  uncovered = [(name, missing) for name, missing in uncovered if missing]
  if uncovered:
    print('  Unmeasured -- instructions of these functions no case ran'
          ' (--uncovered FUNCTION lists them):')
    for name, missing in sorted(uncovered, key=lambda u: -len(u[1])):
      print('    %-60s %4d' % (name[:60], len(missing)))


main()
