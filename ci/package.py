#!/usr/bin/env python3
"""Assemble the x64 player package from bin/mpc-hc_x64, like build.bat's 7z package.

  python ci/package.py [--out DIR] [--pdb-out DIR] [--github-output]

Without --out, the package goes to bin/ci/<name>, where <name> is
MPC-HC.<major>.<minor>.<patch>.<rev>-<hash>.x64 taken from the version headers
that update_version.bat writes. --github-output also exports that name as the
'name' output of the current GitHub Actions step.
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = ROOT / "bin" / "mpc-hc_x64"
LAV_OUT = "LAVFilters64"

REQUIRED = ["mpc-hc64.exe", "mpciconlib.dll", f"{LAV_OUT}/LAVSplitter.ax",
            f"{LAV_OUT}/LAVVideo.ax", f"{LAV_OUT}/LAVAudio.ax"]
OPTIONAL = ["d3dx9_43.dll"]  # bundled with the prebuilt LAV Filters, see ci/prebuilt.py


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def defines(path):
    if not path.is_file():
        return {}
    text = path.read_text(encoding="utf-8", errors="replace")
    return dict(re.findall(r'^#define\s+(\w+)\s+(.+?)\s*$', text, re.M))


def package_name():
    fixed = defines(ROOT / "include" / "version.h")
    rev = defines(ROOT / "build" / "version_rev.h")
    try:
        ver = ".".join(fixed[k] for k in ("MPC_VERSION_MAJOR", "MPC_VERSION_MINOR", "MPC_VERSION_PATCH"))
    except KeyError:
        die("cannot read the version from include/version.h")
    ver += "." + rev.get("MPC_VERSION_REV", "0")
    m = re.search(r'"(\w+)"', rev.get("MPCHC_HASH", ""))
    if m:
        ver += "-" + m.group(1)
    return f"MPC-HC.{ver}.x64"


def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="package directory (default: bin/ci/<name>)")
    ap.add_argument("--pdb-out", help="also collect the debug symbols into this directory")
    ap.add_argument("--github-output", action="store_true", help="write name=<name> to $GITHUB_OUTPUT")
    args = ap.parse_args()

    missing = [f for f in REQUIRED if not (BIN_DIR / f).is_file()]
    if missing:
        die(f"missing from {BIN_DIR}: {', '.join(missing)}")

    name = package_name()
    out = Path(args.out) if args.out else ROOT / "bin" / "ci" / name
    if out.exists():
        shutil.rmtree(out)

    for f in REQUIRED[:2] + [f for f in OPTIONAL if (BIN_DIR / f).is_file()]:
        copy(BIN_DIR / f, out / f)
    for pattern in ("*.ax", "*.dll", "*.manifest"):
        for f in (BIN_DIR / LAV_OUT).glob(pattern):
            copy(f, out / LAV_OUT / f.name)
    for sub, shaders in (("Shaders", "dx9"), ("Shaders11", "dx11")):
        for f in (ROOT / "src" / "mpc-hc" / "res" / "shaders" / shaders).glob("*.hlsl"):
            copy(f, out / sub / f.name)
    copy(ROOT / "COPYING.txt", out / "COPYING.txt")

    if args.pdb_out:
        pdb_out = Path(args.pdb_out)
        if pdb_out.exists():
            shutil.rmtree(pdb_out)
        for f in BIN_DIR.glob("*.pdb"):
            copy(f, pdb_out / f.name)

    count = sum(1 for p in out.rglob("*") if p.is_file())
    print(f"{name}: {count} files -> {out}")
    if args.github_output:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as f:
            f.write(f"name={name}\n")


if __name__ == "__main__":
    main()
