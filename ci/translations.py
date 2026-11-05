#!/usr/bin/env python3
"""Generate the translated resource scripts that mpcresources.sln compiles.

  python ci/translations.py

The mpc-hc.<lang>.rc files are not in the repository: sync.py generates them from
src/mpc-hc/mpc-hc.rc and the PO files, and rewrites the tracked PO/POT files on the way
(the same thing the SyncTranslations project does in a local build). It needs polib.

CI runs this as its own step and then builds only the BuildAll project. The solution
would otherwise run the sync next to the compilation, as nothing there orders the two.
The generated files are then checked, as a sync that matches nothing still looks
successful: the result would compile into DLLs that are simply not translated.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES_DIR = ROOT / "src" / "mpc-hc" / "mpcresources"
UNTRANSLATED = "LANGUAGE LANG_ENGLISH, SUBLANG_ENGLISH_US"


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def check_generated(res_dir=RES_DIR):
    """Every language in PO must have an .rc that sync.py actually localized; returns the count."""
    languages = sorted(f.name.split(".")[1] for f in (res_dir / "PO").glob("mpc-hc.*.menus.po"))
    if not languages:
        die(f"no languages found in {res_dir / 'PO'}")
    missing, untranslated = [], []
    for lang in languages:
        rc = res_dir / f"mpc-hc.{lang}.rc"
        if not rc.is_file():
            missing.append(rc.name)
        elif UNTRANSLATED in rc.read_text(encoding="utf-16", errors="replace"):
            untranslated.append(rc.name)
    if missing:
        die(f"not generated: {', '.join(missing)}")
    if untranslated:
        die(f"not localized: {', '.join(untranslated)}")
    return len(languages)


def main():
    if subprocess.run([sys.executable, "sync.py"], cwd=RES_DIR).returncode != 0:
        die("sync.py failed, see its output above (it needs polib: pip install polib)")
    print(f"{check_generated()} languages generated in {RES_DIR}")


if __name__ == "__main__":
    main()
