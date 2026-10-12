# What tools/osc_profile.py and tools/env_profile.py share: reading a profile
# run back, and the checks that make it a measurement of the firmware.
#
# Hard failures:
#   - a run traced against another image than build/oscprofile's
#   - a trace transition that does not follow from the branch before it
#   - a call the driver announced that the trace did not measure
#   - an executed function that is not instruction for instruction the
#     firmware's
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import pathcost

OUT = os.path.join(ROOT, 'build/oscprofile')
PROFILE_DIS = os.path.join(OUT, 'profile.dis')


def tools_digest():
  """What made a run's summary besides the image: run.py reruns a run whose
  tools moved."""
  digest = hashlib.sha1()
  for name in ('fold.py', 'ranges.py', 'run.py', '../pathcost.py'):
    digest.update(open(os.path.join(HERE, name), 'rb').read())
  return digest.hexdigest()


def fail(tool, message):
  sys.exit('%s: %s' % (tool, message))


def source(relative_path):
  return open(os.path.join(ROOT, relative_path), encoding='utf8').read()


def load_run(tool, directory):
  """-> (fold summary, the driver's case lines, executions per address)."""
  path = os.path.join(OUT, directory)
  try:
    fold = json.load(open(os.path.join(path, 'fold.json')))
  except (IOError, ValueError):
    fail(tool, 'no profile in %s: run make profile' % directory)
  if fold.get('image') != pathcost.image_digest(PROFILE_DIS):
    fail(tool, '%s was profiled against another image: re-run it' % directory)
  if fold['violation_count']:
    fail(tool, '%s: %d trace transitions do not follow from their branch, e.g. %s'
         % (directory, fold['violation_count'], fold['violations'][0]))
  cases = [line.split() for line in open(os.path.join(path, 'qemu_out.txt'))]
  announced = sum(int(case[-1]) for case in cases)
  if announced != len(fold['blocks']):
    fail(tool, '%s: the driver announced %d calls, the trace measured %d'
         % (directory, announced, len(fold['blocks'])))
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


class Image(object):
  """The profile image, and the firmware it is checked against: the given
  disassembly of build/yarns/yarns.elf, or the one made when the profile ran."""

  def __init__(self, arguments):
    given = [a for a in arguments if os.path.isfile(a)]
    self.functions = pathcost.parse(PROFILE_DIS)
    self.firmware = pathcost.parse(
        given[0] if given else os.path.join(OUT, 'firmware.dis'))
    self.text_of = dict(pair for body in self.functions.values() for pair in body)
    symbols = dict((parts[2], int(parts[0], 16)) for parts in
                   (l.split() for l in open(os.path.join(OUT, 'profile.sym')))
                   if len(parts) == 3)
    self.harness = (symbols['_harness_start'], symbols['_harness_end'])

  def check_identity(self, tool, executed):
    """-> the names of the functions the trace ran, each the firmware's own."""
    owner = {}
    for name, body in self.functions.items():
      for address, _ in body:
        owner[address] = name
    names = set(owner[a] for a in executed if a in owner)
    differ = []
    for name in sorted(names):
      if name not in self.firmware:
        differ.append(name + ' (not in the firmware)')
      elif ([normalized(t) for _, t in self.functions[name] if not pathcost.is_data(t)] !=
            [normalized(t) for _, t in self.firmware[name] if not pathcost.is_data(t)]):
        differ.append(name)
    if differ:
      fail(tool, 'measured code differs from the firmware: ' + ', '.join(differ))
    return names

  def unexecuted(self, name, executed):
    return [(a, t) for a, t in self.functions[name]
            if not pathcost.is_data(t) and a not in executed
            and not self.harness[0] <= a < self.harness[1]]


def source_lines():
  line_of, current = {}, None
  for row in open(PROFILE_DIS, encoding='utf8', errors='replace'):
    match = re.match(r'^/?\S*?([\w.]+\.(?:cc|h|c)):(\d+)', row.strip())
    if match:
      current = '%s:%s' % match.groups()
      continue
    match = re.match(r'^\s*([0-9a-f]+):\t', row)
    if match:
      line_of[int(match.group(1), 16)] = current
  return line_of


def report_unexecuted(image, names, executed, wanted=None):
  """Instructions of the entered functions no case ran: unmeasured."""
  if wanted is not None:
    line_of = source_lines()
    for name in sorted(n for n in names if wanted in n):
      by_line = {}
      for address, text in image.unexecuted(name, executed):
        by_line.setdefault(line_of.get(address) or '?', []).append(
            text.split('\t', 2)[-1])
      print('  %s: %d instructions no case ran'
            % (name, sum(map(len, by_line.values()))))
      for line, texts in sorted(by_line.items()):
        print('    %-28s %d: %s' % (line, len(texts), texts[0].strip()[:50]))
    return
  uncovered = [(name, image.unexecuted(name, executed)) for name in sorted(names)]
  uncovered = [(name, missing) for name, missing in uncovered if missing]
  if uncovered:
    print('  Unmeasured -- instructions of these functions no case ran, and their')
    print('  cycles counted once each (--uncovered FUNCTION lists them):')
    for name, missing in sorted(uncovered, key=lambda u: -len(u[1])):
      print('    %-60s %4d %5d' % (name[:60], len(missing), sum(
          pathcost.instruction_cycles(t) for _, t in missing)))
