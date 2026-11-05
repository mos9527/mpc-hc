#!/usr/bin/env python3
"""ATL and MFC for the v143 toolset, which MPC-HC builds with under every Visual Studio.

src/platform.props maps VS 2026 to v143 as well (upstream #3946), and
LAVFilters/Directory.Build.props does the same for IntelQuickSyncDecoder. Visual
Studio 2026's "C++ ATL" and "C++ MFC" components only cover its default toolset
(v145); v143 has its own components, Microsoft.VisualStudio.Component.VC.14.4x.17.xx.ATL
and .MFC. Without them the build fails with MSB8041 ("MFC libraries are required")
or C1083 on atlbase.h. VS 2022, where v143 is the default, needs nothing extra.

  python ci/vstools.py            check, print what is missing (exit code 1 if anything)
  python ci/vstools.py --install  install what is missing (needs admin rights)

--install is meant for a local machine. On GitHub's VS 2026 runner image it ran for
more than ten minutes without finishing, so CI uses the VS 2022 image (which has
v143's ATL/MFC) and only runs the check.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
    "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
HEADERS = {"ATL": "atlbase.h", "MFC": "afxwin.h"}


class VsError(Exception):
    pass


def vswhere(*args):
    if not VSWHERE.is_file():
        raise VsError("vswhere.exe not found; is Visual Studio installed?")
    p = subprocess.run([str(VSWHERE), *args], capture_output=True, text=True, encoding="utf-8")
    if p.returncode != 0:
        raise VsError(f"vswhere failed: {p.stderr.strip()}")
    return p.stdout


def find_vs():
    path = vswhere("-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                   "-property", "installationPath").strip()
    if not path:
        raise VsError("no Visual Studio installation with the C++ x64 tools found")
    return Path(path)


def v143_status(vs=None):
    """(vs, toolset version, {"ATL": component or None, ...} for what is missing)."""
    vs = vs or find_vs()
    build = vs / "VC" / "Auxiliary" / "Build"
    ver_file = build / "Microsoft.VCToolsVersion.v143.default.txt"
    if not ver_file.is_file():
        # VS 2022, where v143 is the default toolset, may only have the generic file
        generic = build / "Microsoft.VCToolsVersion.default.txt"
        if generic.is_file() and re.match(r"14\.[34]\d\.", generic.read_text().strip()):
            ver_file = generic
        else:
            raise VsError("the v143 toolset (MSVC 14.3x/14.4x) is not installed; add 'MSVC v143 - VS 2022 C++ "
                          "x64/x86 build tools' in the Visual Studio Installer")
    ver = ver_file.read_text().strip()
    include = vs / "VC" / "Tools" / "MSVC" / ver / "atlmfc" / "include"
    missing = [lib for lib, header in HEADERS.items() if not (include / header).is_file()]
    if not missing:
        return vs, ver, {}
    # the toolset's own component, e.g. Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64
    m = re.match(r"(\d+)\.(\d+)\.", ver)
    packages = json.loads(vswhere("-path", str(vs), "-format", "json", "-include", "packages"))
    ids = [p["id"] for inst in packages for p in inst.get("packages", [])]
    base = None
    if m:
        pat = re.compile(rf"^(Microsoft\.VisualStudio\.Component\.VC\.{m.group(1)}\.{m.group(2)}\.\d+\.\d+)\.x86\.x64$")
        base = next((x.group(1) for x in map(pat.match, ids) if x), None)
    return vs, ver, {lib: (f"{base}.{lib}" if base else None) for lib in missing}


def install(vs, components):
    installer = VSWHERE.parent / "vs_installer.exe"
    args = [str(installer), "modify", "--installPath", str(vs)]
    for c in components:
        args += ["--add", c]
    args += ["--quiet", "--norestart", "--nocache"]
    print("running: " + " ".join(f'"{a}"' if " " in a else a for a in args), flush=True)
    # vs_installer hands the work to setup.exe and returns early; going through cmd /c start /wait
    # keeps this process alive until the installation has finished
    rc = subprocess.run(["cmd", "/c", "start", "/wait", '""', *args]).returncode
    if rc not in (0, 3010):  # 3010: success, reboot required
        raise VsError(f"the Visual Studio installer failed with exit code {rc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", action="store_true", help="install the missing components (needs admin rights)")
    args = ap.parse_args()
    try:
        vs, ver, missing = v143_status()
        if not missing:
            print(f"v143 ({ver}) has ATL and MFC")
            return 0
        print(f"v143 ({ver}) in {vs} lacks {', '.join(missing)}")
        comps = list(missing.values())
        if None in comps:
            raise VsError("cannot find the component id of the v143 toolset; add 'C++ ATL for v143' and "
                          "'C++ MFC for v143' in the Visual Studio Installer")
        if not args.install:
            installer = VSWHERE.parent / "setup.exe"
            print(f'install with (admin):\n  & "{installer}" modify --installPath "{vs}" '
                  + " ".join(f"--add {c}" for c in comps))
            return 1
        install(vs, comps)
        _, _, still = v143_status(vs)
        if still:
            raise VsError(f"still missing after the installation: {', '.join(still)}")
        print(f"installed {', '.join(comps)}")
        return 0
    except VsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
