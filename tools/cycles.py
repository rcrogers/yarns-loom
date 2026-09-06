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
import math
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pathcost

dis_path, args = sys.argv[1], sys.argv[2:]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Mangled names carry the parameter types, so a signature change renames the
# symbol and the lookup rots. Match the prefix that ends at the parameter list
# and insist it is unique.
RENDER_STAGE = '_ZN5yarns8Envelope11RenderStageE'
CHIFF_BLOCK = '_ZN5yarns8Envelope20AdvanceChiffForBlockE'
HAND_OFF = '_ZN5yarns8Envelope18HandOffToNextStageE'
NOTE_ON = '_ZN5yarns8Envelope6NoteOnE'
TRIGGER = '_ZN5yarns8Envelope7TriggerE'

# STM32F103 at its 72 MHz ceiling.
CPU_HZ = 72e6


def source_constant(relative_path, pattern, cast=int):
  """Read a constant out of the source so this file holds no second copy."""
  text = open(os.path.join(ROOT, relative_path), encoding='utf8').read()
  match = re.search(pattern, text, re.DOTALL)
  if not match:
    raise SystemExit('  %s: no match for %s' % (relative_path, pattern))
  return cast(match.group(1))


# HOW MANY ENVELOPES RENDER PER BLOCK, read rather than restated. It used to be
# a literal here, under a comment describing four CVOutput envelopes plus four
# audio voices -- a configuration no layout offers, since CVOutput::is_envelope
# is literally !is_audio(). multi.h folds the layout map and static-asserts this
# figure against it; the hungriest layout is PARAPHONIC_PLUS_TWO.
ENVELOPES = source_constant(
    'yarns/envelope.h', r'kMaxChiffEnvelopes\s*=\s*(\d+)')

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


def resolve(prefix):
  matches = [name for name in functions if name.startswith(prefix)]
  if len(matches) != 1:
    sys.exit('cycles: %s matched %d symbols, not one -- the signature moved'
             % (prefix, len(matches)))
  return matches[0]


RENDER_STAGE = resolve(RENDER_STAGE)
HAND_OFF = resolve(HAND_OFF)
NOTE_ON = resolve(NOTE_ON)
TRIGGER = resolve(TRIGGER)
CHIFF_BLOCK = resolve(CHIFF_BLOCK)
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
#
# THERE IS MORE THAN ONE SUCH LOOP once the render is unrolled to a whole PRNG
# word: the unrolled one, and the head/tail loop for the samples that do not
# fill a word. The HOT one is the one that emits the most samples per
# iteration, and its usat count IS its samples per iteration -- so the same
# anchor that finds the loop also says how many samples it renders.
graph_for_loops = pathcost.Graph(lines)
candidates = []
for source, target in graph_for_loops.back_edges():
  body_blocks = graph_for_loops.loop_body(source, target)
  # RenderStage tail-calls ITSELF for the rest of the block, and GCC compiles
  # that as a back edge over the prologue. It emits samples, so it looks like a
  # sample loop -- but its body is the whole function, and treating it as one
  # charged the entire per-run path nothing: 59 cycles for a path that costs
  # 511. Its header is where the prologue falls through to, which no loop
  # inside a run is, because the run's setup comes first. A LOOP_SAMPLES that
  # stops matching the unroll factor is how this would show up if it ever
  # stopped being true.
  if target == graph_for_loops.entry or \
      target in graph_for_loops.edges[graph_for_loops.entry]:
    continue
  body = [instruction for leader in body_blocks
          for instruction in graph_for_loops.blocks[leader]]
  saturates = sum(1 for _, text in body if 'usat' in text)
  if saturates and any(re.search(r'\bstrh', text) for _, text in body):
    candidates.append((saturates, body_blocks, source, target))
if not candidates:
  print('  could not identify the sample loop (no usat+strh backward branch)')
  sys.exit(1)
# The HOT loop is innermost: no other candidate sits inside it. Counting over
# the CFG body rather than an address span means an OUTER loop now counts the
# saturates of every loop it contains, so "most saturates" alone would pick the
# outermost one.
innermost = [candidate for candidate in candidates
             if not any(other[1] < candidate[1] for other in candidates)]
loop_saturates, loop_blocks, loop_source, loop_target = max(
    innermost, key=lambda candidate: candidate[0])
loop_edge = (loop_source, loop_target)
loop_start = min(address for leader in loop_blocks
                 for address, _ in graph_for_loops.blocks[leader])
loop_end = max(address for leader in loop_blocks
               for address, _ in graph_for_loops.blocks[leader])
LOOP_SAMPLES = loop_saturates
# Anything else that emits samples is the head/tail path: it renders nothing in
# the common case (a full block, starting on a word boundary), so it is priced
# but not charged per block.
tail_edges = [(source, target) for _, _, source, target in candidates
              if (source, target) != loop_edge]


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
call_cost, call_names = pathcost.call_cost_function(
    functions,
    # Priced separately below: a handoff is not part of an ordinary run, and
    # neither is a re-entry. RenderStage tail-calls ITSELF for the rest of a
    # block after a stage boundary, and GCC compiles that either as a loop or
    # as a `b.w` to the function's own entry depending on how big the body is.
    # As a loop the back edge gets cut; as a branch it looks like a call, and
    # left priced it charged one run for the next one as well -- 1126 cycles
    # of double counting that appeared the moment the render loop was unrolled.
    boundary=(HAND_OFF, TRIGGER, RENDER_STAGE))
sample_region = (loop_start, loop_end)
# A loop that renders FEWER samples than a PRNG word holds is chunked at word
# boundaries by a loop around it, and that wrapper then runs once per word --
# where its whole cost is the render body's registers being spilled and
# reloaded. A loop that consumes a whole word has no such wrapper to pay for.
chunk_region = None
if LOOP_SAMPLES < DRAWS_PER_WORD:
  chunk_region = graph.loop_region(containing=sample_region)
chunk_cycles = 0
if chunk_region:
  chunk_cycles = pathcost.longest_path(
      pathcost.Graph([(a, t) for a, t in lines
                      if chunk_region[0] <= a <= chunk_region[1]]),
      call_cost,
      weights=[(sample_region[0], sample_region[1], 0)])
# Per run: the whole function with everything that renders samples charged
# nowhere, since those are counted per sample above.
# BY BLOCK, NOT BY ADDRESS SPAN. A loop's blocks are scattered by the compiler,
# and a span from the back edge's target to its source sweeps up whatever landed
# between them. Zeroing that span once priced a `bl` in the per-run setup at
# nothing, because GCC had moved it inside a head/tail loop's address range --
# reporting 169 cycles for a path that costs 511.
rendering = []
for source, target in [loop_edge] + tail_edges:
  rendering += graph.block_weights(graph.loop_body(source, target), 0)
# A loop in the per-run path that emits no samples still RUNS its trip count,
# and longest_path cuts back edges, so left alone it is priced once. The one
# here builds the sixteen levels the render loop reads, so its trips come from
# the draw width.
DRAW_LEVELS = 1 << DRAW_BITS
sample_loops = {(loop_edge[0], loop_edge[1])} | set(tail_edges)
for source, target in graph.back_edges():
  if (source, target) in sample_loops:
    continue
  if target == graph.entry or target in graph.edges[graph.entry]:
    continue
  rendering += graph.block_weights(
      graph.loop_body(source, target), DRAW_LEVELS)
if chunk_region:
  rendering.append((chunk_region[0], chunk_region[1], 0))
run_cycles = pathcost.longest_path(graph, call_cost, weights=rendering)
# WHERE THE RUN'S SETUP GOES. It is ~a third of the envelope's block cost and
# every cycle of it is parameter computation, so it is the part that can be
# made cheaper without moving a sample. A total on its own could not be acted
# on; this says which callee to open.
run_breakdown = pathcost.longest_path_breakdown(
    graph, call_cost, call_names, weights=rendering)
# A stage transition, which re-enters RenderStage for the rest of the block.
handoff_call_cost, _ = pathcost.call_cost_function(
    functions, boundary=(RENDER_STAGE,))
# TRIGGER RECURSES, AND GCC TURNED THAT INTO A LOOP. `return Trigger(stage + 1)`
# fires whenever a stage starts where it is meant to end -- peak equal to
# sustain reaches it on an ordinary note -- and the tail call is compiled to a
# back edge, which longest_path cuts. Priced as written, Trigger read as ONE
# pass of a body that can run several, and note_on_cycles was short by that
# much.
#
# THE BOUND IS WHERE THE CHAIN STOPS, NOT THE NUMBER OF STAGES. The recursion
# strictly increases the stage, and the switch above it has cases only for the
# TIMED stages; the first stage without one falls to `default:`, which returns
# before the recursion is reached. So the chain is ATTACK up to and including
# that stage. Read off the enum and the switch, so adding a stage or giving
# SUSTAIN a case moves this by itself.
_stage_order = re.findall(
    r'(ENV_STAGE_\w+),',
    source_constant('yarns/envelope.h',
                    r'enum EnvelopeStage \{(.*?)\};', cast=str))
_timed_stages = set(re.findall(
    r'case (ENV_STAGE_\w+)\s*:\s*stage_phase_increment_u32_',
    source_constant('yarns/envelope.cc',
                    r'void Envelope::Trigger\(EnvelopeStage stage\) \{(.*?)\n\}',
                    cast=str)))
TRIGGER_CHAIN = next(
    (i + 1 for i, name in enumerate(_stage_order) if name not in _timed_stages),
    len(_stage_order))
ENV_NUM_STAGES = TRIGGER_CHAIN
trigger_graph = pathcost.Graph(functions[TRIGGER])
trigger_weights = [
    (min(a for leader in trigger_graph.loop_body(source, target)
         for a, _ in trigger_graph.blocks[leader]),
     max(a for leader in trigger_graph.loop_body(source, target)
         for a, _ in trigger_graph.blocks[leader]),
     ENV_NUM_STAGES)
    for source, target in trigger_graph.back_edges()]
trigger_cycles = pathcost.longest_path(
    trigger_graph, handoff_call_cost, weights=trigger_weights)
_uncounted_trigger = pathcost.longest_path(trigger_graph, handoff_call_cost)


def note_on_call_cost(target):
  """Trigger at its recursive worst; everything else as the handoff prices it."""
  if target == functions[TRIGGER][0][0]:
    return trigger_cycles
  return handoff_call_cost(target)
handoff_cycles = pathcost.longest_path(
    pathcost.Graph(functions[HAND_OFF]), handoff_call_cost)
# NOTEON HAS ONE SEARCH LEFT and it calls nothing, so it cannot be identified
# by a callee the way the old pair were. It is the inverse-interpolation of
# lut_env_expo_u16 in ChiffWalkAudiblePhase_u16 -- a bisection over the table, so
# its trip count is ceil(log2(size)) and comes from the TABLE SIZE rather than
# a literal in the source.
#   The two it replaces were ChiffWalkAudibleAmount_q7_25 (twelve iterations,
# now a closed form -- the level law made the threshold amount solvable) and
# the old sixteen-iteration bisection of the curve itself.
LUT_ENV_EXPO_U16_SIZE = source_constant(
    'yarns/resources.h', r'LUT_ENV_EXPO_U16_SIZE\s+(\d+)')
TABLE_SEARCH_TRIPS = int(math.ceil(math.log(LUT_ENV_EXPO_U16_SIZE, 2)))
note_on_graph = pathcost.Graph(functions[NOTE_ON])
callee_names = {body[0][0]: name for name, body in functions.items()}
search_weights = []
for source, target in note_on_graph.back_edges():
  low = min(source, target)
  high = max(address for address, _ in note_on_graph.blocks[source])
  calls = {callee_names.get(target_address)
           for leader in note_on_graph.blocks
           if low <= leader <= high
           for target_address in note_on_graph.calls[leader]}
  # A call-free back edge inside NoteOn is the table bisection. Anything that
  # calls out is not a search and the CFG already sizes it.
  if not any(calls):
    search_weights.append((low, high, TABLE_SEARCH_TRIPS))
# A rotated loop peels its first iteration ahead of the header, so a trip or so
# of each search sits outside the range weighted here. Sizing, not accounting.
note_on_cycles = pathcost.longest_path(
    note_on_graph, note_on_call_cost, weights=search_weights)
# NoteOn is a worst-case burst of its own -- 13 of them can land in one block --
# and like the run setup it is all parameter computation.
note_on_breakdown = pathcost.longest_path_breakdown(
    note_on_graph, note_on_call_cost, call_names, weights=search_weights)

loop_iterations = BLOCK_SAMPLES // LOOP_SAMPLES
# THE CHIFF IS DERIVED ONCE A BLOCK, OUTSIDE RenderStage, and must be counted
# here or it is counted nowhere. It used to sit in the run setup; moving it out
# made run_cycles fall by 573 and the envelope look 7 points cheaper, which was
# an artifact of measuring the function it left rather than the work it does.
# A cost that moves between functions has not moved.
chiff_block_graph = pathcost.Graph(functions[CHIFF_BLOCK])
chiff_block_cycles = pathcost.longest_path(
    chiff_block_graph, call_cost,
    weights=[(min(a for leader in chiff_block_graph.loop_body(source, target)
                  for a, _ in chiff_block_graph.blocks[leader]),
              max(a for leader in chiff_block_graph.loop_body(source, target)
                  for a, _ in chiff_block_graph.blocks[leader]),
              DRAW_LEVELS)
             for source, target in chiff_block_graph.back_edges()])

block_cycles = (loop_iterations * cycles
                + CHUNKS_PER_BLOCK * chunk_cycles
                + chiff_block_cycles
                + run_cycles)
# A BLOCK IS NOT ONE RUN WHEN A STAGE ENDS INSIDE IT. RenderStage renders
# `min(block_samples_left, stage_samples_left_)` and then TAIL-CALLS ITSELF via
# HandOffToNextStage, so a note-on with a fast attack pays the run setup once
# per stage, not once per block. The shortest stage is four samples
# (envelope.cc, kMaxSlewRate), so the whole chain fits inside one block easily.
#
# The chain is the same one TRIGGER_CHAIN counts -- the timed stages up to the
# first hold -- because a hold has no countdown to expire and runs to the end of
# the block. Three runs, two handoffs.
#
# The sample loop is NOT multiplied: the block still renders the same 64
# samples, just split across more runs. What multiplies is the SETUP.
RUNS_PER_BLOCK = TRIGGER_CHAIN
block_cycles_handoff = (loop_iterations * cycles
                        + CHUNKS_PER_BLOCK * chunk_cycles
                        + chiff_block_cycles
                        + RUNS_PER_BLOCK * run_cycles
                        + (RUNS_PER_BLOCK - 1) * handoff_cycles)
budget = CPU_HZ * BLOCK_SAMPLES / FRAME_HZ

report = {
    'loop_instructions': len(body),
    'loop_samples': LOOP_SAMPLES,
    'loop_cycles': cycles,
    'loop_spills': spills,
    'function_instructions': len(lines),
    'chunk_cycles': chunk_cycles,
    'run_cycles': run_cycles,
    'chiff_block_cycles': chiff_block_cycles,
    'handoff_cycles': handoff_cycles,
    'note_on_cycles': note_on_cycles,
    'block_cycles': block_cycles,
    'runs_per_block': RUNS_PER_BLOCK,
    'block_cycles_handoff': block_cycles_handoff,
}
for key, value in report.items():
  print(f'  {key:<22} {value}')
print('  ---')
print(f'  {"per sample":<22} {loop_iterations:>5} x {cycles}'
      f'   ({cycles / LOOP_SAMPLES:.1f} per sample)')
print(f'  {"per chunk":<22} {CHUNKS_PER_BLOCK if chunk_cycles else 0:>5}'
      f' x {chunk_cycles}')
print(f'  {"per run":<22} {1:>5} x {run_cycles}')
print(f'  {"chiff, per block":<22} {1:>5} x {chiff_block_cycles}')
print('  --- where the run setup goes ---')
for label, spent in run_breakdown:
  if spent:
    print(f'  {label:<46} {spent:>5}  {spent / run_cycles * 100:4.1f}%')
print(f'  {"stage handoffs":<22} {RUNS_PER_BLOCK:>5} runs a block worst case, '
      f'{block_cycles_handoff} cycles ('
      f'{block_cycles_handoff * ENVELOPES / budget * 100:.1f}% of CPU)')
print(f'  {"percent_of_cpu":<22} {block_cycles * ENVELOPES / budget * 100:.1f}%'
      f'  ({ENVELOPES} envelopes x {FRAME_HZ} Hz on {CPU_HZ / 1e6:.0f} MHz)')
print(f'  {"render_percent":<22} '
      f'{loop_iterations * cycles * ENVELOPES / budget * 100:.1f}%'
      '  (the loop alone -- all this tool used to report)')
print(f'  {"note_on_burst":<22} '
      f'{note_on_cycles * ENVELOPES / budget * 100:.1f}%'
      f'  ({ENVELOPES} NoteOns landing in one block, on top of the above)')
print('  A CHAIN IS CHARGED ITS WORST PASS EVERY TIME, which is an upper bound and')
print('  not a reachable one: Trigger recurses only where a stage starts on its')
print('  target, and that is the case its dearest branch cannot take. Work removed')
print('  from the REPEATED passes alone is therefore invisible here, and can even')
print('  read as a regression if it costs the first pass a compare. Reason about')
print('  that class of change from the CFG, not from this number.')
print(f'  trigger_chain          {TRIGGER_CHAIN:>5}  passes, worst body charged for each')
print('  --- where NoteOn goes ---')
for label, spent in note_on_breakdown:
  if spent:
    print(f'  {label:<46} {spent:>5}  {spent / note_on_cycles * 100:4.1f}%')

if '--metrics' in args:
  # Machine-readable, for cycles.sh to diff two builds with. Nothing is written
  # to the tree: a stored number is priced by the model that stored it, and this
  # file's model has moved more than once. Two builds priced by TODAY's model
  # move together, so a comparison cannot go stale.
  for key, value in report.items():
    print(f'{key} {value}')
