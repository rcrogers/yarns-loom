#!/usr/bin/env python3
"""Splice a compiled engine into a sim page.

The page carries the engine inline, so rebuilding chiff_engine.js does nothing
on its own -- without this step the sim silently goes on running whatever
firmware it was last spliced with. build.sh calls it automatically.

Usage: inline_engine.py <engine.js> <source.html> [output.html]
"""
import os
import re
import subprocess
import sys


def firmware_version():
    """git short SHA of HEAD, plus -dirty if the firmware/engine sources have
    uncommitted changes. Stamped into the page so a published artifact
    self-identifies its firmware (published snapshots have no other marker)."""
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        sha = subprocess.check_output(
            ['git', '-C', here, 'rev-parse', '--short', 'HEAD']).decode().strip()
    except Exception:
        return 'unknown'
    # ':/' magic prefix = repo-root-relative, so this works from any subdir
    # (git -C here makes plain paths relative to here, which misses them).
    tracked = [':/yarns/envelope.cc', ':/yarns/envelope.h',
               ':/yarns/resources.cc', ':/tools/simengine/engine.cc']
    dirty = subprocess.check_output(
        ['git', '-C', here, 'status', '--porcelain', '--'] + tracked).decode().strip()
    return sha + ('-dirty' if dirty else '')


def variant_label():
    """A human-readable name for what makes THIS build different, read from the
    source being built rather than typed in.

    A published A/B page is identified only by its firmware SHA, which tells a
    listener nothing about what they are hearing -- and the SHA is stamped from
    HEAD, so an amended commit leaves it pointing at a hash that no longer
    exists. Anything a human is asked to compare by ear needs a name.

    Returns '' when the build has no distinguishing constant, which is the
    canonical page: unlabelled is correct there, because it is the reference.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, '..', '..', 'yarns', 'envelope.cc')
    try:
        source = open(path).read()
    except Exception:
        return ''
    match = re.search(r'^const uint32_t kChiffSlowEndOctaves = (\d+);',
                      source, re.M)
    return 'slow-end cap %s oct' % match.group(1) if match else ''


def main():
    if not 3 <= len(sys.argv) <= 4:
        sys.exit(__doc__)
    engine_path, source, output = sys.argv[1], sys.argv[2], sys.argv[-1]
    html = open(source).read()
    engine = open(engine_path).read()
    blocks = list(re.finditer(r'<script>[\s\S]*?</script>', html))
    if len(blocks) != 2:
        sys.exit('expected 2 script blocks (engine + sim), got %d' % len(blocks))
    spliced = (html[:blocks[0].start()] + '<script>\n' + engine + '\n</script>'
               + html[blocks[0].end():])
    # Splice the shared loader, so the page cannot drift from the copy the node
    # checks use. Its module.exports tail is node-only, so it is dropped.
    loader = open(os.path.join(os.path.dirname(engine_path), 'loader.js')).read()
    loader = loader.split("if (typeof module !== 'undefined'")[0].rstrip()
    spliced, n = re.subn(
        r'// >>> loader\.js[^\n]*\n[\s\S]*?// <<< loader\.js',
        lambda m: ('// >>> loader.js -- spliced by tools/simengine/inline_engine.py, '
                   'do not edit here\n' + loader + '\n// <<< loader.js'),
        spliced)
    if n != 1:
        sys.exit('inline_engine: loader.js markers not found in the sim script')

    # Stamp the firmware version (idempotent: matches any prior value).
    version = firmware_version()
    spliced, n = re.subn(r"const FW_VERSION = '[^']*';",
                         "const FW_VERSION = '%s';" % version, spliced)
    if n != 1:
        sys.exit('inline_engine: FW_VERSION marker not found in the sim script')

    # OPTIONAL, unlike FW_VERSION: a page without the marker is simply not an
    # A/B variant, so a missing marker is not an error.
    spliced = re.sub(r"const FW_VARIANT = '[^']*';",
                     "const FW_VARIANT = '%s';" % variant_label(), spliced)
    open(output, 'w').write(spliced)
    print('inlined %s into %s (firmware %s)' % (engine_path, output, version))


if __name__ == '__main__':
    main()
