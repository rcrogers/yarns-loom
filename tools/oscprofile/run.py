# Profile under QEMU, from the host, after build.sh:
#   run.py [shape ...]   oscillator shapes (default: every one), into shape<n>/
#   run.py env           the envelope grid, split across the cores, into env<k>/
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
import multiprocessing.pool
import os
import subprocess
import sys

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


def profile(job):
  """One QEMU run, its trace folded as it streams: (directory, filter,
  semihosting arguments, functions to count)."""
  directory, address_filter_text, arguments, flags = job
  path = os.path.join(OUT, directory)
  os.makedirs(path, exist_ok=True)
  for stale in ('fold.json', 'qemu_out.txt', 'qemu.log'):
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


def main():
  arguments = sys.argv[1:]
  if subprocess.call(['docker', 'image', 'inspect', IMAGE],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
    subprocess.check_call(['docker', 'build', '-q', '-t', IMAGE, HERE],
                          stdout=subprocess.DEVNULL)
  subprocess.check_call(
      ['docker', 'run', '-d', '--rm', '--name', CONTAINER,
       '-v', '%s:/workdir' % ROOT, IMAGE, 'sleep', 'infinity'],
      stdout=subprocess.DEVNULL)
  try:
    cpus = int(subprocess.check_output(['docker', 'exec', CONTAINER, 'nproc'],
                                       encoding='utf8'))
    if arguments == ['env']:
      entries = address_filter(ENVELOPE_ENTRIES)
      for name in os.listdir(OUT):
        if name.startswith('env') and name[3:].isdigit() and int(name[3:]) >= cpus:
          subprocess.check_call(['rm', '-rf', os.path.join(OUT, name)])
      jobs = [('env%d' % part, entries,
               'arg=env,arg=part=%d,arg=parts=%d' % (part, cpus), ENVELOPE_FLAGS)
              for part in range(cpus)]
    else:
      entries = address_filter(SHAPE_ENTRIES)
      shapes = arguments or range(int(tool(
          os.path.join(os.path.dirname(HERE), 'osc_profile.py'), '--shape-count')))
      jobs = [('shape%s' % shape, entries, 'arg=shape=%s' % shape, [])
              for shape in shapes]
    multiprocessing.pool.ThreadPool(cpus).map(profile, jobs)
  finally:
    subprocess.call(['docker', 'kill', CONTAINER], stdout=subprocess.DEVNULL)


main()
