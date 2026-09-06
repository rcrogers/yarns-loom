# Worst-case cycle cost of a code path, from a disassembly.
#
# tools/cycles.py counts the render loop, which is the per-SAMPLE cost. It
# cannot see the per-RUN path -- the block of work RenderStage does once before
# the loop and once after it -- and that path holds the walk, the mean clamp,
# and several helper calls. It had never been measured, only reduced.
#
# WHAT THIS COMPUTES: the longest path through a function's control flow graph,
# in the same ESTIMATED Cortex-M3 cycles tools/cycles.py counts. Longest, not
# average, because this is a realtime system: the worst case is the only case
# that has to fit. Back edges are cut, so each loop body is counted once; a
# caller that knows a trip count applies it by WEIGHTING the loop's blocks.
# A call costs a branch plus the callee's own longest path.
#
# WHAT IT DOES NOT DO: it does not know which way a data-dependent branch goes,
# so the path it reports need not be reachable with any single input. It is an
# upper bound, and it is for deltas and for sizing.
import re

# Cortex-M3 timing, matching tools/cycles.py so the two are comparable, plus
# the entries a straight-line path needs that a loop body never had:
#   udiv/sdiv   2-12 cycles depending on the operands; the worst case governs.
#   push/pop    1 cycle plus 1 per register.
DIV_CYCLES = 12
BRANCH_CYCLES = 3
# A CONDITIONAL BRANCH THE PATH DOES NOT TAKE IS A FALL-THROUGH, and on
# Cortex-M3 that is the cheap direction: the pipeline is not refilled. Charging
# every branch alike made a branchy shape dearer than it is -- SYNC PULSE has 24
# of them -- and, worse, made BRANCH LAYOUT unmeasurable, so any __builtin_expect
# work would have reported as exactly zero.
NOT_TAKEN_BRANCH_CYCLES = 1
LONG_MULTIPLY_CYCLES = 4
MEMORY_CYCLES = 2

_MNEMONIC = re.compile(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{4}\s+)+(\S+)')
_ADDRESS = re.compile(r'\s*([0-9a-f]+):\s')
_LABEL = re.compile(r'^([0-9a-f]+) <(.+)>:')
# `bne.w 8002310 <Foo+0x12>` and `b.n 8001fbe <Foo+0x22a>`: the operand's bare
# hex is the target. Registers never match, being at least four hex digits.
_TARGET = re.compile(r'\b([0-9a-f]{4,})\b')
_CONDITION = r'(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)'
_CONDITIONAL_BRANCH = re.compile(r'^b%s(?:\.[nw])?$' % _CONDITION)
_UNCONDITIONAL_BRANCH = re.compile(r'^b(?:\.[nw])?$')


def parse(dis_path):
  """disassembly -> {function name: [(address, text), ...]}, in file order."""
  functions, current = {}, None
  for line in open(dis_path, encoding='utf8', errors='replace'):
    label = _LABEL.match(line)
    if label:
      current = label.group(2)
      functions[current] = []
      continue
    address = _ADDRESS.match(line)
    if current is not None and address:
      functions[current].append((int(address.group(1), 16), line.rstrip()))
  return {name: body for name, body in functions.items() if body}


def mnemonic(text):
  match = _MNEMONIC.match(text)
  return match.group(1) if match else ''


def is_data(text):
  """A .byte/.short/.word line: a jump table or a literal pool, not code."""
  return mnemonic(text).startswith('.')


def _data_bytes(text):
  """The little-endian bytes a .byte/.short/.word line stands for."""
  match = re.search(r'\.(byte|short|word)\s+0x([0-9a-f]+)', text)
  if not match:
    return []
  width = {'byte': 1, 'short': 2, 'word': 4}[match.group(1)]
  value = int(match.group(2), 16)
  return [(value >> (8 * i)) & 0xFF for i in range(width)]


def table_branch_targets(instructions, index):
  """Targets of a `tbb`/`tbh [pc, ...]` jump table at `index`, or None.

  The table follows the instruction and objdump renders it as data, so the
  bytes come from the .byte/.short/.word lines after it. How MANY entries is
  not in the table -- it is in the bounds check GCC emits just before, a
  `cmp rN, #k` whose k is the last valid index.
  """
  address, text = instructions[index]
  name = mnemonic(text)
  if name not in ('tbb', 'tbh') or '[pc' not in text:
    return None
  count = None
  for previous in range(index - 1, max(index - 4, -1), -1):
    match = re.search(r'\bcmp(?:\.[nw])?\s+\w+,\s*#(\d+)',
                      instructions[previous][1])
    if match:
      count = int(match.group(1)) + 1
      break
  if count is None:
    return None
  base = address + 4
  raw = []
  for following in range(index + 1, len(instructions)):
    if not is_data(instructions[following][1]):
      break
    raw += _data_bytes(instructions[following][1])
  width = 1 if name == 'tbb' else 2
  targets = []
  for entry in range(count):
    chunk = raw[entry * width:(entry + 1) * width]
    if len(chunk) < width:
      break
    offset = chunk[0] if width == 1 else chunk[0] | (chunk[1] << 8)
    targets.append(base + 2 * offset)
  return targets


def instruction_cycles(text):
  name = mnemonic(text)
  if name.startswith('.'):
    return 0  # data, not code
  if name in ('tbb', 'tbh'):
    return BRANCH_CYCLES + 2  # table read, then a taken branch
  if re.match(r'^(smull|umull|smlal|umlal)', name):
    return LONG_MULTIPLY_CYCLES
  if re.match(r'^(udiv|sdiv)', name):
    return DIV_CYCLES
  if re.match(r'^(push|pop|stmdb|ldmia|stmia|ldmdb)', name):
    return 1 + len(re.findall(r'\b(?:r[0-9]+|sl|fp|ip|sp|lr|pc)\b',
                              text.split('{')[-1])) if '{' in text else 1
  if re.match(r'^(ldr|str)', name):
    return MEMORY_CYCLES
  if re.match(r'^it[te]*$', name):
    return 0  # folded into the instructions it guards
  if name.startswith('b'):
    return BRANCH_CYCLES
  return 1


def _operand_target(text):
  operand = text.split('\t')[-1]
  match = _TARGET.search(operand)
  return int(match.group(1), 16) if match else None


def classify(text):
  """-> ('fall' | 'jump' | 'branch' | 'call' | 'return', target or None)."""
  name = mnemonic(text)
  if re.match(r'^(bl|blx)(?:\.[nw])?$', name):
    return 'call', _operand_target(text)
  if re.match(r'^cbn?z$', name):
    return 'branch', _operand_target(text)
  if name.startswith('bx'):
    return 'return', None
  if re.match(r'^(pop|ldmia)', name) and 'pc}' in text.replace(' ', ''):
    return 'return', None
  if _CONDITIONAL_BRANCH.match(name):
    return 'branch', _operand_target(text)
  if _UNCONDITIONAL_BRANCH.match(name):
    return 'jump', _operand_target(text)
  return 'fall', None


class Graph(object):
  """One function's basic blocks, keyed by the address each starts at."""

  def __init__(self, instructions):
    self.instructions = instructions
    self.first, self.last = instructions[0][0], instructions[-1][0]
    self.entry = self.first
    self._build()

  def _inside(self, address):
    return address is not None and self.first <= address <= self.last

  def _build(self):
    # Jump tables, resolved once: an unresolved `tbb` looks like a fallthrough
    # into its own table data, which severed 30 of NoteOn's 35 blocks from the
    # entry and priced the function at a tenth of its cost.
    self.tables = {}
    for index in range(len(self.instructions)):
      targets = table_branch_targets(self.instructions, index)
      if targets:
        self.tables[self.instructions[index][0]] = targets
    leaders = {self.entry}
    for index, (address, text) in enumerate(self.instructions):
      kind, target = classify(text)
      if address in self.tables:
        leaders.update(t for t in self.tables[address] if self._inside(t))
      elif kind in ('branch', 'jump') and self._inside(target):
        leaders.add(target)
      if (address in self.tables or kind in ('branch', 'jump', 'return')) and \
          index + 1 < len(self.instructions):
        leaders.add(self.instructions[index + 1][0])
    self.blocks, self.edges, self.calls = {}, {}, {}
    current = None
    for index, (address, text) in enumerate(self.instructions):
      if address in leaders:
        current = address
        self.blocks[current], self.edges[current], self.calls[current] = \
            [], [], []
      self.blocks[current].append((address, text))
      kind, target = classify(text)
      following = self.instructions[index + 1][0] \
          if index + 1 < len(self.instructions) else None
      if address in self.tables:
        self.edges[current] += [t for t in self.tables[address]
                                if self._inside(t)]
      elif kind == 'call':
        self.calls[current].append(target)
      elif kind == 'branch':
        if self._inside(target):
          self.edges[current].append(target)
        if following is not None:
          self.edges[current].append(following)
      elif kind == 'jump':
        if self._inside(target):
          self.edges[current].append(target)
        else:
          # A tail call: the callee's cost, then this function returns.
          self.calls[current].append(target)
      elif kind == 'fall' and following is not None and following in leaders:
        self.edges[current].append(following)

  def back_edges(self):
    """Edges that close a CYCLE, found by depth-first search.

    NOT "the target sits at a lower address": GCC moves cold blocks past the
    function's return, so plenty of backward-by-address edges are ordinary
    control flow rejoining the main path. Treating those as loops cut the
    graph and under-reported NoteOn by 10x.
    """
    found, on_stack, seen = set(), [], set()
    stack = [(self.entry, iter(self.edges[self.entry]))]
    on_stack.append(self.entry)
    seen.add(self.entry)
    while stack:
      leader, successors = stack[-1]
      advanced = False
      for successor in successors:
        if successor in on_stack:
          found.add((leader, successor))
          continue
        if successor in seen:
          continue
        seen.add(successor)
        on_stack.append(successor)
        stack.append((successor, iter(self.edges[successor])))
        advanced = True
        break
      if not advanced:
        stack.pop()
        on_stack.pop()
    return sorted(found)

  def loop_body(self, source, target):
    """The BLOCKS a back edge (source -> target) encloses: the natural loop.

    Walk predecessors back from the latch, seeded with the header, so the walk
    stops at the header and never leaves through the loop exit.

    NOT the address span from target to source. GCC scatters a loop's blocks,
    so that span sweeps up unrelated code which merely landed between them --
    and a caller weighting the span then charges that code the loop's trip
    count, or with factor 0 charges it nothing. That is how a `bl` in the
    per-run setup once priced at zero: it had been moved inside the address
    range of a head/tail loop it has nothing to do with.
    """
    predecessors = {}
    for leader in self.blocks:
      for successor in self.edges[leader]:
        predecessors.setdefault(successor, []).append(leader)
    body, stack = set([target]), [source]
    while stack:
      leader = stack.pop()
      if leader in body:
        continue
      body.add(leader)
      stack.extend(predecessors.get(leader, ()))
    return body

  def block_weights(self, leaders, factor):
    """One weight range per block, so a scattered loop weights only itself."""
    return [(self.blocks[leader][0][0], self.blocks[leader][-1][0], factor)
            for leader in leaders]

  def loop_region(self, containing=None):
    """The tightest loop as (low address, high address).

    A loop's extent is its back edge's target through its source, which for
    the loops here (all from structured C or hand-written asm) is contiguous.
    With `containing`, the tightest loop that strictly contains that range.
    """
    best = None
    for source, target in self.back_edges():
      low, high = min(source, target), max(source, target)
      # The source LEADER starts the latch block; the loop runs to its end.
      high = max(address for address, _ in self.blocks[source])
      if containing and not (low <= containing[0] and containing[1] <= high
                             and (low, high) != containing):
        continue
      if best is None or high - low < best[1] - best[0]:
        best = (low, high)
    return best


def shortest_path(graph, call_cost, weights=(), entry=None, restrict=None):
  """The CHEAPEST way through, which for a sample loop is the common sample.

  The dear path through an oscillator's loop is the one where a phase wrapped:
  a BLEP is laid down, a reset is computed, an edge is taken. None of that
  happens on most samples. Longest-path alone therefore prices every sample as
  though every rare thing happened at once -- and a budget wants the BLOCK,
  which is mostly cheap samples. The gap between the two paths IS the rare work,
  and the caller weights it by how often it actually happens.
  """
  cut = set(graph.back_edges())
  memo = {}

  def factor_at(address):
    result = 1
    for low, high, factor in weights:
      if low <= address <= high:
        result = factor
    return result

  def cost_of(leader):
    if leader in memo:
      return memo[leader]
    memo[leader] = 0
    own = sum(factor_at(address) * instruction_cycles(text)
              for address, text in graph.blocks[leader])
    own += sum(factor_at(graph.blocks[leader][-1][0]) *
               (BRANCH_CYCLES + call_cost(target))
               for target in graph.calls[leader])
    best, taken = None, None
    for successor in graph.edges[leader]:
      if (leader, successor) in cut:
        continue
      if restrict is not None and successor not in restrict:
        continue
      value = cost_of(successor) + _branch_adjustment(
          graph, leader, successor, factor_at)
      if best is None or value < best:
        best, taken = value, successor
    memo[leader] = own + (best if best is not None else 0)
    return memo[leader]

  return cost_of(graph.entry if entry is None else entry)


def _branch_adjustment(graph, leader, chosen, factor_at):
  """Refund the taken-branch penalty when the path falls through instead.

  `instruction_cycles` prices every branch as taken, because on its own it
  cannot know which way a path went. Here we do: the block's terminator is a
  conditional branch and the successor in question is not its target.

  APPLIED PER CANDIDATE, INSIDE THE COMPARISON, not to the winner afterwards.
  Adjusting after the pick means the walk chooses on unadjusted cost and is then
  handed a refund that depends on the choice -- so the LONGEST path could come
  out cheaper than the shortest, which it did, by a cycle, on DIRAC COMB.
  """
  block = graph.blocks[leader]
  if chosen is None or not block:
    return 0
  address, text = block[-1]
  kind, target = classify(text)
  if kind != 'branch' or chosen == target:
    return 0
  return -factor_at(address) * (BRANCH_CYCLES - NOT_TAKEN_BRANCH_CYCLES)


def longest_path(graph, call_cost, weights=(), entry=None, restrict=None):
  """Longest path from the entry with back edges cut, in cycles.

  `weights` is a list of (low, high, factor) address ranges: instructions in
  the range are charged factor times, which is how a caller applies a trip
  count the CFG cannot supply. Later entries win, so a nested loop's range can
  follow the loop that contains it. Factor 0 excludes a range entirely.

  `entry` starts the walk somewhere other than the function's own entry, and
  `restrict` limits it to a set of block leaders. Together they price ONE
  LOOP BODY -- the walk stops at the loop's exits instead of running on into
  the rest of the function.
  """
  def factor_at(address):
    result = 1
    for low, high, factor in weights:
      if low <= address <= high:
        result = factor
    return result

  # Cut the cycles FIRST, then walk the acyclic remainder. Cutting them
  # opportunistically during the walk, as an earlier version did, poisons the
  # memo: whichever path reaches a block first fixes its value, and if that
  # path came round a loop the value is the truncated one.
  cut = set(graph.back_edges())
  memo = {}

  def cost_of(leader):
    if leader in memo:
      return memo[leader]
    memo[leader] = 0  # guards against an unreachable-by-DFS residual cycle
    own = sum(factor_at(address) * instruction_cycles(text)
              for address, text in graph.blocks[leader])
    own += sum(factor_at(graph.blocks[leader][-1][0]) *
               (BRANCH_CYCLES + call_cost(target))
               for target in graph.calls[leader])
    best, taken = 0, None
    for successor in graph.edges[leader]:
      if (leader, successor) in cut:
        continue
      if restrict is not None and successor not in restrict:
        continue
      value = cost_of(successor) + _branch_adjustment(
          graph, leader, successor, factor_at)
      if taken is None or value > best:
        best, taken = value, successor
    memo[leader] = own + (best if taken is not None else 0)
    return memo[leader]

  return cost_of(graph.entry if entry is None else entry)


def longest_path_breakdown(graph, call_cost, by_address, weights=()):
  """WHERE the longest path's cycles go, as (label, cycles) pairs.

  `longest_path` answers how much; a number that large is not actionable
  without knowing which callee owns it. Same walk, same cuts, same weights --
  it re-derives the chosen successors and then tallies the blocks on that one
  path, so the total agrees with `longest_path` by construction.
  """
  def factor_at(address):
    result = 1
    for low, high, factor in weights:
      if low <= address <= high:
        result = factor
    return result

  cut = set(graph.back_edges())
  memo, chosen = {}, {}

  def cost_of(leader):
    if leader in memo:
      return memo[leader]
    memo[leader] = 0
    own = sum(factor_at(address) * instruction_cycles(text)
              for address, text in graph.blocks[leader])
    own += sum(factor_at(graph.blocks[leader][-1][0]) *
               (BRANCH_CYCLES + call_cost(target))
               for target in graph.calls[leader])
    best, pick = 0, None
    for successor in graph.edges[leader]:
      if (leader, successor) in cut:
        continue
      value = cost_of(successor) + _branch_adjustment(
          graph, leader, successor, factor_at)
      if pick is None or value > best:
        best, pick = value, successor
    chosen[leader] = pick
    memo[leader] = own + (best if pick is not None else 0)
    return memo[leader]

  cost_of(graph.entry)
  tally, leader = {}, graph.entry
  while leader is not None:
    inline = sum(factor_at(address) * instruction_cycles(text)
                 for address, text in graph.blocks[leader])
    inline += _branch_adjustment(graph, leader, chosen.get(leader), factor_at)
    tally['(inline)'] = tally.get('(inline)', 0) + inline
    for target in graph.calls[leader]:
      name = by_address.get(target, hex(target))
      charge = factor_at(graph.blocks[leader][-1][0]) * (
          BRANCH_CYCLES + call_cost(target))
      tally[name] = tally.get(name, 0) + charge
    leader = chosen.get(leader)
  return sorted(tally.items(), key=lambda pair: -pair[1])


def call_cost_function(functions, boundary=()):
  """-> (cost(address), {address: name}); `boundary` names cost nothing.

  A callee already on the stack costs nothing: RenderStage tail-calls its way
  back into itself for the next run, and ONE run is what is being priced.
  """
  by_address = {body[0][0]: name for name, body in functions.items()}
  memo, visiting = {}, set()

  def cost(target):
    name = by_address.get(target)
    if name is None or name in boundary:
      return 0
    if name in memo:
      return memo[name]
    if name in visiting:
      return 0
    visiting.add(name)
    value = longest_path(Graph(functions[name]), cost)
    visiting.discard(name)
    memo[name] = value
    return value

  return cost, by_address
