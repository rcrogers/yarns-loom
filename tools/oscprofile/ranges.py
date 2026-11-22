# The address ranges QEMU logs (-dfilter): the profile markers and every
# function reachable by call or tail call from the entry functions.
#
#   ranges.py DIS SYM ENTRY_REGEX
#
# A measured block runs only code reachable from the entry it called, so
# nothing it executes is filtered out. A call into a function left out would
# show as a transition fold.py rejects: a call's next logged block must be its
# target. A call or jump through a register cannot be followed statically, so
# finding one in the reachable set logs everything instead.
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pathcost

dis_path, sym_path, entry_pattern = sys.argv[1:4]
functions = pathcost.parse(dis_path)
by_address = dict((body[0][0], name) for name, body in functions.items())
symbols = dict((parts[2], int(parts[0], 16)) for parts in
               (line.split() for line in open(sym_path)) if len(parts) == 3)

reachable, stack = set(), [n for n in functions if re.search(entry_pattern, n)]
if not stack:
  sys.exit('ranges.py: no function matches %s' % entry_pattern)
computed = False
while stack:
  name = stack.pop()
  if name in reachable:
    continue
  reachable.add(name)
  graph = pathcost.Graph(functions[name])
  for targets in graph.calls.values():
    for target in targets:
      if target in by_address:
        stack.append(by_address[target])
  for _, text in functions[name]:
    kind, target = pathcost.classify(text)
    operation = pathcost.mnemonic(text)
    operands = text.split('\t')[-1].split(';')[0].strip()
    if (kind == 'call' and target is None
        or operation.startswith('bx') and operands != 'lr'
        or re.match(r'^(?:ldr|mov)', operation) and operands.startswith('pc,')):
      computed = True

if computed:
  print('0x0..0xffffffff')
else:
  # Inclusive: a function's first address through its last instruction's.
  spans = [(functions[n][0][0], functions[n][-1][0]) for n in reachable]
  spans += [(symbols[m], symbols[m])
            for m in ('ProfileBlockBegin', 'ProfileBlockEnd')]
  print(','.join('0x%x..0x%x' % span for span in sorted(spans)))
