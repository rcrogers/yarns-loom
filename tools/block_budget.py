# EVERYTHING THAT RUNS IN ONE AUDIO BLOCK, for the layout that runs the most.
#
# cycles.py prices the envelope; osc_cycles.py prices a shape's sample loop.
# Neither sees what surrounds them -- the DAC packing, the mix-buffer fill, the
# Q15->Q16 shift, the per-voice dispatch -- and a budget that omits those is not
# a budget. This adds them up, so what is left out is left out ON PURPOSE and
# says so at the bottom.
#
#   objdump -dl build/yarns/yarns.elf > /tmp/y.dis   # -dl: loops are found by line
#   python3 tools/block_budget.py /tmp/y.dis
#
# HOW IT AVOIDS THE TRAP THAT BROKE ITS FIRST DRAFT. That draft weighted EVERY
# back edge in a function by the block size, which multiplies nested loops
# together and reported 800% of the CPU. A loop's trip count is a property of
# that loop, so each one is named here, located by the SOURCE FILE its body
# comes from, and given its own count -- and a name that matches no loop, or
# more than one, is a hard error rather than a silent zero.
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pathcost

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
dis_path = sys.argv[1]


def source(relative_path):
  return open(os.path.join(ROOT, relative_path), encoding='utf8').read()


def constant(relative_path, pattern):
  return int(re.search(pattern, source(relative_path)).group(1))


BLOCK_SAMPLES = 1 << constant('yarns/drivers/dac.h', r'kAudioBlockSizeBits\s*=\s*(\d+)')
FRAME_HZ = constant('yarns/drivers/dac.h', r'kFrameHz\s*=\s*(\d+)')
CPU_HZ = 72e6
PARA = constant('yarns/part.h', r'kNumParaphonicVoices\s*=\s*(\d+)')
BUDGET = CPU_HZ * BLOCK_SAMPLES / FRAME_HZ

# THE LAYOUT, folded from the map in multi.h rather than assumed -- the same map
# MaxLayoutEnvelopes and MaxLayoutAudioVoices static-assert against.
VOICE_COUNTS = {'VOICES_NONE': 0, 'VOICES_MONO': 1, 'VOICES_PARA': PARA}
layouts = {}
for name, body in re.findall(
    r'#define YARNS_CV_MAP_(LAYOUT_\w+)\(ROW\)\s*\\\n(.*?)(?=\n#define|\n\n)',
    source('yarns/multi.h'), re.DOTALL):
  rows = re.findall(r'ROW\(\s*(\w+),\s*[^,]+,\s*\w+,\s*(\w+)\)', body)
  if not rows:
    continue
  audio = [VOICE_COUNTS[a] for _, a in rows if VOICE_COUNTS[a]]
  envelopes = 2 * sum(audio) + sum(
      1 for role, a in rows if not VOICE_COUNTS[a]
      and role in ('DC_AUX_1', 'DC_AUX_2'))
  layouts[name] = (audio, envelopes)
WORST = max(layouts, key=lambda k: layouts[k][1])
AUDIO_VOICES_PER_OUTPUT, ENVELOPES = layouts[WORST]
AUDIO_OUTPUTS = len(AUDIO_VOICES_PER_OUTPUT)
ENVELOPE_OUTPUTS = ENVELOPES - 2 * sum(AUDIO_VOICES_PER_OUTPUT)
OUTPUTS_BUFFERED = AUDIO_OUTPUTS + ENVELOPE_OUTPUTS

functions = pathcost.parse(dis_path)
call_cost, _ = pathcost.call_cost_function(functions)

line_of, current = {}, None
for row in open(dis_path, encoding='utf8', errors='replace'):
  match = re.match(r'^(/?\S*?([\w.]+\.(?:cc|h))):(\d+)', row.strip())
  if match:
    current = (match.group(2), int(match.group(3)))
    continue
  match = re.match(r'^\s*([0-9a-f]+):\t', row)
  if match:
    line_of[int(match.group(1), 16)] = current
if not any(line_of.values()):
  sys.exit('  no line info: disassemble with objdump -dl, not -d')


def function(fragment):
  hits = [n for n in functions if fragment in n]
  if len(hits) != 1:
    sys.exit('  %s matches %d symbols, expected 1' % (fragment, len(hits)))
  return pathcost.Graph(functions[hits[0]])


def source_line(relative_path, needle):
  """The 1-based line a distinctive statement sits on, so a loop can be found
  by WHAT IT IS rather than by which file it came from. Two loops in one
  function share a file; they do not share a statement."""
  for number, text in enumerate(source(relative_path).split('\n'), 1):
    if needle in text:
      return number
  sys.exit('  %s: no line containing %r' % (relative_path, needle))


def loop_in(graph, fragment, from_file, at_line=None):
  """The one loop in `graph` whose body comes from `from_file` (and `at_line`)."""
  found = []
  for source_leader, target in graph.back_edges():
    body = graph.loop_body(source_leader, target)
    where = {line_of.get(a) for l in body for a, _ in graph.blocks[l]}
    files = {w[0] for w in where if w}
    lines = {w[1] for w in where if w}
    if from_file in files and (at_line is None or at_line in lines):
      found.append(body)
  if len(found) != 1:
    sys.exit('  %s: %d loops from %s, expected 1 -- the code moved, fix this'
             % (fragment, len(found), from_file))
  return found[0]


def header_of(graph, body):
  """The loop's ENTRY, which is the back edge's target -- not its lowest address.

  GCC rotates loops, so the latch can sit BELOW the header; taking the lowest
  address then prices from the latch, the back edge is cut, and the walk stops
  after one block. That read as 3 cycles for a loop that is thirteen.
  """
  for source_leader, target in graph.back_edges():
    if target in body and graph.loop_body(source_leader, target) == body:
      return target
  sys.exit('  no back edge owns this body')


NO_CALLEES = lambda target: 0


def loop_cycles(graph, body):
  """ONE PASS of the loop, by longest path -- not the sum of its blocks.

  Summing charges both arms of every `if` in the body, which is the error that
  made osc_cycles.py head its table with the wrong shape. A call inside costs
  only its branch here; the callee is a line of its own in this table.
  """
  return pathcost.longest_path(
      graph, NO_CALLEES, entry=header_of(graph, body), restrict=body)


def samples_per_iteration(graph, body):
  """Read the buffer stride off the loop itself, so a widened store shows up.

  The fill and the shift both walk the sample buffer; how many int16 they cover
  per pass is the pointer advance, which is in the instructions. Deriving it
  means this tool reports the win automatically if either is widened, instead
  of carrying a number that has to be remembered.
  """
  text = ' ; '.join(t for leader in body for _, t in graph.blocks[leader])
  # All three forms a walk can take: a separate add, a post-indexed store
  # (`[r0], #4`) and a pre-indexed one with writeback (`[r3, #4]!`). Missing the
  # last of these is how this exited on a loop that had simply got better.
  strides = [int(n) for n in re.findall(r'(?:adds?|add\.w)\s+\w+,\s*#(\d+)', text)]
  strides += [int(n) for n in re.findall(
      r'str\S*\s+\w+,\s*\[\w+\](?:,\s*#(\d+))?\]?!?', text) if n]
  strides += [int(n) for n in re.findall(
      r'str\S*\s+\w+,\s*\[\w+,\s*#(\d+)\]!', text)]
  if not strides:
    sys.exit('  cannot read a stride from this loop; it is not a buffer walk')
  return max(strides) // 2


rows = []


def add(label, cycles, count):
  rows.append((label, cycles, count))


# --- the DAC packing: one pass over the block, per buffered output ------------
def stores_per_iteration(graph, body):
  """How many samples one pass writes, counted off the stores themselves.

  BufferSamples is unrolled, so its trip count is not the block size; asserting
  that here charged an unrolled loop its rolled count and reported the unroll as
  a 3x REGRESSION. Counting the stores means the number follows the code.
  """
  stores = sum(1 for leader in body for _, text in graph.blocks[leader]
               if re.match(r'^str', pathcost.mnemonic(text)))
  if not stores:
    sys.exit('  no stores in a loop that is supposed to write the buffer')
  return stores


dac_graph = function('3Dac13BufferSamples')
dac_body = loop_in(dac_graph, 'BufferSamples', 'dac.cc')
dac_per_pass = stores_per_iteration(dac_graph, dac_body)
add('Dac::BufferSamples (%d samples a pass)' % dac_per_pass,
    loop_cycles(dac_graph, dac_body) * (BLOCK_SAMPLES // dac_per_pass),
    OUTPUTS_BUFFERED)

# --- the three loops inside CVOutput::RenderSamples ---------------------------
cv = function('CVOutput13RenderSamples')
fill = loop_in(cv, 'CVOutput::RenderSamples', 'voice.cc',
               source_line('yarns/voice.cc', 'words[i] = zero_pair;'))
fill_trips = BLOCK_SAMPLES // samples_per_iteration(cv, fill)
add('mix buffer fill (%d int16 a pass)' % samples_per_iteration(cv, fill),
    loop_cycles(cv, fill) * fill_trips, AUDIO_OUTPUTS)

# Both remaining loops come from voice.cc, so they are told apart by whether the
# body calls out: the per-voice one contains the oscillator call.
shift = loop_in(cv, 'CVOutput::RenderSamples', 'voice.cc',
                source_line('yarns/voice.cc', 'words[i] <<= 1;'))
dispatch = [cv.loop_body(s_, t_) for s_, t_ in cv.back_edges()
            if any(cv.calls[l] for l in cv.loop_body(s_, t_))]
if len(dispatch) != 1:
  sys.exit('  CVOutput::RenderSamples: %d loops call out, expected 1' % len(dispatch))
for calls, body in ((True, dispatch[0]), (False, shift)):
  if calls:
    # The callee is priced on its own rows below, so the `bl` counts as a branch
    # and nothing else.
    add('per-voice dispatch (loop overhead only)',
        loop_cycles(cv, body), sum(AUDIO_VOICES_PER_OUTPUT))
  else:
    trips = BLOCK_SAMPLES // samples_per_iteration(cv, body)
    add('Q15->Q16 shift (%d int16 a pass)' % samples_per_iteration(cv, body),
        loop_cycles(cv, body) * trips, ENVELOPE_OUTPUTS)

# --- Oscillator::Render's own body: the timbre warp, the tremolo, the dispatch.
# Its two envelope renders and its shape call are counted below, so they are
# excluded here rather than counted twice.
# WITH its warp and its phase-increment helper, WITHOUT the two envelope renders
# it calls -- those are cycles.py's rows. The shape it dispatches to goes through
# a function pointer, which no static walk resolves and osc_cycles.py prices.
envelope_render = [n for n in functions if '8Envelope13RenderSamples' in n]
osc_call_cost, _ = pathcost.call_cost_function(
    functions, boundary=tuple(envelope_render))
osc = function('Oscillator6RenderEPs')
add('Oscillator::Render (warp + dispatch, excl. envelopes)',
    pathcost.longest_path(osc, osc_call_cost), sum(AUDIO_VOICES_PER_OUTPUT))
# CVOutput::RenderSamples' own straight-line body: the branch on output kind and
# the frame it sets up. Its three loops are rows of their own, and its callees
# are priced elsewhere, so both are zeroed here rather than double-counted.
cv_body_zero = [(a, a, 0) for body in (fill, shift, dispatch[0])
                for leader in body for a, _ in cv.blocks[leader]]
add('CVOutput::RenderSamples (body, excl. its loops)',
    pathcost.longest_path(cv, NO_CALLEES, weights=cv_body_zero),
    OUTPUTS_BUFFERED)

# --- the DSP itself, from the two tools that own it --------------------------
def tool(script, *args):
  return subprocess.check_output(
      [sys.executable, os.path.join(HERE, script), dis_path] + list(args),
      encoding='utf8')


# cycles.py prints its human report before the metrics, so take only the
# `key value` lines rather than depending on where they start.
metrics = dict(
    line.split() for line in tool('cycles.py', '--metrics').splitlines()
    if re.match(r'^\w+ -?\d+$', line))
# TWO KINDS OF BLOCK, because they are not close. A steady block renders one
# run an envelope. THE BLOCK A NOTE ARRIVES IN pays three: RenderStage renders
# up to the stage boundary and tail-calls itself, so a fast attack expires
# inside the block and hands off twice. It pays the NoteOn burst on top.
# That is the block the user reports glitching on, so it gets its own column
# rather than a footnote.
STEADY = [('envelopes, one run', int(metrics['block_cycles']), ENVELOPES)]
ATTACK = [('envelopes, %s runs + handoffs' % metrics['runs_per_block'],
           int(metrics['block_cycles_handoff']), ENVELOPES),
          ('NoteOn burst (all %d in one block)' % ENVELOPES,
           int(metrics['note_on_cycles']), ENVELOPES)]

# Only the `name hi c4` lines; the tool prints prose around them too.
shapes = [(parts[0], float(parts[1]), float(parts[2]))
          for parts in (l.split() for l in tool('osc_cycles.py', '--metrics').splitlines())
          if len(parts) == 3 and parts[0].startswith('Render')]
# PER PITCH, not once. The dearest shape at the top of the keyboard is not the
# dearest at middle C: a branchy shape's cost is wrap work it only pays up
# there, and a flat one costs the same everywhere. Picking once by the top note
# priced the middle-C block with a shape that is cheaper there.
worst_shape_hi, worst_hi, _ = max(shapes, key=lambda row: row[1])
worst_shape_c4, _, worst_c4 = max(shapes, key=lambda row: row[2])

print('layout %s -- the hungriest of %d' % (WORST.replace('LAYOUT_', ''), len(layouts)))
print('  %d audio outputs carrying %s voices, %d envelope output(s), %d envelopes'
      % (AUDIO_OUTPUTS, AUDIO_VOICES_PER_OUTPUT, ENVELOPE_OUTPUTS, ENVELOPES))
print('  %d cycles a block at %d Hz on %.0f MHz' % (BUDGET, FRAME_HZ, CPU_HZ / 1e6))
print()
print('  %-46s %8s %4s %9s %7s' % ('per-block item', 'each', 'x', 'cycles', '%CPU'))
for kind, extra in (('steady block', STEADY), ('ATTACK block', ATTACK)):
  for pitch, shape_cycles, worst_shape in (
      ('top note', worst_hi, worst_shape_hi),
      ('middle C', worst_c4, worst_shape_c4)):
    full = rows + extra + [
        ('worst shape, %s (%s)' % (pitch, worst_shape.replace('Render', '')),
         shape_cycles * BLOCK_SAMPLES, sum(AUDIO_VOICES_PER_OUTPUT))]
    total = 0
    print('  --- %s, worst shape at %s ---' % (kind, pitch))
    for label, cycles, count in sorted(full, key=lambda r: -r[1] * r[2]):
      total += cycles * count
      print('  %-46s %8d %4d %9d %6.1f%%'
            % (label, cycles, count, cycles * count, cycles * count / BUDGET * 100))
    flag = '   <-- OVER' if total > BUDGET else ''
    print('  %-46s %8s %4s %9d %6.1f%%%s' % ('TOTAL', '', '', total,
                                             total / BUDGET * 100, flag))
    print()
# EVERY FUNCTION IN THE TREE IS ACCOUNTED FOR, OR THIS SAYS SO.
#
# The rows above are a list someone wrote, and a list cannot tell you what is
# missing from it. That is exactly how AdvanceChiffForBlock's 625 cycles left
# the budget: the work moved out of RenderStage, cycles.py prices RenderStage,
# and the envelope read seven points cheaper for doing the same arithmetic. A
# COST THAT MOVES BETWEEN FUNCTIONS HAS NOT MOVED, and nothing noticed.
#
# So: walk the call tree from the per-block entry and demand a reason for each
# function in it. A new callee is then a loud failure rather than a silent zero.
ACCOUNTED = {
    'CVOutput13RenderSamples': 'three loop rows plus a body row',
    '3Dac13BufferSamples': 'a row',
    '3Dac11FillDCNoops': 'the plain-CV branch; no output in this layout takes it',
    '10Oscillator6Render': 'a row, with its warp and its helpers',
    'Oscillator10WarpTimbre': 'inside the Oscillator::Render row',
    'Oscillator21ComputePhaseIncrement': 'inside the Oscillator::Render row',
    '8Envelope13RenderSamples': 'cycles.py block_cycles',
    '8Envelope20AdvanceChiffForBlock': 'cycles.py block_cycles (chiff_block_cycles)',
    '8Envelope11RenderStage': 'cycles.py block_cycles (run_cycles)',
    '8Envelope18HandOffToNextStage': 'cycles.py block_cycles_handoff',
    '8Envelope7Trigger': 'inside handoff_cycles and note_on_cycles',
    'ChiffSlewTimeAtAmount': 'inside its callers',
    'ChiffSlewInputFractionAtAmount': 'inside its callers',
    'DivU64ByU32': 'inside its callers',
}


def account_for_the_whole_tree():
  by_address = {body[0][0]: name for name, body in functions.items()}
  entry = [n for n in functions if 'CVOutput13RenderSamples' in n]
  if len(entry) != 1:
    sys.exit('  cannot find the per-block entry point')
  seen, stack = set(), [entry[0]]
  while stack:
    name = stack.pop()
    if name in seen or name not in functions:
      continue
    seen.add(name)
    graph = pathcost.Graph(functions[name])
    for targets in graph.calls.values():
      for address in targets:
        if address in by_address:
          stack.append(by_address[address])
  missing = [n for n in sorted(seen)
             if not any(key in n for key in ACCOUNTED)]
  if missing:
    print('  UNACCOUNTED, and therefore counted NOWHERE:')
    for name in missing:
      print('    %s' % re.sub(r'^_ZN5yarns', '', name)[:66])
    print('  Give each a row or a reason in ACCOUNTED. A budget that does not')
    print('  know what it is missing is not a budget.')
    return False
  print('  all %d functions in the per-block call tree are accounted for' % len(seen))
  return True


account_for_the_whole_tree()
print()
print('  THESE TOTALS ARE LOWER BOUNDS. The cost table is a Cortex-M3 core and the')
print('  F103 is not one: two flash wait states at 72 MHz, hidden by the prefetch')
print('  buffer for straight-line code and not for a taken branch. The error has')
print('  the same sign everywhere. Trust a DIFFERENCE between two builds; do not')
print('  quote a total as a measurement. See tools/pathcost.py.')
print()
print('  NOT COUNTED, and it is not nothing: ui.DoEvents, midi_handler.ProcessInput')
print('  and multi.LowPriority share the same main loop and the same 72 MHz. They')
print('  are not per-block, so they are not in a per-block table -- but a note-on')
print('  burst is exactly when MIDI parsing runs, which is when this glitches.')
