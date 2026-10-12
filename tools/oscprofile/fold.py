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
from measured import tools_digest

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
  """One QEMU translation block, priced as far as it can be on its own, and
  what each block seen after it means: (follows from its last instruction,
  the branch outcome to count or None, cycles to take back)."""

  def __init__(self, addresses):
    self.addresses = addresses
    self.cycles = sum(instruction[a][0] for a in addresses if measured(a))
    self.last = addresses[-1]
    _, size, self.kind, self.target, self.computed = instruction[self.last]
    self.fallthrough = self.last + size
    self.measured_start = measured(addresses[0])
    self.measured_last = measured(self.last)
    self.next = {}

  def successor(self, pc):
    if self.computed or self.kind == 'return':
      fits = True
    elif self.kind == 'branch':
      fits = pc == self.target or pc == self.fallthrough
    elif self.kind == 'fall':
      fits = pc == self.fallthrough
    else:
      fits = pc == self.target
    outcome, refund = None, 0
    if self.measured_last and self.kind == 'branch':
      taken = pc == self.target and self.target != self.fallthrough
      outcome = (self.last, taken)
      refund = 0 if taken else REFUND
    self.next[pc] = (fits, outcome, refund)
    return self.next[pc]


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


# Exec lines read `Trace 0: <host> [<cs_base>/<pc>/<flags>...] <symbol>`;
# in_asm lines `0x<address>:  <encoding>  <instruction>`.
for line in sys.stdin:
  first = line[0]
  if first == 'T':
    if pending:
      start = pending[0]
      block = Block(pending)
      known = translations.get(start)
      if known is not None and known.addresses != block.addresses:
        violations.append('block 0x%x retranslated with a different extent' % start)
      translations[start] = block
      pending = None
    pc = int(line.split('/', 2)[1], 16)
    if previous is not None:
      transitions += 1
      fits, outcome, refund = previous.next.get(pc) or previous.successor(pc)
      if not fits:
        violations.append('0x%x (%s) -> 0x%x' % (previous.last, previous.kind, pc))
      if inside and outcome is not None:
        cycles -= refund
        block_branches[outcome] = block_branches.get(outcome, 0) + 1
    if pc == BEGIN:
      inside, cycles = True, 0
    elif pc == END:
      if inside:
        close_block()
      inside = False
    block = translations[pc]
    if inside:
      cycles += block.cycles
      if block.measured_start:
        block_tbs[pc] = block_tbs.get(pc, 0) + 1
      if pc in FLAGS:
        block_flags[FLAGS[pc]] = block_flags.get(FLAGS[pc], 0) + 1
    previous = block
  elif first == '0' and line[1] == 'x':
    pending.append(int(line[2:line.index(':')], 16))
  elif line.startswith('IN:'):
    pending = []


def outcome_key(outcome):
  return '%x %s' % (outcome[0], 'taken' if outcome[1] else 'fell')


json.dump({
    'image': pathcost.image_digest(dis_path),
    'tools': tools_digest(),
    'blocks': blocks_cycles,
    'flags': flag_counts,
    'tb_counts': {'%x' % pc: n for pc, n in tb_counts.items()},
    'tb_extent': {'%x' % pc: ['%x' % a for a in translations[pc].addresses]
                  for pc in tb_counts},
    'branch_taken': dict((outcome_key(o), n) for o, n in branch_taken.items()),
    'branch_block_max': dict((outcome_key(o), n) for o, n in branch_block_max.items()),
    'transitions': transitions,
    'violations': violations[:50],
    'violation_count': len(violations),
}, sys.stdout)
