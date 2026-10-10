# Profile under QEMU, from the host, after build.sh:
#   run.py [--force] [shape ...]   oscillator shapes (default: every one), into
#                                  shape<n>/
#   run.py [--force] env           the envelope grid, split across the cores,
#                                  into env<k>/
#
# A run whose summary was made from this image, by these tools, for this job,
# is up to date and is not rerun; --force reruns it anyway.
#
# QEMU runs in one container for the whole run, a `docker exec` a job, from
# an image of QEMU alone for the host's own architecture (Dockerfile); each
# trace is gzipped there and folded here by fold.py:
#   - nothing runs emulated: the toolchain image is amd64, and on an arm64
#     host its QEMU and Python run several times slower;
#   - the trace is text gzip shrinks sixty times over, and the container's
#     stdout carries a few MB a second;
#   - an exec costs a fraction of a second, a new container several.
# QEMU logs only the code a measured call can reach (ranges.py says why that
# loses nothing): the rest is the harness preparing each call.
import json
import multiprocessing.pool
import os
import subprocess
import sys

import measured

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, 'build/oscprofile')
IMAGE = 'oscprofile-qemu'
CONTAINER = 'oscprofile-%d' % os.getpid()
SHAPE_ENTRIES = r'^_ZN5yarns10Oscillator(\d+Render|10RenderLoop)'
ENVELOPE_ENTRIES = r'^_ZN5yarns8Envelope(6NoteOn|7NoteOff|13RenderSamples)E'
ENVELOPE_FLAGS = ['_ZN5yarns8Envelope18HandOffToNextStage']


def tool(*arguments):
  return subprocess.check_output([sys.executable] + list(arguments), encoding='utf8')


def address_filter(entries):
  return tool(os.path.join(HERE, 'ranges.py'), os.path.join(OUT, 'profile.dis'),
              os.path.join(OUT, 'profile.sym'), entries).strip()


def describe(job):
  directory, _, arguments, flags = job
  return ' '.join([arguments] + flags)


def up_to_date(job, image, tools):
  path = os.path.join(OUT, job[0])
  try:
    if open(os.path.join(path, 'job.txt')).read() != describe(job):
      return False
    fold = json.load(open(os.path.join(path, 'fold.json')))
  except (IOError, ValueError):
    return False
  return (fold.get('image') == image and fold.get('tools') == tools
          and not fold['violation_count'])


def profile(job):
  """One QEMU run, its trace folded as it streams: (directory, filter,
  semihosting arguments, functions to count)."""
  directory, address_filter_text, arguments, flags = job
  path = os.path.join(OUT, directory)
  os.makedirs(path, exist_ok=True)
  for stale in ('job.txt', 'fold.json', 'qemu_out.txt', 'qemu.log'):
    if os.path.exists(os.path.join(path, stale)):
      os.remove(os.path.join(path, stale))
  with open(os.path.join(path, 'qemu.log'), 'w') as log, \
       open(os.path.join(path, 'fold.json'), 'w') as summary:
    qemu = subprocess.Popen(
        ['docker', 'exec', '-i', '-w', '/workdir/build/oscprofile/' + directory,
         CONTAINER, 'bash', '-o', 'pipefail', '-c',
         'qemu-system-arm -M lm3s6965evb -display none -serial null -monitor none '
         '-semihosting -semihosting-config arg=prof,%s -kernel ../profile.elf '
         '-d exec,in_asm,nochain -dfilter %s -D /dev/stdout | gzip -1'
         % (arguments, address_filter_text)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=log)
    gunzip = subprocess.Popen(['gzip', '-dc'], stdin=qemu.stdout,
                              stdout=subprocess.PIPE)
    qemu.stdout.close()
    fold = subprocess.Popen(
        [sys.executable, os.path.join(HERE, 'fold.py'),
         os.path.join(OUT, 'profile.dis'), os.path.join(OUT, 'profile.sym')] + flags,
        stdin=gunzip.stdout, stdout=summary)
    gunzip.stdout.close()
    if fold.wait() or gunzip.wait() or qemu.wait():
      raise RuntimeError('%s failed; see %s' % (directory, os.path.join(path, 'qemu.log')))
  with open(os.path.join(path, 'job.txt'), 'w') as job_file:
    job_file.write(describe(job))


def main():
  arguments = [a for a in sys.argv[1:] if a != '--force']
  cpus = lambda: int(subprocess.check_output(
      ['docker', 'info', '--format', '{{.NCPU}}'], encoding='utf8'))
  if arguments == ['env']:
    parts = cpus()
    entries = address_filter(ENVELOPE_ENTRIES)
    for name in os.listdir(OUT):
      if name.startswith('env') and name[3:].isdigit() and int(name[3:]) >= parts:
        subprocess.check_call(['rm', '-rf', os.path.join(OUT, name)])
    jobs = [('env%d' % part, entries,
             'arg=env,arg=part=%d,arg=parts=%d' % (part, parts), ENVELOPE_FLAGS)
            for part in range(parts)]
  else:
    entries = address_filter(SHAPE_ENTRIES)
    shapes = arguments or range(int(tool(
        os.path.join(os.path.dirname(HERE), 'osc_profile.py'), '--shape-count')))
    jobs = [('shape%s' % shape, entries, 'arg=shape=%s' % shape, [])
            for shape in shapes]
  if '--force' not in sys.argv:
    image = measured.pathcost.image_digest(os.path.join(OUT, 'profile.dis'))
    tools = measured.tools_digest()
    jobs = [job for job in jobs if not up_to_date(job, image, tools)]
  print('run.py: %d runs to make' % len(jobs))
  if not jobs:
    return
  if subprocess.call(['docker', 'image', 'inspect', IMAGE],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
    subprocess.check_call(['docker', 'build', '-q', '-t', IMAGE, HERE],
                          stdout=subprocess.DEVNULL)
  subprocess.check_call(
      ['docker', 'run', '-d', '--rm', '--name', CONTAINER,
       '-v', '%s:/workdir' % ROOT, IMAGE, 'sleep', 'infinity'],
      stdout=subprocess.DEVNULL)
  try:
    multiprocessing.pool.ThreadPool(cpus()).map(profile, jobs)
  finally:
    subprocess.call(['docker', 'kill', CONTAINER], stdout=subprocess.DEVNULL)


main()
