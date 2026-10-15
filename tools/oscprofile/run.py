# Profile under QEMU, from the host, after build.sh:
#   run.py [--force] [shape ...]   oscillator shapes (default: every one), into
#                                  shape<n>/
#   run.py [--force] env           the envelope grid, in ENVELOPE_PARTS parts,
#                                  into env<k>/
#
# A run is not made again when its summary already exists for this image, these
# tools and this job: in this worktree, or in the cache every worktree shares
# ($OSCPROFILE_CACHE, default ~/.cache/yarns-oscprofile), so a fresh worktree on
# a firmware some other one profiled reuses its runs. --force runs them anyway.
# The key is content, never a name: the image digest is recomputed here, and
# osc_profile.py / env_profile.py still check the code against the firmware.
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
#
# Several sessions at once:
#   - a job takes one of the cache's CPU slots, one per Docker CPU, so every
#     run on the machine together stays within Docker's CPUs;
#   - one run at a time in a worktree: a second waits for the first;
#   - a job whose trace stops for WATCHDOG_SECONDS fails the run;
#   - the container outlives a killed run by CONTAINER_SECONDS at most.
import fcntl
import hashlib
import json
import multiprocessing.pool
import os
import shutil
import subprocess
import sys
import threading
import time

import measured

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, 'build/oscprofile')
CACHE = os.environ.get('OSCPROFILE_CACHE',
                       os.path.expanduser('~/.cache/yarns-oscprofile'))
CACHE_DAYS = 30
IMAGE = 'oscprofile-qemu'
CONTAINER = 'oscprofile-%d' % os.getpid()
CONTAINER_SECONDS = 6 * 3600
WATCHDOG_SECONDS = 300
SHAPE_ENTRIES = r'^_ZN5yarns10Oscillator(\d+Render|10RenderLoop)'
ENVELOPE_ENTRIES = r'^_ZN5yarns8Envelope(6NoteOn|7NoteOff|13RenderSamples)E'
ENVELOPE_FLAGS = ['_ZN5yarns8Envelope18HandOffToNextStage']
# Fixed, not the CPU count: any split runs the same cases, and a fixed one
# keeps the cache's keys when Docker's CPUs change.
ENVELOPE_PARTS = 16
# What a run leaves, and what the cache keeps of it.
RESULTS = ('fold.json', 'qemu_out.txt')


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


def cache_entry(job, image, tools):
  return os.path.join(CACHE, 'results', image, tools,
                      hashlib.sha1(describe(job).encode()).hexdigest())


def from_cache(job, image, tools):
  entry = cache_entry(job, image, tools)
  if not all(os.path.exists(os.path.join(entry, name)) for name in RESULTS):
    return False
  path = os.path.join(OUT, job[0])
  os.makedirs(path, exist_ok=True)
  for name in RESULTS:
    shutil.copyfile(os.path.join(entry, name), os.path.join(path, name))
  with open(os.path.join(path, 'job.txt'), 'w') as job_file:
    job_file.write(describe(job))
  os.utime(entry)
  return True


def to_cache(job, image, tools):
  """Written beside the entry and renamed into place, so no session sees half
  of one."""
  entry = cache_entry(job, image, tools)
  if os.path.exists(entry):
    return
  staging = '%s.%d.%d' % (entry, os.getpid(), threading.get_ident())
  os.makedirs(staging)
  for name in RESULTS:
    shutil.copyfile(os.path.join(OUT, job[0], name), os.path.join(staging, name))
  try:
    os.rename(staging, entry)
  except OSError:
    shutil.rmtree(staging)


def prune_cache():
  """Entries unused for CACHE_DAYS go."""
  root = os.path.join(CACHE, 'results')
  oldest = time.time() - CACHE_DAYS * 86400
  for image in os.listdir(root) if os.path.isdir(root) else ():
    for tools in os.listdir(os.path.join(root, image)):
      for name in os.listdir(os.path.join(root, image, tools)):
        entry = os.path.join(root, image, tools, name)
        if os.path.getmtime(entry) < oldest:
          shutil.rmtree(entry, ignore_errors=True)


class Slot(object):
  """One of the machine's CPU slots, held across processes by a file lock."""

  def __init__(self, count):
    self.count = count
    os.makedirs(os.path.join(CACHE, 'slots'), exist_ok=True)

  def __enter__(self):
    while True:
      for index in range(self.count):
        handle = open(os.path.join(CACHE, 'slots', '%d.lock' % index), 'w')
        try:
          fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
          self.handle = handle
          return self
        except OSError:
          handle.close()
      time.sleep(0.5)

  def __exit__(self, *_):
    self.handle.close()


def profile(job, slots):
  """One QEMU run, its trace folded as it streams: (directory, filter,
  semihosting arguments, functions to count)."""
  directory, address_filter_text, arguments, flags = job
  path = os.path.join(OUT, directory)
  os.makedirs(path, exist_ok=True)
  for stale in ('job.txt',) + RESULTS + ('qemu.log',):
    if os.path.exists(os.path.join(path, stale)):
      os.remove(os.path.join(path, stale))
  with Slot(slots), open(os.path.join(path, 'qemu.log'), 'w') as log, \
       open(os.path.join(path, 'fold.json'), 'w') as summary:
    qemu = subprocess.Popen(
        ['docker', 'exec', '-i', '-w', '/workdir/build/oscprofile/' + directory,
         CONTAINER, 'bash', '-o', 'pipefail', '-c',
         'qemu-system-arm -M lm3s6965evb -display none -serial null -monitor none '
         '-semihosting -semihosting-config arg=prof,%s -kernel ../profile.elf '
         '-d exec,in_asm,nochain -dfilter %s -D /dev/stdout | gzip -1'
         % (arguments, address_filter_text)],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=log)
    gunzip = subprocess.Popen(['gzip', '-dc'], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE)
    fold = subprocess.Popen(
        [sys.executable, os.path.join(HERE, 'fold.py'),
         os.path.join(OUT, 'profile.dis'), os.path.join(OUT, 'profile.sym')] + flags,
        stdin=gunzip.stdout, stdout=summary)
    gunzip.stdout.close()
    # The trace passes through here so the watchdog can see it move.
    last_data = [time.time()]

    def pump():
      for chunk in iter(lambda: qemu.stdout.read1(1 << 16), b''):
        last_data[0] = time.time()
        gunzip.stdin.write(chunk)
      gunzip.stdin.close()

    pumping = threading.Thread(target=pump)
    pumping.start()
    while pumping.is_alive():
      pumping.join(5)
      if time.time() - last_data[0] > WATCHDOG_SECONDS:
        for process in (qemu, gunzip, fold):
          process.kill()
        raise RuntimeError('%s: no trace for %d s; see %s'
                           % (directory, WATCHDOG_SECONDS, os.path.join(path, 'qemu.log')))
    if fold.wait() or gunzip.wait() or qemu.wait():
      raise RuntimeError('%s failed; see %s' % (directory, os.path.join(path, 'qemu.log')))
  with open(os.path.join(path, 'job.txt'), 'w') as job_file:
    job_file.write(describe(job))


def main():
  arguments = [a for a in sys.argv[1:] if a != '--force']
  force = '--force' in sys.argv
  os.makedirs(OUT, exist_ok=True)
  worktree_lock = open(os.path.join(OUT, 'run.lock'), 'w')
  try:
    fcntl.flock(worktree_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
  except OSError:
    print('run.py: another run in this worktree; waiting for it')
    fcntl.flock(worktree_lock, fcntl.LOCK_EX)
  if arguments == ['env']:
    entries = address_filter(ENVELOPE_ENTRIES)
    for name in os.listdir(OUT):
      if (name.startswith('env') and name[3:].isdigit()
          and int(name[3:]) >= ENVELOPE_PARTS):
        shutil.rmtree(os.path.join(OUT, name))
    jobs = [('env%d' % part, entries, 'arg=env,arg=part=%d,arg=parts=%d'
             % (part, ENVELOPE_PARTS), ENVELOPE_FLAGS)
            for part in range(ENVELOPE_PARTS)]
  else:
    entries = address_filter(SHAPE_ENTRIES)
    shapes = arguments or range(int(tool(
        os.path.join(os.path.dirname(HERE), 'osc_profile.py'), '--shape-count')))
    jobs = [('shape%s' % shape, entries, 'arg=shape=%s' % shape, [])
            for shape in shapes]
  image = measured.pathcost.image_digest(os.path.join(OUT, 'profile.dis'))
  tools = measured.tools_digest()
  prune_cache()
  if not force:
    jobs = [job for job in jobs if not up_to_date(job, image, tools)]
    cached = [job for job in jobs if from_cache(job, image, tools)]
    jobs = [job for job in jobs if job not in cached]
    print('run.py: %d runs from the shared cache' % len(cached))
  print('run.py: %d runs to make' % len(jobs))
  if not jobs:
    return
  if subprocess.call(['docker', 'image', 'inspect', IMAGE],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
    subprocess.check_call(['docker', 'build', '-q', '-t', IMAGE, HERE],
                          stdout=subprocess.DEVNULL)
  subprocess.check_call(
      ['docker', 'run', '-d', '--rm', '--name', CONTAINER,
       '-v', '%s:/workdir' % ROOT, IMAGE, 'sleep', str(CONTAINER_SECONDS)],
      stdout=subprocess.DEVNULL)
  try:
    cpus = int(subprocess.check_output(['docker', 'exec', CONTAINER, 'nproc'],
                                       encoding='utf8'))

    def run(job):
      profile(job, cpus)
      to_cache(job, image, tools)
    multiprocessing.pool.ThreadPool(cpus).map(run, jobs)
  finally:
    subprocess.call(['docker', 'kill', CONTAINER], stdout=subprocess.DEVNULL)


main()
