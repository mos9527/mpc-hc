#!/usr/bin/env python3
"""Third-party runtime files the player loads but this repository does not build or contain.

  MPCVR/MpcVideoRenderer64.ax   MPC Video Renderer, from its own GitHub releases
                                (Aleksoid1978/VideoRenderer); without it the renderer is
                                listed as **unavailable** in Options > Output
  MediaInfo.dll                 MediaInfoLib, from upstream's stable package (MediaArea's
                                GitHub releases carry no binaries); the MediaInfo tab of
                                the file properties needs it

Each source is pinned in ci/extras.json to a release tag, an asset and the asset's
SHA-256, so builds are reproducible and a new upstream release never changes a build
by itself.

  python ci/extras.py [--dest DIR]       download, verify and unpack into bin/mpc-hc_x64
  python ci/extras.py --update [NAME..]  pin the latest release of NAME (default: all),
                                         rewriting ci/extras.json; commit the result
  python ci/extras.py --check            only report sources with a newer release

Fetching also reports a newer release, as a notice in GitHub Actions; it never fails
the build for that.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN_DIR = ROOT / "bin" / "mpc-hc_x64"
PINS = Path(__file__).resolve().parent / "extras.json"


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def gh(*args, check=True):
    if not shutil.which("gh"):
        die("the GitHub CLI (gh) is not on PATH")
    p = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if check and p.returncode != 0:
        die(f"'gh {' '.join(args)}' failed: {p.stderr.strip()}")
    return p


def load_pins():
    return json.loads(PINS.read_text(encoding="utf-8"))


def save_pins(pins):
    PINS.write_text(json.dumps(pins, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def latest_release(src):
    """(tag, asset name) of the repository's latest stable release, or None."""
    p = gh("release", "view", "-R", src["repo"], "--json", "tagName,assets", check=False)
    if p.returncode != 0:
        return None
    info = json.loads(p.stdout)
    pattern = re.compile(src["asset_pattern"])
    assets = [a["name"] for a in info["assets"] if pattern.match(a["name"])]
    if len(assets) != 1:
        die(f"{src['repo']} {info['tagName']}: expected one asset matching {src['asset_pattern']}, found {assets}")
    return info["tagName"], assets[0]


def download(src, tag, asset, tmp):
    gh("release", "download", tag, "-R", src["repo"], "-p", asset, "-D", tmp, "--clobber")
    return Path(tmp) / asset


def zip_members(z, src, label):
    """Map each wanted file to its zip member (case-insensitive, at the root or below a top folder)."""
    names = {n.lower(): n for n in z.namelist() if not n.endswith("/")}
    found = {}
    for want in src["files"]:
        hits = [orig for low, orig in names.items() if low == want.lower() or low.endswith("/" + want.lower())]
        if len(hits) != 1:
            die(f"{label}: {len(hits)} entries match {want}")
        found[want] = hits[0]
    return found


def fetch(pins, dest):
    for name, src in pins.items():
        label = f"{name} ({src['repo']} {src['tag']})"
        with tempfile.TemporaryDirectory() as tmp:
            archive = download(src, src["tag"], src["asset"], tmp)
            digest = sha256(archive)
            if digest != src["sha256"]:
                die(f"{label}: {src['asset']} has SHA-256 {digest}, pinned {src['sha256']}")
            with zipfile.ZipFile(archive) as z:  # closed before the temp dir is removed
                for want, member in zip_members(z, src, label).items():
                    target = dest / src["files"][want]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(z.read(member))
                    print(f"{label}: {src['files'][want]}")


def check(pins, names):
    """Report sources with a newer release; returns the names that have one."""
    newer = []
    for name in names:
        src = pins[name]
        latest = latest_release(src)
        if latest is None:
            print(f"{name}: cannot query the latest release of {src['repo']}", file=sys.stderr)
        elif latest[0] != src["tag"]:
            newer.append(name)
            msg = (f"{name}: {src['repo']} has a newer release {latest[0]} (pinned {src['tag']}); "
                   f"pin it with 'python ci/extras.py --update {name}'")
            print(f"::notice::{msg}" if os.environ.get("GITHUB_ACTIONS") else msg)
    return newer


def update(pins, names):
    changed = False
    for name in names:
        src = pins[name]
        latest = latest_release(src)
        if latest is None:
            die(f"{name}: cannot query the latest release of {src['repo']}")
        tag, asset = latest
        if tag == src["tag"] and asset == src["asset"]:
            print(f"{name}: {src['repo']} {tag} is already pinned")
            continue
        with tempfile.TemporaryDirectory() as tmp:
            archive = download(src, tag, asset, tmp)
            with zipfile.ZipFile(archive) as z:
                zip_members(z, src, f"{name} ({src['repo']} {tag})")  # the new asset must still have the files
            digest = sha256(archive)
        print(f"{name}: {src['repo']} {src['tag']} -> {tag} ({asset}, sha256 {digest})")
        src.update(tag=tag, asset=asset, sha256=digest)
        changed = True
    if changed:
        save_pins(pins)
        print(f"updated {PINS.relative_to(ROOT)}; review and commit it")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dest", help=f"output directory (default: {BIN_DIR.relative_to(ROOT)})")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--update", nargs="*", metavar="NAME", help="pin the latest release (default: all sources)")
    mode.add_argument("--check", action="store_true", help="only report sources with a newer release")
    args = ap.parse_args()

    pins = load_pins()
    if args.update is not None:
        unknown = set(args.update) - set(pins)
        if unknown:
            die(f"unknown source(s) {', '.join(sorted(unknown))}; known: {', '.join(pins)}")
        update(pins, args.update or list(pins))
    elif args.check:
        sys.exit(1 if check(pins, list(pins)) else 0)
    else:
        fetch(pins, Path(args.dest) if args.dest else BIN_DIR)
        check(pins, list(pins))


if __name__ == "__main__":
    main()
