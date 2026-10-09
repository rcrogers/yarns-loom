# Folds a QEMU `-d exec,in_asm,nochain` trace of the profile harness into the
# cycles each audio block executed, priced by tools/pathcost.py's table.
#
#   fold.py DIS SYM [SYMBOL ...] < trace > summary.json
#
# For each SYMBOL (a substring of one function's name), the summary counts,
# per measured call, how many times that function was entered.
#
# The trace is the instruction stream, exactly: in_asm gives each translation
# block's instructions once, exec gives every block executed in order. A
# conditional branch is priced taken or not by where the next block starts.
# Every transition is checked against the branch that ended the block before
# it; any that does not fit is counted, and the summary is worthless unless
# that count is zero.
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pathcost

dis_path, sym_path, flag_names = sys.argv[1], sys.argv[2], sys.argv[3:]

symbols = {}
for line in open(sym_path):
  parts = line.split()
  if len(parts) == 3:
    symbols[parts[2]] = int(parts[0], 16)
BEGIN, END = symbols['ProfileBlockBegin'], symbols['ProfileBlockEnd']


def entry_of(substring):
  matches = [address for name, address in symbols.items() if substring in name]
  if len(matches) != 1:
    sys.exit('fold.py: %d symbols match %s' % (len(matches), substring))
  return matches[0]


FLAGS = dict((entry_of(name), name) for name in flag_names)
HARNESS = (symbols['_harness_start'], symbols['_harness_end'])
REFUND = pathcost.BRANCH_CYCLES - pathcost.NOT_TAKEN_BRANCH_CYCLES


def computed_destination(text):
  """A jump whose destination is a register or a table: any next block fits."""
  name = pathcost.mnemonic(text)
  operands = text.split('\t')[-1].split(';')[0]
  return bool(name in ('tbb', 'tbh') or
              re.match(r'^blx?$', name) and ' <' not in operands or
              re.match(r'^(?:ldr|mov|add|pop|ldm)', name) and
              re.search(r'(?:^|[\s{,])pc(?:[,}]|$)', operands))


_ENCODING = re.compile(r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{4}\s)+)')
instruction = {}
for line in open(dis_path, encoding='utf8', errors='replace'):
  match = _ENCODING.match(line)
  if not match:
    continue
  text = line.rstrip()
  if pathcost.is_data(text):
    continue
  address = int(line.split(':')[0], 16)
  size = 2 * len(match.group(1).split())
  kind, target = pathcost.classify(text)
  instruction[address] = (pathcost.instruction_cycles(text), size, kind, target,
                          computed_destination(text))


def measured(address):
  return not HARNESS[0] <= address < HARNESS[1]


class Block(object):
  """One QEMU translation block, priced as far as it can be on its own."""

  def __init__(self, addresses):
    self.addresses = addresses
    self.cycles = sum(instruction[a][0] for a in addresses if measured(a))
    self.last = addresses[-1]
    _, size, self.kind, self.target, self.computed = instruction[self.last]
    self.fallthrough = self.last + size
    self.measured_last = measured(self.last)


translations = {}
pending = None
blocks_cycles = []
tb_counts = {}
branch_taken, branch_block_max = {}, {}
violations, transitions = [], 0
inside = False
cycles = 0
block_tbs, block_branches = {}, {}
flag_counts = dict((name, []) for name in flag_names)
block_flags = {}
previous = None

_TRACE = re.compile(r'^Trace \d+: \S+ \[[0-9a-f]+/([0-9a-f]+)/')
_IN_ASM = re.compile(r'^0x([0-9a-f]+):')


def close_block():
  global block_tbs, block_branches, block_flags
  blocks_cycles.append(cycles)
  for name in flag_names:
    flag_counts[name].append(block_flags.get(name, 0))
  block_flags = {}
  for pc, count in block_tbs.items():
    tb_counts[pc] = tb_counts.get(pc, 0) + count
  for key, count in block_branches.items():
    branch_taken[key] = branch_taken.get(key, 0) + count
    branch_block_max[key] = max(branch_block_max.get(key, 0), count)
  block_tbs, block_branches = {}, {}


for line in sys.stdin:
  if line.startswith('0x'):
    match = _IN_ASM.match(line)
    pending.append(int(match.group(1), 16))
    continue
  if line.startswith('IN:'):
    pending = []
    continue
  if line.startswith('Trace'):
    if pending:
      start = pending[0]
      block = Block(pending)
      known = translations.get(start)
      if known is not None and known.addresses != block.addresses:
        violations.append('block 0x%x retranslated with a different extent' % start)
      translations[start] = block
      pending = None
    pc = int(_TRACE.match(line).group(1), 16)
    if previous is not None:
      transitions += 1
      fits = previous.computed or {
          'branch': pc in (previous.target, previous.fallthrough),
          'jump': pc == previous.target,
          'call': pc == previous.target,
          'fall': pc == previous.fallthrough,
          'return': True}[previous.kind]
      if not fits:
        violations.append('0x%x (%s) -> 0x%x' % (previous.last, previous.kind, pc))
      if inside and previous.measured_last and previous.kind == 'branch':
        taken = pc == previous.target and previous.target != previous.fallthrough
        if not taken:
          cycles -= REFUND
        key = '%x %s' % (previous.last, 'taken' if taken else 'fell')
        block_branches[key] = block_branches.get(key, 0) + 1
    if pc == BEGIN:
      inside, cycles = True, 0
    elif pc == END:
      if inside:
        close_block()
      inside = False
    block = translations[pc]
    if inside:
      cycles += block.cycles
      if measured(pc):
        block_tbs[pc] = block_tbs.get(pc, 0) + 1
      if pc in FLAGS:
        block_flags[FLAGS[pc]] = block_flags.get(FLAGS[pc], 0) + 1
    previous = block

json.dump({
    'image': pathcost.image_digest(dis_path),
    'blocks': blocks_cycles,
    'flags': flag_counts,
    'tb_counts': {'%x' % pc: n for pc, n in tb_counts.items()},
    'tb_extent': {'%x' % pc: ['%x' % a for a in translations[pc].addresses]
                  for pc in tb_counts},
    'branch_taken': branch_taken,
    'branch_block_max': branch_block_max,
    'transitions': transitions,
    'violations': violations[:50],
    'violation_count': len(violations),
}, sys.stdout)
