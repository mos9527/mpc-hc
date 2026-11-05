#!/usr/bin/env python3
"""Compile the x64 installer (distrib/mpc-hc_setup.iss) from bin/mpc-hc_x64.

  python ci/installer.py [--out DIR]

The installer is named like the package (see ci/package.py) and goes to bin/ci without
--out. It is not signed, as CI has no signing certificate.

The script takes the MPC Video Renderer and MediaInfo.dll from distrib\\, and silently
leaves out whichever it does not find there. ci/extras.py downloads them into the build
output, so they are copied over (both destinations are ignored by git, and any copies
already there are replaced). The compiler's output is then checked for what must have
been included.
"""

import argparse
import shutil
import subprocess
import sys
import winreg
from pathlib import Path

import package  # ci/package.py, next to this script

ROOT = package.ROOT
BIN_DIR = package.BIN_DIR
SCRIPT = ROOT / "distrib" / "mpc-hc_setup.iss"
# Where the script looks for the files it treats as optional
STAGED = {
    "MPCVR/MpcVideoRenderer64.ax": ROOT / "distrib" / "mpcvr" / "MpcVideoRenderer64.ax",
    "MediaInfo.dll": ROOT / "distrib" / "x64" / "MediaInfo.dll",
}
INNO_KEY = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"


def find_iscc():
    """The Inno Setup 6 compiler, found the way common.bat finds it, or from PATH."""
    for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, INNO_KEY, 0, winreg.KEY_READ | view) as key:
                iscc = Path(winreg.QueryValueEx(key, "Inno Setup: App Path")[0]) / "ISCC.exe"
        except OSError:
            continue
        if iscc.is_file():
            return str(iscc)
    iscc = shutil.which("iscc")
    if not iscc:
        package.die("Inno Setup 6 not found (no ISCC.exe in its install location or on PATH)")
    return iscc


def compile_installer(iscc, out, name):
    """Run the compiler, echoing its output; returns that output, or exits if it failed."""
    cmd = [iscc, "/Dx64Build", f"/O{out}", f"/F{name}", str(SCRIPT)]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace")
    output = []
    for line in proc.stdout:
        print(line, end="", flush=True)
        output.append(line)
    if proc.wait() != 0:
        package.die(f"ISCC failed with exit code {proc.returncode}, see its output above")
    return "".join(output)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="output directory (default: bin/ci)")
    args = ap.parse_args()

    required = package.required_files()
    iscc = find_iscc()
    for src, dst in STAGED.items():
        package.copy(BIN_DIR / src, dst)

    out = Path(args.out).resolve() if args.out else ROOT / "bin" / "ci"
    out.mkdir(parents=True, exist_ok=True)
    name = package.package_name()
    output = compile_installer(iscc, out, name).lower()

    # The compiler lists each file it compresses
    omitted = [f for f in required if Path(f).name.lower() not in output]
    if omitted:
        package.die(f"not in the installer: {', '.join(omitted)}")
    setup = out / f"{name}.exe"
    if not setup.is_file():
        package.die(f"ISCC succeeded, but {setup} does not exist")
    print(f"{setup.name}: {setup.stat().st_size / 2**20:.1f} MiB -> {out}")


if __name__ == "__main__":
    main()
