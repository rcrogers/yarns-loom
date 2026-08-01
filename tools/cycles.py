# Analysis half of tools/cycles.sh -- see that file for what this is for.
# Finds Envelope::RenderStage in a disassembly, identifies the sample loop as
# the span covered by the furthest-reaching backward branch, and counts it.
import re, sys, os

dis_path, args = sys.argv[1], sys.argv[2:]
BASE = os.path.join(os.path.dirname(__file__), 'cycles_baseline.txt')
FN = '<_ZN5yarns8Envelope11RenderStageEPsjll>:'

lines, inside = [], False
for line in open(dis_path, encoding='utf8', errors='replace'):
    if line.rstrip().endswith(FN):
        inside = True; continue
    if inside and re.match(r'^[0-9a-f]+ <', line):
        break
    m = re.match(r'\s*([0-9a-f]+):\s', line)
    if inside and m:
        lines.append((int(m.group(1), 16), line.rstrip()))

if not lines:
    print('  RenderStage not found in the disassembly'); sys.exit(1)

# The sample loop is IDENTIFIED BY WHAT IT CONTAINS, not by its size: it is the
# backward branch whose body holds the output saturate and the store, which
# together happen once per rendered sample and nowhere else. Guessing by span
# picked the wrong loop twice -- the widest branch is outer control flow (298
# instructions, 315% of the CPU), and the tightest is some other inlined loop
# (14 instructions, 1 spill). Anchoring on usat+strh is unambiguous.
loop_start = loop_end = None
for addr, text in lines:
    m = re.search(r'\bb(?:ne|eq|cs|cc|mi|pl|hi|ls|ge|lt|gt|le)?(?:\.[nw])?\s+([0-9a-f]{4,})\b', text)
    if not m:
        continue
    tgt = int(m.group(1), 16)
    if tgt >= addr:
        continue
    body = [t for a, t in lines if tgt <= a <= addr]
    if any('usat' in t for t in body) and any(re.search(r'\bstrh', t) for t in body):
        if loop_start is None or addr - tgt < loop_end - loop_start:
            loop_start, loop_end = tgt, addr
if loop_start is None:
    print('  could not identify the sample loop (no usat+strh backward branch)')
    sys.exit(1)

def cost(text, is_branch):
    if re.search(r'\b(smull|umull|smlal|umlal)\b', text): return 4
    if re.search(r'\b(ldr|str)', text):                   return 2
    if re.search(r'\bit[te]*\b', text):                   return 0
    return 3 if is_branch else 1

body = [(a, t) for a, t in lines if loop_start <= a <= loop_end]
cycles = sum(cost(t, a == loop_end) for a, t in body)
spills = sum(1 for _, t in body if re.search(r'(ldr|str)\w*\s+\S+,\s*\[sp', t))

report = {
    'loop_instructions': len(body),
    'loop_cycles': cycles,
    'loop_spills': spills,
    'function_instructions': len(lines),
}
for k, v in report.items():
    print(f'  {k:<22} {v}')
print('  ---')
pct = cycles * 12 * 45000 / 72e6 * 100
print(f'  {"percent_of_cpu":<22} {pct:.1f}%  (12 envelopes x 45 kHz on 72 MHz)')

if '--update' in args:
    with open(BASE, 'w') as f:
        for k, v in report.items():
            f.write(f'{k} {v}\n')
    print('  baseline updated'); sys.exit(0)

if not os.path.exists(BASE):
    print('  no baseline yet -- run with --update'); sys.exit(0)

old = {}
for line in open(BASE):
    k, v = line.split()
    old[k] = int(v)
print('  ---')
worse = False
for k, v in report.items():
    if k in old and v != old[k]:
        tag = 'REGRESSION' if v > old[k] else 'improved  '
        if v > old[k]: worse = True
        print(f'  {tag} {k:<20} {old[k]} -> {v}')
if worse:
    print('  WORSE THAN BASELINE'); sys.exit(1)
print('  no regression against the baseline')
