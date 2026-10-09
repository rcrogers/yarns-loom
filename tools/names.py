# Two defects that keep reaching review by eye:
#   a declaration nothing uses, or that one site uses;
#   a comment naming an identifier the code no longer has.
#
#   python3 tools/names.py yarns/oscillator.h yarns/oscillator.cc
import collections
import glob
import os
import re
import sys

DECL = re.compile(
    r'^\s*(?:static\s+|inline\s+|const\s+)*'
    r'(?:void|bool|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|size_t)\s+'
    r'([A-Za-z_][A-Za-z0-9_]*)\s*\(')
MEMBER = re.compile(
    r'^\s*(?:static\s+)?(?:const\s+)?'
    r'(?:bool|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|size_t)\s+'
    r'([A-Za-z_][A-Za-z0-9_]*_)\s*;')
# Identifier-shaped words: snake_case with an underscore, or a k-prefixed name.
CODEISH = re.compile(r'\b(?:k[A-Z][A-Za-z0-9]+|[a-z][a-z0-9]*(?:_[a-z0-9]+)+_?)\b')


def strip_comments(text):
  return '\n'.join(re.sub(r'//.*', '', line) for line in text.splitlines())


def comments(text):
  return [(n, m.group(0)) for n, line in enumerate(text.splitlines(), 1)
          for m in [re.search(r'//(.*)', line)] if m]


def main():
  paths = sys.argv[1:]
  sources = {p: open(p).read() for p in paths}
  code = '\n'.join(strip_comments(t) for t in sources.values())
  # Vocabulary comes from the whole tree: a comment may name something another
  # file owns, and only a name NOTHING owns is a stale reference.
  root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
  tree = []
  for pattern in ('yarns/*.h', 'yarns/*.cc', 'yarns/drivers/*.h',
                  'yarns/tools/*.cc', 'tools/*/*.cc', 'stmlib/**/*.h'):
    for f in glob.glob(os.path.join(root, pattern), recursive=True):
      tree.append(strip_comments(open(f, errors='ignore').read()))
  tree_code = '\n'.join(tree)
  counts = collections.Counter(re.findall(r'[A-Za-z_][A-Za-z0-9_]*', tree_code))
  words = set(counts)

  print('declarations used once or not at all:')
  for path, text in sources.items():
    for n, line in enumerate(strip_comments(text).splitlines(), 1):
      for pattern in (DECL, MEMBER):
        m = pattern.match(line)
        if not m:
          continue
        name = m.group(1)
        uses = counts[name] - 1
        if uses <= 1:
          print('  %s:%d  %-34s %d use%s'
                % (path, n, name, uses, '' if uses == 1 else 's'))

  # A comment that names something this file never calls is explaining code it
  # does not own: the fact belongs where the thing is.
  print('comments naming identifiers this file never uses:')
  for path, text in sources.items():
    own = set(re.findall(r'[A-Za-z_][A-Za-z0-9_]*', strip_comments(text)))
    for n, comment in comments(text):
      for word in sorted(set(CODEISH.findall(comment))):
        if word in words and word not in own:
          print('  %s:%d  %s' % (path, n, word))

  print('identifiers named in comments that the code does not have:')
  for path, text in sources.items():
    for n, comment in comments(text):
      for word in set(CODEISH.findall(comment)):
        if word not in words:
          print('  %s:%d  %s' % (path, n, word))


if __name__ == '__main__':
  main()
