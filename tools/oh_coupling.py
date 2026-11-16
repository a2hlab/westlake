#!/usr/bin/env python3
"""Per component directory: source files and lines that reference OpenHarmony platform APIs.

usage: tools/oh_coupling.py [westlake checkout, default .]

A heuristic for where a second platform backend has work (docs/DEBIAN-PORT-PLAN-2026-11-11.md, section 2):
OH NDK headers, OH_* and OHOS:: symbols, ohos.* names, hilog, samgr, RenderService, access tokens and appspawn.
Westlake's own OH_* macros count too; they mark OH-only paths. historical/ and third_party/ are left out.
"""
import collections
import os
import re
import sys

PATTERN = re.compile(
    r'#\s*include\s*[<"](native_window|native_buffer|native_vsync|native_image|native_drawing|ohaudio|multimedia|hilog|'
    r'window_manager|bundle|AbilityKit|ability|ipc|accesstoken|napi|ace|render_service|syscap|parameter|samgr)[/._a-z]'
    r'|\bOH_[A-Za-z]|\bOHOS::|\bohos[._:]|\bOHNativeWindow|\bHiLog|\bhilog\b|\bsamgr\b|\bAccessToken|RenderService|\bRSNode\b|'
    r'\bRSSurface|\bappspawn\b|\bAbilityRuntime|\bBundleMgr|\bOHServiceManager|\bOH[A-Z][A-Za-z]*Adapter')
EXTENSIONS = ('.c', '.cc', '.cpp', '.h', '.hpp', '.java', '.kt', '.S', '.py', '.sh')
TOPS = ('framework', 'native', 'apps', 'tools', 'framework-tools')
SKIP = {'.git', 'historical', 'third_party', 'build', 'out'}


def component(root, path):
    """framework/<dir> and native/<dir> per subdirectory; anything else by its top directory."""
    parts = os.path.relpath(path, root).split(os.sep)
    if parts[0] in ('framework', 'native') and len(parts) > 2:
        return parts[0] + '/' + parts[1]
    return parts[0]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    stats = collections.defaultdict(lambda: [0, 0, 0, 0])  # files, lines, files touching OH, OH lines
    for top in TOPS:
        for dirpath, dirs, files in os.walk(os.path.join(root, top)):
            dirs[:] = [d for d in dirs if d not in SKIP]
            for name in files:
                if not name.endswith(EXTENSIONS):
                    continue
                path = os.path.join(dirpath, name)
                if os.path.abspath(path) == os.path.abspath(__file__):
                    continue  # its own patterns would count
                try:
                    with open(path, encoding='utf-8', errors='replace') as stream:
                        lines = stream.read().splitlines()
                except OSError:
                    continue
                hits = sum(1 for line in lines if PATTERN.search(line))
                entry = stats[component(root, path)]
                entry[0] += 1
                entry[1] += len(lines)
                entry[2] += 1 if hits else 0
                entry[3] += hits
    print('%-34s %6s %8s %9s %8s' % ('component', 'files', 'lines', 'OH files', 'OH lines'))
    for name, (files, lines, oh_files, oh_lines) in sorted(stats.items(), key=lambda item: -item[1][3]):
        print('%-34s %6d %8d %9d %8d' % (name, files, lines, oh_files, oh_lines))
    total = [sum(column) for column in zip(*stats.values())]
    print('%-34s %6d %8d %9d %8d' % ('total', *total))


if __name__ == '__main__':
    main()
