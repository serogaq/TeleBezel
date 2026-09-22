#!/usr/bin/env python3
"""Analyze every adapter translation unit, excluding vendored dependency sources."""
import json
import pathlib
import re
import shlex
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
build = pathlib.Path(sys.argv[1]).resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
entries = [entry for entry in commands if pathlib.Path(entry['file']).resolve().parent == root / 'src']
expected = set((root / 'src').glob('*.cpp'))
if {pathlib.Path(entry['file']).resolve() for entry in entries} != expected:
    raise SystemExit('Incomplete adapter compilation database')
for entry in entries:
    flags = entry.get('arguments') or shlex.split(entry['command'])
    if not {'-Wall', '-Wextra', '-Werror'}.issubset(flags):
        raise SystemExit('Missing strict compiler flags: ' + entry['file'])
filtered = build / 'adapter-compile-commands.json'
filtered.write_text(json.dumps(entries))
failed = False
for source in sorted(expected):
    print('clang-tidy: ' + str(source), flush=True)
    result = subprocess.run(['clang-tidy', str(source), '-p', str(build),
                    '--checks=clang-analyzer-*,bugprone-*,performance-*,-bugprone-easily-swappable-parameters',
                    '--header-filter=^' + re.escape(str(root / 'include')) + '/',
                    '--warnings-as-errors=*'], check=False)
    print('clang-tidy exit: ' + str(result.returncode), flush=True)
    failed = failed or result.returncode != 0
result = subprocess.run(['cppcheck', '--project=' + str(filtered), '--std=c++20',
                '--enable=warning,performance,portability', '--error-exitcode=1',
                '--inline-suppr', '--suppress=missingIncludeSystem',
                # cppcheck fails to resolve nlohmann's number_float_t alias to double.
                '--suppress=invalidPrintfArgType_float:*/nlohmann/detail/output/serializer.hpp',
                # Upstream lexer intentionally returns a position snapshot by value.
                '--suppress=returnByReference:*/nlohmann/detail/input/lexer.hpp',
                '--check-level=exhaustive'], check=False)
print('cppcheck exit: ' + str(result.returncode), flush=True)
sys.exit(1 if failed or result.returncode != 0 else 0)
