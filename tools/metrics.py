# EVERY METRIC A CHANGE CAN MOVE, IN ONE PLACE, AGAINST A NAMED BASELINE:
# per-shape cycles and spills, the per-block totals, flash, and the goldens.
# A change is neutral only against all of them.
#
#   ./env/mutable-env.sh make -f yarns/makefile syx     # build the baseline
#   python3 tools/metrics.py save before
#   ...edit...
#   ./env/mutable-env.sh make -f yarns/makefile syx     # build the change
#   python3 tools/metrics.py diff before
#
# `save` reads the CURRENT build tree; `diff` compares a saved snapshot with
# a fresh reading of it. Snapshots live in .metrics/ and are not tracked --
# they describe a build, and the build is reproducible from the SHA they carry.
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SNAPSHOTS = os.path.join(ROOT, '.metrics')
# Repo-relative: these run inside the build image, where the host's
# absolute paths do not exist.
ELF = 'build/yarns/yarns.elf'
OBJDUMP = '/usr/local/arm-4.8.3/bin/arm-none-eabi-objdump'
SIZE = '/usr/local/arm-4.8.3/bin/arm-none-eabi-size'


def in_env(args):
  """Run a toolchain command inside the build image, as the makefile does."""
  env = dict(os.environ, SKIP_PROGRAMMING='true')
  out = subprocess.run([os.path.join(ROOT, 'env/mutable-env.sh')] + args,
                       cwd=ROOT, env=env, capture_output=True, text=True)
  if not out.stdout.strip():
    sys.exit('metrics: %s produced nothing. Is build/yarns/yarns.elf built?'
             % args[0])
  return out.stdout


def disassembly():
  # -dl, not -d: osc_cycles and block_budget find loops by source line.
  return in_env([OBJDUMP, '-dl', ELF])


def sizes():
  for line in in_env([SIZE, ELF]).splitlines():
    parts = line.split()
    if len(parts) >= 4 and parts[0].isdigit():
      return {'text': int(parts[0]), 'data': int(parts[1]), 'bss': int(parts[2])}
  return {}


def run_tool(name, dis_path):
  return subprocess.run([sys.executable, os.path.join(HERE, name), dis_path],
                        capture_output=True, text=True).stdout


def shapes(dis_path):
  """Per-shape worst-case cycles, %CPU and spills, from osc_cycles.py."""
  out = {}
  for line in run_tool('osc_cycles.py', dis_path).splitlines():
    m = re.match(r'\s+(Render\w+)\s+([\d.]+)%\s+([\d.]+)%\s+(\d+)\s+(\d+)'
                 r'\s+(\d+)\s+(\d+)', line)
    if m:
      out[m.group(1)] = {'cpu_hi': float(m.group(2)), 'cpu_c4': float(m.group(3)),
                         'worst_cycles': int(m.group(5)), 'spills': int(m.group(6))}
  return out


def blocks(dis_path):
  """The per-block totals, one per case block_budget.py reports."""
  out, case = {}, None
  for line in run_tool('block_budget.py', dis_path).splitlines():
    m = re.match(r'\s+--- (.+) ---', line)
    if m:
      case = m.group(1)
    m = re.match(r'\s+TOTAL\s+(\d+)\s+([\d.]+)%', line)
    if m and case:
      out[case] = {'cycles': int(m.group(1)), 'percent': float(m.group(2))}
  return out


def goldens():
  path = os.path.join(ROOT, 'tools/osctest/golden_shapes.json')
  try:
    with open(path) as f:
      return json.load(f)
  except (IOError, ValueError):
    return {}


def capture():
  dis_path = os.path.join(SNAPSHOTS, '.dis')
  os.makedirs(SNAPSHOTS, exist_ok=True)
  with open(dis_path, 'w') as f:
    f.write(disassembly())
  sha = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT,
                       capture_output=True, text=True).stdout.strip()
  dirty = subprocess.run(['git', 'status', '--porcelain', '--', 'yarns'],
                         cwd=ROOT, capture_output=True, text=True).stdout.strip()
  return {'sha': sha + ('-dirty' if dirty else ''), 'size': sizes(),
          'shapes': shapes(dis_path), 'blocks': blocks(dis_path),
          'goldens': goldens()}


# UP IS WORSE for every metric here. The goldens are a change or no change,
# and never an improvement.
def report(before, after):
  moved = False
  print('  %s -> %s' % (before['sha'], after['sha']))

  for key in ('text', 'data', 'bss'):
    a, b = before['size'].get(key, 0), after['size'].get(key, 0)
    if a != b:
      moved = True
      print('  flash/%-5s %8d -> %8d  %+d bytes' % (key, a, b, b - a))

  for name in sorted(set(before['shapes']) | set(after['shapes'])):
    x = before['shapes'].get(name, {})
    y = after['shapes'].get(name, {})
    for field, unit in (('worst_cycles', 'cycles a sample'), ('spills', 'spills')):
      a, b = x.get(field), y.get(field)
      if a != b:
        moved = True
        print('  %-26s %s %s -> %s  %+d' % (name, unit, a, b, (b or 0) - (a or 0)))

  for case in sorted(set(before['blocks']) | set(after['blocks'])):
    a = before['blocks'].get(case, {}).get('percent')
    b = after['blocks'].get(case, {}).get('percent')
    if a != b:
      moved = True
      print('  block: %-38s %.1f%% -> %.1f%%  %+.1f' % (case, a, b, b - a))

  changed = [s for s in set(before['goldens']) | set(after['goldens'])
             if before['goldens'].get(s) != after['goldens'].get(s)]
  if changed:
    moved = True
    print('  goldens MOVED for shape(s): %s' % ', '.join(sorted(changed, key=int)))
    print('    the render changed; neutral is not available. Say why.')

  if not moved:
    print('  no metric moved: cycles, spills, block totals, flash and goldens '
          'all identical.')


def main():
  mode = sys.argv[1] if len(sys.argv) > 1 else 'help'
  if mode == 'save':
    os.makedirs(SNAPSHOTS, exist_ok=True)
    label = sys.argv[2]
    snap = capture()
    with open(os.path.join(SNAPSHOTS, label + '.json'), 'w') as f:
      json.dump(snap, f, indent=1)
    print('saved %s at %s' % (label, snap['sha']))
  elif mode == 'diff':
    with open(os.path.join(SNAPSHOTS, sys.argv[2] + '.json')) as f:
      before = json.load(f)
    after = capture()
    if len(sys.argv) > 3:
      with open(os.path.join(SNAPSHOTS, sys.argv[3] + '.json')) as f:
        after = json.load(f)
    report(before, after)
  else:
    print(__doc__ or 'usage: metrics.py save LABEL | diff LABEL [LABEL]')


if __name__ == '__main__':
  main()
