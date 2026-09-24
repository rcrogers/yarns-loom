#!/usr/bin/env python3
"""Report worst-case cumulative stack per function using puncover's collector.

Usage:
  stack_report.py ELF BUILD_DIR [SRC_ROOT]

Emits top-N call chains sorted by total stack usage.
"""
import sys
from pathlib import Path
from puncover.collector import Collector
from puncover.gcc_tools import GCCTools


def main():
    elf_file = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    src_root = Path(sys.argv[3] if len(sys.argv) > 3 else '.')

    tools = GCCTools('/usr/local/arm-4.8.3/bin/arm-none-eabi-')
    c = Collector(tools)
    c.parse_elf(elf_file)
    c.parse_su_dir(build_dir)
    c.enhance(src_root)

    # For each function, compute the worst-case stack by walking the call
    # graph recursively. stack_size attribute is per-function; we need to
    # traverse 'callees'.
    worst = {}  # sym -> (total_stack, path)

    def walk(sym, visited):
        if sym['address'] in worst:
            return worst[sym['address']]
        own = sym.get('stack_size') or 0
        # Dynamic ("unbounded") VLAs show as None / no 'stack_size'; skip.
        if not isinstance(own, int):
            own = 0
        visited = visited | {sym['address']}
        best_child_total = 0
        best_child_path = []
        for callee in sym.get('callees', []) or []:
            if callee['address'] in visited:
                continue  # recursion — skip
            sub_total, sub_path = walk(callee, visited)
            if sub_total > best_child_total:
                best_child_total = sub_total
                best_child_path = sub_path
        total = own + best_child_total
        path = [(sym.get('name', '?'), own)] + best_child_path
        worst[sym['address']] = (total, path)
        return total, path

    results = []
    for sym in c.all_functions():
        total, path = walk(sym, set())
        results.append((total, sym.get('name', '?'), path))

    results.sort(key=lambda r: r[0], reverse=True)
    print('Top 15 worst-case cumulative stack depths:')
    for total, name, path in results[:15]:
        print(f'\n{total:5d} bytes  {name}')
        for p_name, p_own in path:
            print(f'                 + {p_own:4d}  {p_name}')


if __name__ == '__main__':
    main()
