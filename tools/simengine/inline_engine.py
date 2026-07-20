#!/usr/bin/env python3
"""Splice a compiled engine into a sim page.

The page carries the engine inline, so rebuilding chiff_engine.js does nothing
on its own -- without this step the sim silently goes on running whatever
firmware it was last spliced with. build.sh calls it automatically.

Usage: inline_engine.py <engine.js> <source.html> [output.html]
"""
import re
import sys


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
    open(output, 'w').write(spliced)
    print('inlined %s into %s' % (engine_path, output))


if __name__ == '__main__':
    main()
