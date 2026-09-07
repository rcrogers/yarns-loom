# Per-sample cost of every oscillator shape, so the WORST CASE is visible.
#
# The audio budget is set by the most expensive shape running on every voice at
# once, not by the average, so a table of all of them is worth more than any one
# optimisation. Run after touching oscillator.cc. tools/cycles.py is the same
# idea for the envelope's render loop.
#
#   SKIP_PROGRAMMING=true ./env/mutable-env.sh \
#     /usr/local/arm-4.8.3/bin/arm-none-eabi-objdump -dl build/yarns/yarns.elf \
#     > /tmp/yarns.dis && python3 tools/osc_cycles.py /tmp/yarns.dis
#
# -dl, NOT -d: the edge split reads source lines. Without them the table says
# so and falls back to charging the edge body every sample.
#
# THE LOOP IS FOUND BY CFG, NOT BY ADDRESS SPAN, and that distinction is the
# whole reason this file exists. GCC lays a loop body out across several basic
# blocks joined by backward jumps, so "the instructions between a branch and its
# target" is sometimes only a FRAGMENT of the loop -- measuring RenderFilteredNoise
# that way reported 15 instructions for a body that really has 30-odd. This walks
# the natural loop of each back edge instead: the header, plus every block that
# can reach the back edge without passing through the header.
#
# EDGE LOOPS ARE REPORTED SEPARATELY. `while (true) { EDGES_SAW(...) }` breaks
# immediately on all but one sample per oscillator period, so charging its body
# per sample would overstate every band-limited shape. The per-sample column is
# the sample loop minus any loop nested inside it; that nested cost is "edge",
# paid once per period.
#
# THE LONGEST PATH, NOT THE SUM OF THE BLOCKS. This used to add up every
# instruction in the loop body, so a shape was charged for both arms of every
# if -- WHISTLE read 68.2% that way, and four voices of it plus the envelope
# read over 100% for a build that runs. It now walks the CFG with
# tools/pathcost.py, the same code cycles.py prices the envelope with, so the
# two tools finally share one cost model. `sum` is still reported beside it:
# the gap between them IS the branchiness, and it is worth seeing.
import os
import re
import sys

dis_path = sys.argv[1]

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pathcost

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def source_int(relative_path, pattern, cast):
  """Read a constant from the source rather than restate it here."""
  text = open(os.path.join(ROOT, relative_path), encoding='utf8').read()
  match = re.search(pattern, text)
  if not match:
    raise SystemExit('  %s: no match for %s' % (relative_path, pattern))
  return cast(*match.groups())

STORE = re.compile(r'\bstrh')

OSCILLATOR_CC = open(os.path.join(ROOT, 'yarns/oscillator.cc'),
                     encoding='utf8').read()

SHAPE_BODIES = dict(re.findall(
    r'void Oscillator::(\w+)\(int16_t\* \w+, int16_t\* \w+\) \{(.*?)\n\}',
    OSCILLATOR_CC, re.DOTALL))

# The shapes to price: the RenderFn entries fn_table_ can dispatch, and only
# those. A commented-out entry is unreachable, and a Render symbol the table
# never names is dead. Any of these the pass cannot price is an error below.
_table = re.search(r'Oscillator::fn_table_\[\] = \{(.*?)\n\};',
                   OSCILLATOR_CC, re.DOTALL)
if not _table:
  raise SystemExit('  cannot find fn_table_; there is no set to price')
REACHABLE = frozenset(re.findall(
    r'&Oscillator::(\w+)', re.sub(r'//[^\n]*', '', _table.group(1))))


def shape_name(symbol):
  """Itanium mangling: the length prefix delimits the name, so a name holding
  a capital E survives."""
  match = re.match(r'^_ZN5yarns10Oscillator(\d+)', symbol)
  if not match:
    return ''
  return symbol[match.end():match.end() + int(match.group(1))]

# THE EDGE WORK IS NOT PAID EVERY SAMPLE, and charging it as though it were is
# what made the band-limited shapes head this table.
#
# `while (true) { EDGES_SAW(...) }` breaks unless an edge fell in THIS sample.
# EDGES_SAW clears self_reset on its first pass, so the second always breaks and
# GCC unrolls the loop away entirely; EDGES_PULSE keeps one, because a rising
# and a falling edge can both land in one sample. Either way the work is bounded
# PER SAMPLE -- there is no per-period loop to report, which is why the `edge`
# column read 0 -- and it is taken only on samples an edge falls in.
#
# That rate is the pitch: at kHighestNote, MIDI 128 and 13.3 kHz, a saw crosses
# 0.295 edges a sample and a pulse 0.591. At middle C it is 0.006. So the block
# a budget is set by pays the sample body 64 times and the edge body ~38.
#
# TWO EDGES A PERIOD IS ASSUMED FOR EVERY SHAPE, which over-charges the saws by
# 2x. Deliberate: it is the pulse rate, it is the worst case, and counting call
# sites through an inliner to do better is not worth the fragility.
EDGE_SOURCE_LINES = (454, 486)   # EdgeTime through the end of EDGES_PULSE
BLEP_SOURCE_LINES = (331, 345)   # This/NextBlepSample, oscillator.h
HIGHEST_MIDI = source_int('yarns/oscillator.h', r'kHighestNote\s*=\s*(\d+) \* (\d+)',
                          lambda a, b: int(a) * int(b) / 128.0)
FRAME_HZ = source_int('yarns/drivers/dac.h', r'kFrameHz\s*=\s*(\d+)', int)
BLOCK_SAMPLES = 1 << source_int('yarns/drivers/dac.h',
                                r'kAudioBlockSizeBits\s*=\s*(\d+)', int)
EDGES_PER_PERIOD = 2
# THE SYNC MODULATOR RUNS FASTER THAN THE NOTE. Its increment is
# `phase_increment * timbre >> kSyncRatioFractionalBits`, so at full timbre it
# is a multiple of the master's, and everything guarded by ITS wrap is paid at
# that higher rate. Read the shift out of the source rather than restate it.
SYNC_RATIO_BITS = source_int('yarns/oscillator.cc',
                             r'kSyncRatioFractionalBits\s*=\s*(\d+)', int)
TIMBRE_MAX = 32767
MAX_SYNC_RATIO = TIMBRE_MAX / float(1 << SYNC_RATIO_BITS)
# The line PhaseWrapped is defined on: every guard inlines to it, which is how a
# region that is paid once a PERIOD is told from one paid once a SAMPLE.
# THE WHOLE FUNCTION'S SPAN, not the line of its `return`: GCC attributes an
# inlined body to the SIGNATURE line, so anchoring on the statement finds
# nothing and every region silently comes back empty.
def _span(source_text, opener):
  lines = source_text.split('\n')
  for number, line in enumerate(lines, 1):
    if opener in line:
      for end in range(number, len(lines) + 1):
        if lines[end - 1].startswith('}'):
          return number, end
  raise SystemExit('  cannot find %r; regions cannot be found without it' % opener)


WRAP_GUARD_LINES = _span(OSCILLATOR_CC, 'static inline bool PhaseWrapped(')
# FractionU32 is reached only from SYNC's master-reset arm, so its cycles are
# wrap-guarded even though they carry a dsp.h line rather than an oscillator one.
FRACTION_SOURCE_LINES = _span(
    open(os.path.join(ROOT, 'stmlib/dsp/dsp.h'), encoding='utf8').read(),
    'inline uint32_t FractionU32(')
# BOTH ENDS, ALWAYS. The edge rate is the pitch, so one column is half an
# answer: the top of the keyboard is the budget and middle C is what the
# instrument mostly does, and a band-limited shape is a different animal at
# each. Reported side by side so neither can be quoted alone.
MIDDLE_C_MIDI = 60


def edges_per_sample(midi):
  return 440.0 * 2 ** ((midi - 69) / 12.0) / FRAME_HZ * EDGES_PER_PERIOD


EDGES_PER_SAMPLE = edges_per_sample(HIGHEST_MIDI)
functions = pathcost.parse(dis_path)
call_cost, _ = pathcost.call_cost_function(functions)

# Source line per address, from `objdump -dl`. Without it the edge split cannot
# be made and the table falls back to charging the edge body every sample, which
# is the old behaviour -- so say so rather than report it as though it were the
# block cost.
line_of, _current = {}, None
for _row in open(dis_path, encoding='utf8', errors='replace'):
  _m = re.match(r'^(/?\S*?([\w.]+\.(?:cc|h))):(\d+)', _row.strip())
  if _m:
    _current = (_m.group(2), int(_m.group(3)))
    continue
  _m = re.match(r'^\s*([0-9a-f]+):\t', _row)
  if _m:
    line_of[int(_m.group(1), 16)] = _current
HAVE_LINES = any(line_of.values())


def reachable_from(graph, start, restrict, cut):
  seen, stack = set(), [start]
  while stack:
    leader = stack.pop()
    if leader in seen:
      continue
    seen.add(leader)
    for successor in graph.edges[leader]:
      if successor in restrict and (leader, successor) not in cut:
        stack.append(successor)
  return seen


def wrap_guarded_regions(graph, restrict):
  """The blocks each PhaseWrapped guard controls -- paid once a WRAP, not once
  a sample.

  An `if (c) { body }` leaves the body reachable down ONE side of the branch
  only, so the body is the set difference between what the two successors
  reach. An if/else makes both differences non-empty and is not classified
  here rather than guessed at.
  """
  cut = set(graph.back_edges())
  regions = []
  for leader in restrict:
    where = [line_of.get(a) for a, _ in graph.blocks[leader]]
    if not any(w and w[0] == 'oscillator.cc'
               and WRAP_GUARD_LINES[0] <= w[1] <= WRAP_GUARD_LINES[1]
               for w in where):
      continue
    address, text = graph.blocks[leader][-1]
    kind, target = pathcost.classify(text)
    successors = [s for s in graph.edges[leader] if s in restrict]
    if kind != 'branch' or len(successors) != 2:
      continue
    other = [s for s in successors if s != target]
    if not other:
      continue
    taken = reachable_from(graph, target, restrict, cut)
    fell = reachable_from(graph, other[0], restrict, cut)
    for body in (fell - taken, taken - fell):
      if body and not (fell - taken and taken - fell):
        regions.append(body)
  return regions


def is_edge_address(address):
  """Does this instruction belong to the BLEP edge machinery?"""
  where = line_of.get(address)
  if not where:
    return False
  name, line = where
  if name == 'oscillator.cc':
    return EDGE_SOURCE_LINES[0] <= line <= EDGE_SOURCE_LINES[1]
  if name == 'oscillator.h':
    return BLEP_SOURCE_LINES[0] <= line <= BLEP_SOURCE_LINES[1]
  if name == 'dsp.h':
    return FRACTION_SOURCE_LINES[0] <= line <= FRACTION_SOURCE_LINES[1]
  if name == 'oscillator.cc':
    return WRAP_GUARD_LINES[0] <= line <= WRAP_GUARD_LINES[1]
  return False

rows = []
unexplained_shapes = []
for name, body in functions.items():
    short = shape_name(name)
    if short not in REACHABLE or not body:
        continue
    graph = pathcost.Graph(body)
    def stores(leaders):
        return any(STORE.search(text)
                   for leader in leaders for _, text in graph.blocks[leader])
    sample = []
    for source, target in graph.back_edges():
        blocks = graph.loop_body(source, target)
        if stores(blocks):
            sample.append((source, target, blocks))
    if not sample:
        continue
    # THE WORST OF THEM, PRICED, not the one with the most blocks. RenderTransfer
    # has FOUR sample loops -- the transfer-function switch is hoisted out, so
    # each arm gets its own -- and picking by block count picked an arm at
    # random. A shape is as expensive as its dearest path through a sample.
    def price(entry):
      return pathcost.longest_path(
          graph, call_cost, entry=entry[1], restrict=entry[2])
    source, header, blocks = max(sample, key=price)
    # A loop nested inside the sample loop that does NOT itself store is an
    # edge loop: paid once per oscillator period, not per sample.
    nested = set()
    for other_source, other_target in graph.back_edges():
        other = graph.loop_body(other_source, other_target)
        if other < blocks and not stores(other):
            nested |= other
    per_sample = blocks - nested
    cycles = pathcost.longest_path(
        graph, call_cost, entry=header, restrict=per_sample)
    # A MODULATED SHAPE WRAPS FASTER THAN ITS NOTE, so everything a wrap guards
    # is paid at the modulator's rate, not the master's.
    ratio = (MAX_SYNC_RATIO if 'RENDER_MODULATED' in SHAPE_BODIES.get(short, '')
             else 1.0)
    regions = wrap_guarded_regions(graph, per_sample)
    region_zero = [(a, a, 0) for body in regions for leader in body
                   for a, _ in graph.blocks[leader]]
    # One (address, address, 0) per instruction, not a span: a span would sweep
    # up whatever GCC laid between the edge blocks and zero it too.
    edge_zero = [(a, a, 0) for leader in per_sample
                 for a, _ in graph.blocks[leader] if is_edge_address(a)]

    # THE COMMON SAMPLE AND THE DEAR ONE. Everything expensive in these loops
    # is guarded by a phase wrap -- the BLEP, the sync reset, the edge -- so the
    # cheap path is the sample where nothing wrapped, and the gap between the
    # paths is the whole of the rare work. That needs no region to be located,
    # which matters: the guards do not survive inlining in a findable form.
    base = pathcost.shortest_path(
        graph, call_cost, entry=header, restrict=per_sample)
    rare_cycles = cycles - base

    def at(midi):
      # Charged at the rate the FASTEST accumulator wraps, which for a sync
      # shape is the modulator's. Conservative: it over-charges a region the
      # slower master guards, and a budget should err that way.
      return base + min(edges_per_sample(midi) * ratio, 1.0) * rare_cycles

    effective, effective_c4 = at(HIGHEST_MIDI), at(MIDDLE_C_MIDI)
    total = sum(pathcost.instruction_cycles(text)
                for leader in per_sample for _, text in graph.blocks[leader])
    edge = sum(pathcost.instruction_cycles(text)
               for leader in nested for _, text in graph.blocks[leader])
    instructions = sum(len(graph.blocks[leader]) for leader in per_sample)
    spills = sum(1 for leader in per_sample for _, text in graph.blocks[leader]
                 if re.search(r'(ldr|str)\w*\s+\S+,\s*\[sp', text))
    branches = sum(1 for leader in per_sample
                   for _, text in graph.blocks[leader]
                   if re.match(r'^b(?!l)', pathcost.mnemonic(text)))
    # HOW MUCH OF THE RARE WORK THIS CANNOT ACCOUNT FOR, which is a BOUND on the
    # model's uncertainty and not a failure.
    #
    # The model charges the whole longest/shortest gap at the wrap rate, on the
    # grounds that everything costly in these loops sits behind a phase wrap.
    # Zeroing the machinery that is identifiable BY SOURCE -- EdgeTime, the
    # EDGES macros, the BLEP helpers, FractionU32, PhaseWrapped -- leaves a
    # residue, and the residue is mostly the SHAPE'S OWN code inside those same
    # guarded regions: SYNC's discontinuity, EDGES_PULSE's high_ handling. That
    # carries the shape's line, not an edge one, so no line-based test can tell
    # it from steady-path code -- and identifying the region from its guard does
    # not survive inlining, which was tried.
    #
    # So this is reported, not asserted. Read it as: if ALL of this residue were
    # really unconditional, the shape's cheap path would be understated by that
    # much. It is an upper bound on the error, and for the SYNC shapes it is
    # about half the rare work.
    unexplained = (
        pathcost.longest_path(graph, call_cost, weights=edge_zero,
                              entry=header, restrict=per_sample)
        - pathcost.shortest_path(graph, call_cost, weights=edge_zero,
                                 entry=header, restrict=per_sample))
    if unexplained:
      unexplained_shapes.append((short, unexplained, rare_cycles))
    rows.append((cycles, base, effective_c4, effective, spills, branches, short))

unpriced = REACHABLE - set(row[-1] for row in rows)
if unpriced:
  raise SystemExit('  no sample loop found for %s -- the table cannot rank '
                   'shapes it does not hold' % ', '.join(sorted(unpriced)))

if unexplained_shapes and '--metrics' not in sys.argv[2:]:
  print('  RARE WORK THIS CANNOT ATTRIBUTE TO EDGE OR WRAP MACHINERY BY SOURCE.')
  print('  Mostly shape code inside a guarded region, which carries the shape\'s')
  print('  own line -- an UPPER BOUND on how much of the cheap path is understated,')
  print('  not a defect list. Shown worst first.')
  for short, gap, rare in sorted(unexplained_shapes, key=lambda r: -r[1]):
    print('    %-28s %3d of %3d rare cycles' % (short, gap, rare))
# By the ceiling: the number a real-time budget is set by.
rows.sort(reverse=True)
if '--metrics' in sys.argv[2:]:
  # For tools/block_budget.py: one line a shape, no formatting to parse around.
  for cycles, base, effective_c4, effective, spills, branches, short in rows:
    print('%s %.4f %.4f' % (short, effective, effective_c4))
  raise SystemExit(0)
if not HAVE_LINES:
  print('  NO LINE INFO in this disassembly (use objdump -dl): the edge body is')
  print('  charged to every sample, which OVERSTATES every band-limited shape.')
print('  %-28s %6s %6s %6s %6s %8s %8s'
      % ('shape', 'floor', 'ceil', 'C4', 'MIDI %d' % HIGHEST_MIDI,
         'spills', 'branches'))
for cycles, base, effective_c4, effective, spills, branches, short in rows:
    print('  %-28s %6d %6d %6.0f %6.0f %8d %8d'
          % (short[:28], base, cycles, effective_c4, effective,
             spills, branches))
print('  ---')
print('  Cycles a sample. floor = a sample where nothing wrapped; ceil = the')
print('  dearest way through one sample. The right two charge the')
print('  gap between them at the rate a wrap falls at that pitch: %.3f a sample'
      % edges_per_sample(HIGHEST_MIDI))
print('  at MIDI %d and %.3f at middle C, times the modulator ratio (up to %.0fx)'
      % (HIGHEST_MIDI, edges_per_sample(MIDDLE_C_MIDI), MAX_SYNC_RATIO))
print('  on a modulated shape, capped at one. That over-charges a region the')
print('  slower master guards, which is the direction a budget should err.')
print('  Assumes the gap is all wrap-guarded: a shape with a genuinely even')
print('  branch has its floor under-counted, and none here has one.')
print('  A Cortex-M3 timing table, so an estimate. Good for DELTAS.')
