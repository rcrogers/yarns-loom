# Analysis half of tools/cycles.sh -- see that file for what this is for.
#
# TWO THINGS ARE COUNTED, because the envelope costs in two places.
#
#   THE LOOP, per sample. Found by the backward branch whose body holds the
#   output saturate and the store. This is the number that has always been
#   here.
#
#   THE PER-RUN PATH, once per run and never measured before -- only reduced.
#   `function_instructions` has read 531 against a loop of 25, and every
#   reduction of that 531 was argued from disassembly reading. It is priced by
#   tools/pathcost.py as the longest path through the control flow graph, which
#   is the right notion here: this is a realtime system, so the worst case is
#   the only case that has to fit.
#
# The two combine into ONE number, `percent_of_cpu`, which is what the budget
# is actually spent against. The old figure counted the loop alone.
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pathcost

dis_path, args = sys.argv[1], sys.argv[2:]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    'cycles_baseline.txt')

RENDER_STAGE = '_ZN5yarns8Envelope11RenderStageEPsjll'
HAND_OFF = '_ZN5yarns8Envelope18HandOffToNextStageEPsjll'
NOTE_ON = '_ZN5yarns8Envelope6NoteOnERNS_4ADSREllhh'
TRIGGER = '_ZN5yarns8Envelope7TriggerENS_13EnvelopeStageE'

# TWELVE ENVELOPES RENDER PER BLOCK: four CVOutput::envelope_ plus four audio
# voices x (gain, timbre). Same figure kMaxChiffEnvelopes is sized from.
ENVELOPES = 12
# STM32F103 at its 72 MHz ceiling.
CPU_HZ = 72e6


def source_constant(relative_path, pattern, cast=int):
  """Read a constant out of the source so this file holds no second copy."""
  text = open(os.path.join(ROOT, relative_path), encoding='utf8').read()
  match = re.search(pattern, text, re.DOTALL)
  if not match:
    raise SystemExit('  %s: no match for %s' % (relative_path, pattern))
  return cast(match.group(1))


BLOCK_SAMPLES = 1 << source_constant(
    'yarns/drivers/dac.h', r'kAudioBlockSizeBits\s*=\s*(\d+)')
FRAME_HZ = source_constant('yarns/drivers/dac.h', r'kFrameHz\s*=\s*(\d+)')
DRAW_BITS = source_constant(
    'yarns/envelope.cc', r'kChiffDrawBits\s*=\s*(\d+)')
# One PRNG word carries this many samples, and the render loop is chunked at
# that boundary -- so the chunk loop runs BLOCK_SAMPLES / DRAWS_PER_WORD times
# per full-block run, and its overhead is per-chunk, not per-run.
DRAWS_PER_WORD = 32 // DRAW_BITS
CHUNKS_PER_BLOCK = BLOCK_SAMPLES // DRAWS_PER_WORD

functions = pathcost.parse(dis_path)
if RENDER_STAGE not in functions:
  print('  RenderStage not found in the disassembly')
  sys.exit(1)
lines = functions[RENDER_STAGE]

# The sample loop is IDENTIFIED BY WHAT IT CONTAINS, not by its size: it is the
# backward branch whose body holds the output saturate and the store, which
# together happen once per rendered sample and nowhere else. Guessing by span
# picked the wrong loop twice -- the widest branch is outer control flow (298
# instructions, 315% of the CPU), and the tightest is some other inlined loop
# (14 instructions, 1 spill). Anchoring on usat+strh is unambiguous.
loop_start = loop_end = None
for addr, text in lines:
  match = re.search(
      r'\bb(?:ne|eq|cs|cc|mi|pl|hi|ls|ge|lt|gt|le)?(?:\.[nw])?\s+([0-9a-f]{4,})\b',
      text)
  if not match:
    continue
  target = int(match.group(1), 16)
  if target >= addr:
    continue
  body = [t for a, t in lines if target <= a <= addr]
  if any('usat' in t for t in body) and any(re.search(r'\bstrh', t)
                                            for t in body):
    if loop_start is None or addr - target < loop_end - loop_start:
      loop_start, loop_end = target, addr
if loop_start is None:
  print('  could not identify the sample loop (no usat+strh backward branch)')
  sys.exit(1)


def loop_cost(text, is_branch):
  """The loop's own cost table, unchanged, so its baseline still compares."""
  if re.search(r'\b(smull|umull|smlal|umlal)\b', text):
    return 4
  if re.search(r'\b(ldr|str)', text):
    return 2
  if re.search(r'\bit[te]*\b', text):
    return 0
  return 3 if is_branch else 1


body = [(a, t) for a, t in lines if loop_start <= a <= loop_end]
cycles = sum(loop_cost(t, a == loop_end) for a, t in body)
spills = sum(1 for _, t in body if re.search(r'(ldr|str)\w*\s+\S+,\s*\[sp', t))

# THE PER-RUN AND PER-CHUNK PATHS. Weighting a region by 0 charges it nowhere,
# which is how each tier is isolated from the ones counted separately.
graph = pathcost.Graph(lines)
call_cost, _ = pathcost.call_cost_function(
    functions,
    # Priced separately below: a handoff is not part of an ordinary run.
    boundary=(HAND_OFF, TRIGGER))
sample_region = (loop_start, loop_end)
chunk_region = graph.loop_region(containing=sample_region) or sample_region

# Per chunk: the chunk loop's body with the sample loop charged nowhere.
chunk_cycles = pathcost.longest_path(
    pathcost.Graph([(a, t) for a, t in lines
                    if chunk_region[0] <= a <= chunk_region[1]]),
    call_cost,
    weights=[(sample_region[0], sample_region[1], 0)])
# Per run: the whole function with both loops charged nowhere.
run_cycles = pathcost.longest_path(
    graph, call_cost,
    weights=[(chunk_region[0], chunk_region[1], 0)])
# A stage transition, which re-enters RenderStage for the rest of the block.
handoff_call_cost, _ = pathcost.call_cost_function(
    functions, boundary=(RENDER_STAGE,))
handoff_cycles = pathcost.longest_path(
    pathcost.Graph(functions[HAND_OFF]), handoff_call_cost)
# NoteOn's two binary searches are the only loops in the envelope whose trip
# count is a literal rather than a block size, so they are the only ones the
# CFG cannot supply. Read the counts from the source and match each loop to a
# search by the helper its body calls -- addresses move, the calls do not.
SEARCH_TRIPS = [
    ('_ZN5yarnsL28ChiffScaledRmsPerInput_q15_5Em',
     r'ChiffWalkAudibleAmount_q7_25\(.*?for \(uint32_t i = 0; i < (\d+)'),
    ('_ZN5yarnsL22ChiffWalkRemaining_u16Em',
     r'ChiffWalkAudiblePhase_u16\(.*?for \(uint32_t i = 0; i < (\d+)'),
]
note_on_graph = pathcost.Graph(functions[NOTE_ON])
callee_names = {body[0][0]: name for name, body in functions.items()}
search_weights = []
for callee, pattern in SEARCH_TRIPS:
  trips = source_constant('yarns/envelope.cc', pattern)
  for source, target in note_on_graph.back_edges():
    low = min(source, target)
    high = max(address for address, _ in note_on_graph.blocks[source])
    calls = {callee_names.get(target_address)
             for leader in note_on_graph.blocks
             if low <= leader <= high
             for target_address in note_on_graph.calls[leader]}
    if callee in calls:
      search_weights.append((low, high, trips))
# A rotated loop peels its first iteration ahead of the header, so a trip or so
# of each search sits outside the range weighted here. Sizing, not accounting.
note_on_cycles = pathcost.longest_path(
    note_on_graph, handoff_call_cost, weights=search_weights)

block_cycles = (BLOCK_SAMPLES * cycles
                + CHUNKS_PER_BLOCK * chunk_cycles
                + run_cycles)
budget = CPU_HZ * BLOCK_SAMPLES / FRAME_HZ

report = {
    'loop_instructions': len(body),
    'loop_cycles': cycles,
    'loop_spills': spills,
    'function_instructions': len(lines),
    'chunk_cycles': chunk_cycles,
    'run_cycles': run_cycles,
    'handoff_cycles': handoff_cycles,
    'note_on_cycles': note_on_cycles,
    'block_cycles': block_cycles,
}
for key, value in report.items():
  print(f'  {key:<22} {value}')
print('  ---')
print(f'  {"per sample":<22} {BLOCK_SAMPLES:>5} x {cycles}')
print(f'  {"per chunk":<22} {CHUNKS_PER_BLOCK:>5} x {chunk_cycles}')
print(f'  {"per run":<22} {1:>5} x {run_cycles}')
print(f'  {"percent_of_cpu":<22} {block_cycles * ENVELOPES / budget * 100:.1f}%'
      f'  ({ENVELOPES} envelopes x {FRAME_HZ} Hz on {CPU_HZ / 1e6:.0f} MHz)')
print(f'  {"loop_only_percent":<22} '
      f'{BLOCK_SAMPLES * cycles * ENVELOPES / budget * 100:.1f}%'
      '  (what this tool reported before the per-run path was priced)')
print(f'  {"note_on_burst":<22} '
      f'{note_on_cycles * ENVELOPES / budget * 100:.1f}%'
      f'  ({ENVELOPES} NoteOns landing in one block, on top of the above)')

if '--update' in args:
  with open(BASE, 'w') as f:
    for key, value in report.items():
      f.write(f'{key} {value}\n')
  print('  baseline updated')
  sys.exit(0)

if not os.path.exists(BASE):
  print('  no baseline yet -- run with --update')
  sys.exit(0)

old = {}
for line in open(BASE):
  key, value = line.split()
  old[key] = int(value)
print('  ---')
worse = False
for key, value in report.items():
  if key in old and value != old[key]:
    tag = 'REGRESSION' if value > old[key] else 'improved  '
    if value > old[key]:
      worse = True
    print(f'  {tag} {key:<20} {old[key]} -> {value}')
if worse:
  print('  WORSE THAN BASELINE')
  sys.exit(1)
print('  no regression against the baseline')
