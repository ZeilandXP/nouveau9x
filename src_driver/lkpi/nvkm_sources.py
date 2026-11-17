#!/usr/bin/env python3
"""Print the nouveau source list (nvkm core, then the nvif client library),
exactly as nouveau's own Kbuild files define it.

Reading Kbuild instead of globbing keeps the port honest about what upstream
builds. Nothing is excluded: the Tegra chips' files are compiled too, so the
chipset table links unmodified, with the Tegra platform services stubbed in
lkpi.h.
"""
import pathlib
import re
import sys


def walk(root, kbuild, out):
    for line in (root / kbuild).read_text().splitlines():
        m = re.match(r"include \$\(src\)/(\S+)", line)
        if m:
            walk(root, m.group(1), out)
            continue
        m = re.match(r"nvkm-y\s*[:+]?=\s*(\S+)\.o", line)
        if m:
            out.append(m.group(1) + ".c")
            continue
        m = re.match(r"nvif-y\s*[:+]?=\s*(\S+)\.o", line)
        if m:
            out.append(m.group(1) + ".c")


def main():
    root = pathlib.Path(sys.argv[1])
    out = []
    walk(root, "nvkm/Kbuild", out)
    walk(root, "nvif/Kbuild", out)
    missing = [p for p in out if not (root / p).exists()]
    if missing:
        sys.exit("Kbuild names missing files: " + ", ".join(missing))
    if len(out) < 500:
        sys.exit("only %d sources found; Kbuild format changed?" % len(out))
    print("\n".join(out))


if __name__ == "__main__":
    main()
