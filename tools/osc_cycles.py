# Per-sample cost of every oscillator shape, so the WORST CASE is visible.
#
# The audio budget is set by the most expensive shape running on every voice at
# once, not by the average, so a table of all of them is worth more than any one
# optimisation. Run after touching oscillator.cc. tools/cycles.py is the same
# idea for the envelope's render loop.
#
#   SKIP_PROGRAMMING=true ./env/mutable-env.sh \
#     /usr/local/arm-4.8.3/bin/arm-none-eabi-objdump -d build/yarns/yarns.elf \
#     > /tmp/yarns.dis && python3 tools/osc_cycles.py /tmp/yarns.dis
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
import re
import sys

dis_path = sys.argv[1]

funcs, cur = {}, None
for line in open(dis_path, encoding='utf8', errors='replace'):
    m = re.match(r'^[0-9a-f]+ <(.+)>:', line)
    if m:
        cur, funcs[cur] = m.group(1), []
        continue
    m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} ?)+)\s*\t(.*)', line)
    if cur and m:
        funcs[cur].append((int(m.group(1), 16), m.group(3).strip(),
                           len(m.group(2).replace(' ', '')) // 2))

BRANCH = re.compile(r'^(b|b\.n|b\.w|bx|blx?)\b|^b(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)(\.[nw])?\b')
TARGET = re.compile(r'\b([0-9a-f]{4,})\b')


def cost(text, taken_branch):
    if re.search(r'\b(smull|umull|smlal|umlal)\b', text):
        return 4
    if re.search(r'\b(udiv|sdiv)\b', text):
        return 8
    if re.search(r'\b(ldr|str)', text):
        return 2
    if re.search(r'\bit[te]*\b', text):
        return 0
    return 3 if taken_branch else 1


def analyse(lines):
    addrs = [a for a, _, _ in lines]
    text = {a: t for a, t, _ in lines}
    size = {a: n for a, _, n in lines}
    nxt = {a: addrs[i + 1] if i + 1 < len(addrs) else None
           for i, a in enumerate(addrs)}
    succ = {}
    for a in addrs:
        t = text[a]
        op = t.split()[0]
        tgt = None
        m = TARGET.search(t.split(None, 1)[1]) if ' ' in t else None
        if m and re.match(r'^b', op) and not op.startswith('bl'):
            v = int(m.group(1), 16)
            if v in text:
                tgt = v
        s = []
        if tgt is not None:
            s.append(tgt)
        uncond = op in ('b', 'b.n', 'b.w') or op.startswith('bx') or 'pop' in t
        if not uncond and nxt[a] is not None:
            s.append(nxt[a])
        succ[a] = s
    preds = {a: [] for a in addrs}
    for a in addrs:
        for b in succ[a]:
            preds[b].append(a)

    def natural_loop(head, tail):
        body, stack = {head}, [tail]
        while stack:
            n = stack.pop()
            if n in body:
                continue
            body.add(n)
            stack.extend(preds[n])
        return body

    loops = []
    for a in addrs:
        for b in succ[a]:
            if b <= a:
                loops.append((b, a, natural_loop(b, a)))
    return loops, text, size


rows = []
for name, lines in funcs.items():
    if 'Oscillator' not in name or 'Render' not in name or not lines:
        continue
    loops, text, size = analyse(lines)
    sample = [L for L in loops
              if any(re.search(r'\bstrh', text[a]) for a in L[2])]
    if not sample:
        continue
    head, tail, body = max(sample, key=lambda L: len(L[2]))
    # A nested loop only counts as an EDGE path if it does not itself contain
    # the sample store. GCC rotates `while (true) { if (!x) break; ... }` so its
    # back edge's natural loop overlaps most of the sample loop; subtracting
    # that leaves nothing, and the shape reads as free.
    nested = set()
    for h2, t2, b2 in loops:
        if b2 < body and not any(re.search(r'\bstrh', text[a]) for a in b2):
            nested |= b2
    per = body - nested
    cyc = sum(cost(text[a], a == tail) for a in per)
    edge = sum(cost(text[a], False) for a in nested)
    spills = sum(1 for a in per if re.search(r'(ldr|str)\w*\s+\S+,\s*\[sp', text[a]))
    br = sum(1 for a in per if re.match(r'^b(?!l)', text[a].split()[0]))
    short = re.sub(r'^_ZN5yarns10Oscillator\d+', '', name).split('E')[0]
    rows.append((cyc, len(per), spills, br, edge, short))

rows.sort(reverse=True)
VOICES = 4
print('  %-28s %5s %6s %7s %8s %6s %7s'
      % ('shape', 'instr', 'cycles', 'spills', 'branches', 'edge', '%CPU'))
for cyc, n, sp, br, ec, nm in rows:
    print('  %-28s %5d %6d %7d %8d %6d %6.1f%%'
          % (nm[:28], n, cyc, sp, br, ec, cyc * VOICES * 45000 / 72e6 * 100))
print('  ---')
print('  %%CPU = this shape on all %d audio voices, 45 kHz on 72 MHz.' % VOICES)
print('  THIS IS AN UPPER BOUND, NOT THE EXECUTED COST. It sums every block in')
print('  the loop, and mutually exclusive arms of an if/switch cannot all run on')
print('  one sample -- so a branchy shape is charged for paths it did not take.')
print('  Read it as "worst case through the loop"; that is the right quantity')
print('  for a realtime budget, but it is NOT what an average sample costs.')
print('  BRANCHES ARE UNDERCOUNTED: 3 cycles are charged for the loop-closing')
print('  branch and 1 for any other, but a TAKEN branch costs ~3 -- so a shape')
print('  with several internal branches is dearer on hardware than shown.')
