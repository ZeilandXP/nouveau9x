#!/usr/bin/env python3
"""Check a miniport PE: native subsystem, XP-era version, and an import
allowlist. A video miniport may import only from videoprt.sys and ntoskrnl.exe
(HAL imports would not load under videoprt on XP x64), and every ntoskrnl name
must be one this project has confirmed exists on XP. Extend ALLOWED when a new
import is deliberately introduced."""
import re
import subprocess
import sys

ALLOWED = {
    "videoprt.sys": None,  # any VideoPort* export
    "ntoskrnl.exe": {"KeGetCurrentIrql", "ZwCreateFile", "ZwWriteFile", "strlen"},
}


def main(paths):
    failed = False
    for path in paths:
        tool = None
        for candidate in ("x86_64-w64-mingw32-objdump", "i686-w64-mingw32-objdump"):
            if "file format pei-" in subprocess.run([candidate, "-f", path], text=True,
                                                    capture_output=True).stdout:
                tool = candidate
                break
        if not tool:
            sys.exit(f"{path}: not a PE image")
        dump = subprocess.run([tool, "-p", path], text=True, capture_output=True, check=True).stdout
        is64 = "PE32+" in dump or "pei-x86-64" in subprocess.run(
            [tool, "-f", path], text=True, capture_output=True).stdout
        major = int(re.search(r"MajorSubsystemVersion\s+(\d+)", dump).group(1))
        minor = int(re.search(r"MinorSubsystemVersion\s+(\d+)", dump).group(1))
        if "(NT native)" not in dump:
            print(f"{path}: subsystem is not NT native"); failed = True
        if (major, minor) != ((5, 2) if is64 else (5, 1)):
            print(f"{path}: subsystem version {major}.{minor} is not the XP target"); failed = True
        dll = None
        count = 0
        section = dump.split("The Import Tables", 1)[1].split("PE File Base Relocations", 1)[0]
        for line in section.splitlines():
            m = re.search(r"DLL Name: (\S+)", line)
            if m:
                dll = m.group(1).lower()
                if dll not in ALLOWED:
                    print(f"{path}: imports from unexpected module {dll}"); failed = True
                continue
            m = re.match(r"\s+[0-9a-f]+\s+(?:<none>\s+)?[0-9a-f]+\s+(\w+)\s*$", line)
            if m and dll:
                name = re.sub(r"@\d+$", "", m.group(1)).lstrip("_")
                count += 1
                allowed = ALLOWED.get(dll)
                if dll == "videoprt.sys" and not name.startswith("VideoPort"):
                    print(f"{path}: odd videoprt import {name}"); failed = True
                elif allowed is not None and name not in allowed:
                    print(f"{path}: ntoskrnl import {name} is not on the allowlist"); failed = True
        if not count:
            print(f"{path}: no imports parsed; objdump format changed?"); failed = True
        print(f"{path}: {count} imports checked, subsystem {major}.{minor}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
