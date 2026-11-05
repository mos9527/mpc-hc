#!/usr/bin/env python3
"""Prebuilt x64 runtime bundle for CI: LAV Filters + d3dx9_43.dll.

CI does not build LAV Filters (ffmpeg, dav1d, libxml2, ... take far longer than
the player itself). Instead they are built locally from the pinned submodules,
published as an asset of a GitHub release in this repository, and downloaded
by CI.

The release tag is derived from the git tree of src/thirdparty/LAVFilters. That
tree covers LAV's build scripts and the pinned commits of every submodule the
LAV build uses, so any change to what would be built yields a new tag and CI
refuses to run with a stale bundle.

  python ci/prebuilt.py key        print the tag the current checkout needs
  python ci/prebuilt.py build      build LAV Filters x64 Release locally (GCC toolchain by default)
  python ci/prebuilt.py publish    package bin/mpc-hc_x64/LAVFilters64 and upload it (run locally)
  python ci/prebuilt.py fetch      download and unpack the bundle into bin/mpc-hc_x64 (run by CI)

Local workflow after the pinned LAV Filters changed (e.g. after merging upstream):
  git submodule update --init --recursive
  python ci/prebuilt.py build
  python ci/prebuilt.py publish
"""

import sys

if sys.platform != "win32":
    # MSYS2's /usr/bin/python (sys.platform "msys"/"cygwin") has no Windows ctypes and runs
    # shell commands with sh instead of cmd, so the build and version checks cannot work
    sys.exit(f"error: run this with a native Windows Python, not {sys.executable} ({sys.platform}); "
             "e.g. 'py -3 ci/prebuilt.py ...' or from PowerShell/cmd")

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile
from ctypes import wintypes
from datetime import datetime, timezone
from pathlib import Path

import vstools  # ci/vstools.py, next to this script

ROOT = Path(__file__).resolve().parent.parent
LAV_DIR = "src/thirdparty/LAVFilters"
LAV_SRC = LAV_DIR + "/src"
BIN_DIR = ROOT / "bin" / "mpc-hc_x64"
LAV_OUT = "LAVFilters64"
D3DX9 = "d3dx9_43.dll"
ASSET = "prebuilt-x64.zip"
PDB_ASSET = "prebuilt-x64-pdb.zip"
METADATA = "prebuilt.json"
# Bump when the layout of the bundle changes, so old releases are not picked up
FORMAT = 1

# What must be in LAVFilters64 for the player to load the internal LAV Filters
REQUIRED = [
    "LAVSplitter.ax", "LAVVideo.ax", "LAVAudio.ax",
    "avcodec-lav-*.dll", "avformat-lav-*.dll", "avutil-lav-*.dll",
    "avfilter-lav-*.dll", "swresample-lav-*.dll", "swscale-lav-*.dll",
    "LAVFilters.Dependencies.manifest",
]
SHIPPED = ["*.ax", "*.dll", "*.manifest"]  # the same set build.bat packages


def die(msg):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(*cmd, cwd=ROOT, check=True):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8")
    if check and p.returncode != 0:
        die(f"'{' '.join(cmd)}' failed:\n{p.stderr.strip()}")
    return p


def find_git():
    """Git for Windows, the same git the repository's own build scripts look for.

    Not simply the first git on PATH: inside an MSYS2 shell that is MSYS2's /usr/bin/git,
    which does not read Git for Windows' system config (where core.autocrlf=true lives on
    a default install). On a checkout made by Git for Windows it then reports every
    text file whose working copy has CRLF line endings as modified.
    """
    candidates = []
    mpchc_git = os.environ.get("MPCHC_GIT") or user_config().get("MPCHC_GIT")
    if mpchc_git:
        candidates += [Path(mpchc_git) / "cmd" / "git.exe", Path(mpchc_git) / "bin" / "git.exe"]
    for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"),
                 os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs")):
        if base:
            candidates.append(Path(base) / "Git" / "cmd" / "git.exe")
    for c in candidates:
        if c.is_file():
            return str(c)
    on_path = shutil.which("git")
    if not on_path:
        die("git not found; install Git for Windows or set MPCHC_GIT")
    if re.search(r"[\\/](usr[\\/]bin|cygwin)", on_path, re.I):
        print(f"warning: using {on_path}; an MSYS2/Cygwin git may report line-ending-only changes "
              "as modifications. Install Git for Windows or set MPCHC_GIT.", file=sys.stderr)
    return on_path


GIT = None


def git(*args, cwd=ROOT, check=True):
    global GIT
    if GIT is None:
        GIT = find_git()
    return run(GIT, *args, cwd=cwd, check=check).stdout.strip()


def tag_for_checkout():
    tree = git("rev-parse", f"HEAD:{LAV_DIR}")
    return f"prebuilt-v{FORMAT}-{tree[:12]}"


def default_repo():
    if os.environ.get("GITHUB_REPOSITORY"):
        return os.environ["GITHUB_REPOSITORY"]
    url = git("remote", "get-url", "origin")
    m = re.search(r"github\.com[:/]([^/]+/[^/]+?)(?:\.git)?/?$", url)
    if not m:
        die(f"cannot derive a GitHub repository from origin ({url}); pass --repo owner/name")
    return m.group(1)


def file_version(path):
    """The numeric FILEVERSION of a PE file as a 4-tuple, or None."""
    ver = ctypes.WinDLL("version")
    ver.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, wintypes.LPDWORD]
    ver.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    ver.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR,
                                   ctypes.POINTER(wintypes.LPVOID), ctypes.POINTER(wintypes.UINT)]
    size = ver.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return None
    buf = ctypes.create_string_buffer(size)
    if not ver.GetFileVersionInfoW(str(path), 0, size, buf):
        return None
    ptr, n = wintypes.LPVOID(), wintypes.UINT()
    if not ver.VerQueryValueW(buf, "\\", ctypes.byref(ptr), ctypes.byref(n)):
        return None
    # VS_FIXEDFILEINFO starts with dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS
    info = ctypes.cast(ptr, ctypes.POINTER(wintypes.DWORD * 4)).contents
    return (info[2] >> 16, info[2] & 0xFFFF, info[3] >> 16, info[3] & 0xFFFF)


def is_x64_pe(path):
    with open(path, "rb") as f:
        head = f.read(4096)
    if head[:2] != b"MZ" or len(head) < 0x40:
        return False
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    if pe + 6 > len(head) or head[pe:pe + 4] != b"PE\0\0":
        return False
    return struct.unpack_from("<H", head, pe + 4)[0] == 0x8664


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def gh(*args, check=True):
    if not shutil.which("gh"):
        die("the GitHub CLI (gh) is not on PATH")
    return run("gh", *args, check=check)


# ---------------------------------------------------------------- publish

def check_sources():
    """The LAV build output must come from exactly the pinned, unmodified sources."""
    st = git("submodule", "status", "--recursive", "--", LAV_DIR).splitlines()
    if not st:
        die(f"no submodules found under {LAV_DIR}; run 'git submodule update --init --recursive'")
    bad = [line for line in st if line[:1] in "-+U"]
    if bad:
        die("LAV Filters submodules are not checked out at their pinned commits "
            "(- = not initialized, + = different commit, U = conflict):\n  " + "\n  ".join(bad) +
            "\nrun 'git submodule update --init --recursive' and rebuild")
    dirty = git("status", "--porcelain", "--ignore-submodules=untracked", "--", LAV_DIR)
    if dirty:
        # show what changed: a rewrite with the same content (line endings, BOM) looks the same in --porcelain
        diff = git("diff", "--stat", "--", LAV_DIR, check=False)
        die(f"{LAV_DIR} has uncommitted changes, which the release tag would not reflect:\n{dirty}\n\n{diff}\n\n"
            f"inspect with 'git diff -- {LAV_DIR}', discard with 'git restore -- {LAV_DIR}'")


def lav_describe():
    """The version the LAV build stamps into its filters, computed the way it does it.

    Major/minor/revision come from common/includes/version.h; the build number is
    what genversion.cmd derives: the commit count of 'git describe' at the commit
    before clsid2's MPC-HC patch series starts, not at HEAD.
    """
    src = ROOT / LAV_SRC
    header = (src / "common" / "includes" / "version.h").read_text(encoding="utf-8", errors="replace")
    parts = {k: int(v) for k, v in re.findall(r"#define\s+LAV_VERSION_(MAJOR|MINOR|REVISION)\s+(\d+)", header)}
    if len(parts) != 3:
        return None, None
    base = git("log", r"--grep=\[MPC-HC\] Use our own ffmpeg clone repository\.", "--format=%H",
               cwd=src, check=False).splitlines()
    describe = git("describe", "--long", f"{base[0]}~1", cwd=src, check=False) if base else ""
    m = re.match(r"^.+?-(\d+)-g([0-9a-f]+)$", describe)
    if not m:
        return describe or None, None
    head = git("rev-parse", "--short=8", "HEAD", cwd=src)
    label = f"{parts['MAJOR']}.{parts['MINOR']}.{parts['REVISION']}.{m.group(1)} ({head})"
    return label, (parts["MAJOR"], parts["MINOR"], parts["REVISION"], int(m.group(1)))


def collect(src):
    files = []
    for pattern in REQUIRED:
        if not list(src.glob(pattern)):
            die(f"{src} has no {pattern}; build LAV Filters first:\n"
                f"  {LAV_DIR.replace('/', os.sep)}\\build_lavfilters.bat Build x64 Release")
    for pattern in SHIPPED:
        files += sorted(src.glob(pattern))
    for f in files:
        if f.suffix.lower() in (".ax", ".dll") and not is_x64_pe(f):
            die(f"{f} is not an x64 binary")
    return files


def find_d3dx9(explicit):
    candidates = [Path(explicit)] if explicit else [
        BIN_DIR / D3DX9,
        Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / D3DX9,
    ]
    for c in candidates:
        if c.is_file():
            if not is_x64_pe(c):
                die(f"{c} is not an x64 binary")
            return c
    die(f"{D3DX9} not found (looked in {', '.join(map(str, candidates))}).\n"
        "Install the DirectX End-User Runtime, pass --d3dx9 PATH, or --no-d3dx9 to leave it out")


def cmd_publish(args):
    check_sources()
    tag = tag_for_checkout()
    repo = args.repo or default_repo()
    src = BIN_DIR / LAV_OUT
    files = collect(src)

    describe, expected = lav_describe()
    versions = {f.name: file_version(f) for f in files if f.suffix.lower() == ".ax"}
    if len(set(versions.values())) != 1:
        die(f"the LAV filters in {src} have different versions: {versions}")
    built = next(iter(versions.values()))
    built_str = ".".join(map(str, built)) if built else "unknown"
    if expected is None:
        print(f"warning: cannot parse the LAV version from '{describe}', skipping the staleness check")
    elif built != expected:
        die(f"{src} holds LAV Filters {built_str}, but the checkout is {describe}; "
            "the build output is stale, rebuild LAV Filters")

    d3dx9 = None if args.no_d3dx9 else find_d3dx9(args.d3dx9)
    pdbs = sorted(src.glob("*.pdb"))

    metadata = {
        "format": FORMAT,
        "tag": tag,
        "lav_tree": git("rev-parse", f"HEAD:{LAV_DIR}"),
        "lav_describe": describe,
        "lav_version": built_str,
        "source_commit": git("rev-parse", "HEAD"),
        "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "files": {f"{LAV_OUT}/{f.name}": sha256(f) for f in files},
    }
    if d3dx9:
        metadata["files"][D3DX9] = sha256(d3dx9)

    exists = gh("release", "view", tag, "-R", repo, check=False).returncode == 0
    if exists and not args.force:
        die(f"release {tag} already exists in {repo}; pass --force to replace its assets")

    with tempfile.TemporaryDirectory() as tmp:
        asset = Path(tmp) / ASSET
        with zipfile.ZipFile(asset, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for f in files:
                z.write(f, f"{LAV_OUT}/{f.name}")
            if d3dx9:
                z.write(d3dx9, D3DX9)
            z.writestr(METADATA, json.dumps(metadata, indent=2))
        assets = [str(asset)]
        if pdbs:
            pdb_asset = Path(tmp) / PDB_ASSET
            with zipfile.ZipFile(pdb_asset, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
                for f in pdbs:
                    z.write(f, f"{LAV_OUT}/{f.name}")
            assets.append(str(pdb_asset))

        print(f"{tag}: LAV Filters {describe} ({built_str}), {len(files)} files"
              f"{', ' + D3DX9 if d3dx9 else ''}, {asset.stat().st_size / 2**20:.1f} MiB")
        if args.dry_run:
            print("dry run, nothing uploaded")
            return

        if exists:
            gh("release", "upload", tag, *assets, "-R", repo, "--clobber")
        else:
            notes = (f"Prebuilt x64 runtime files for CI, not a player release.\n\n"
                     f"- LAV Filters `{describe}` (file version {built_str})\n"
                     f"- `{LAV_DIR}` tree `{metadata['lav_tree']}`\n"
                     f"- built from `{metadata['source_commit']}`\n"
                     + (f"- includes `{D3DX9}`\n" if d3dx9 else ""))
            target = []
            if git("branch", "-r", "--contains", "HEAD", check=False):
                target = ["--target", metadata["source_commit"]]
            else:
                print("note: HEAD is not on any remote branch yet, the tag will point at the default branch")
            gh("release", "create", tag, *assets, "-R", repo, "--prerelease",
               "--title", f"Prebuilt LAV Filters {describe} (x64)", "--notes", notes, *target)
    print(f"published https://github.com/{repo}/releases/tag/{tag}")


# ---------------------------------------------------------------- build

def find_vsdevcmd():
    try:
        vs, ver, missing = vstools.v143_status()
    except vstools.VsError as e:
        die(str(e))
    if missing:
        die(f"the v143 toolset ({ver}) lacks {', '.join(missing)}, which MPC-HC builds with under every Visual Studio "
            f"version. Run 'python ci/vstools.py' for the install command (or --install from an admin shell).")
    vsdevcmd = vs / "Common7" / "Tools" / "VsDevCmd.bat"
    if not vsdevcmd.is_file():
        die(f"{vsdevcmd} not found")
    return vsdevcmd


def msvc_ffmpeg_staleness():
    """Compare the ffmpeg the MSVC project files were generated from with the pinned one.

    msvc/ffmpeg holds generated config headers, source lists and export files that
    msvc/regen/regen.cmd produces from a configured ffmpeg; they are only valid for
    the ffmpeg commit they were generated from. Returns (then, now), with then None
    when it cannot be determined.
    """
    src = ROOT / LAV_SRC
    ff_now = git("rev-parse", "HEAD:ffmpeg", cwd=src)
    last = git("log", "-1", "--format=%H", "--", f"{LAV_DIR}/msvc/ffmpeg")
    if not last:
        return None, ff_now
    lav_then = git("rev-parse", f"{last}:{LAV_SRC}", check=False)
    ff_then = git("rev-parse", f"{lav_then}:ffmpeg", cwd=src, check=False)
    if not re.fullmatch(r"[0-9a-f]{40}", ff_then or ""):
        # clsid2 rebases the LAV fork, so the old commit may not be in the clone
        git("fetch", "-q", "origin", lav_then, cwd=src, check=False)
        ff_then = git("rev-parse", f"{lav_then}:ffmpeg", cwd=src, check=False)
    return (ff_then if re.fullmatch(r"[0-9a-f]{40}", ff_then or "") else None), ff_now


def user_config():
    """The SET "NAME=value" lines of build.user.bat, overridden by the environment."""
    cfg = {}
    bat = ROOT / "build.user.bat"
    if bat.is_file():
        for m in re.finditer(r'^\s*SET\s+"?(\w+)=([^"\r\n]*)"?\s*$', bat.read_text(errors="replace"), re.M | re.I):
            cfg[m.group(1).upper()] = m.group(2)
    for k in ("MPCHC_MSYS", "MPCHC_MINGW32", "MPCHC_MINGW64"):
        if os.environ.get(k):
            cfg[k] = os.environ[k]
    return cfg


def check_gcc_toolchain():
    """What build_ffmpeg.sh needs for an x64 GCC build, checked before the long build starts.

    common.bat puts MSYS's usr\\bin and bin and then MPCHC_MINGW32's bin in front of PATH;
    the x64 build calls the x86_64-w64-mingw32- prefixed tools, which the stock MSYS2
    binutils do not provide (the toolchain of docs/Compilation.md, Part H does). If they
    are missing there, whatever else on PATH has them is used (e.g. Strawberry Perl's gcc).
    """
    cfg = user_config()
    msys = Path(cfg.get("MPCHC_MSYS", r"C:\msys64"))
    mingw = Path(cfg.get("MPCHC_MINGW32", r"C:\msys64\mingw32"))
    problems = []
    for tool in ("make", "sh"):
        if not (msys / "usr" / "bin" / f"{tool}.exe").is_file():
            problems.append(f"{msys}\\usr\\bin\\{tool}.exe is missing (pacman -S make)")
    if not any((d / "pkg-config.exe").is_file() for d in (msys / "usr" / "bin", mingw / "bin")):
        problems.append("no pkg-config.exe in MSYS's usr\\bin or MinGW's bin (pacman -S pkgconf)")
    for tool in ("gcc", "nm", "ar", "ranlib", "strip", "windres"):
        if not (mingw / "bin" / f"x86_64-w64-mingw32-{tool}.exe").is_file():
            problems.append(f"{mingw}\\bin\\x86_64-w64-mingw32-{tool}.exe is missing")
    if problems:
        die("the GCC toolchain is incomplete:\n  " + "\n  ".join(problems) +
            "\nSee docs/Compilation.md, Part H. MPCHC_MINGW32 in build.user.bat must point at the "
            "64-bit toolchain (e.g. C:\\msys64\\mingw64): it is the MinGW directory that goes on PATH.")


def cmd_build(args):
    """MPC-HC's zlib, then LAV Filters through build_lavfilters.bat.

    The MSVC build of LAV's ffmpeg links MPC-HC's zlib.lib from bin\\lib\\Release_x64.
    Inside mpc-hc.sln that is a project dependency of LAVFilters; calling
    build_lavfilters.bat on its own skips it, so it is built here first.
    """
    check_sources()
    if args.toolchain == "GCC":
        check_gcc_toolchain()
    if args.toolchain == "MSVC":
        then, now = msvc_ffmpeg_staleness()
        if then is None:
            print("warning: cannot tell which ffmpeg the MSVC project files were generated from")
        elif then != now:
            ffdir = ROOT / LAV_SRC / "ffmpeg"
            n = git("rev-list", "--count", f"{then}..{now}", cwd=ffdir, check=False) or "?"
            msg = (f"the MSVC project files in {LAV_DIR}/msvc/ffmpeg were generated from ffmpeg {then[:10]}, "
                   f"but LAV pins {now[:10]} ({n} commits later); the build would fail or miss components.\n"
                   f"Regenerate them first (needs Git for Windows' bash and MSYS2's make and pkgconf):\n"
                   f"  {LAV_DIR.replace('/', os.sep)}\\msvc\\regen\\regen.cmd x64\n"
                   f"or build with --toolchain GCC")
            if not args.ignore_stale:
                die(msg)
            print("warning: " + msg)
    if not shutil.which("nasm"):
        die("nasm.exe is not on PATH (see docs/Compilation.md, Part C)")
    vsdevcmd = find_vsdevcmd()
    lav_bat = ROOT / LAV_DIR / "build_lavfilters.bat"
    steps = " && ".join([
        f'call "{vsdevcmd}" -no_logo -arch=amd64 -host_arch=amd64',
        # the solution, not zlib.vcxproj alone: its OutDir is derived from $(SolutionDir)
        f'msbuild mpc-hc.sln -t:"Libraries\\zlib" -p:Configuration=Release -p:Platform=x64 -m -nologo -v:minimal',
        f'call "{lav_bat}" {args.type} x64 Release {args.toolchain}',
    ])
    print(f"building LAV Filters x64 Release ({args.toolchain}), this takes a while...", flush=True)
    rc = subprocess.run(steps, cwd=ROOT, shell=True).returncode
    if rc != 0:
        die(f"build failed (exit code {rc})")
    print(f"done; next: python ci/prebuilt.py publish --dry-run")


# ---------------------------------------------------------------- fetch

def cmd_fetch(args):
    tag = tag_for_checkout()
    repo = args.repo or default_repo()
    dest = Path(args.dest) if args.dest else BIN_DIR
    with tempfile.TemporaryDirectory() as tmp:
        p = gh("release", "download", tag, "-R", repo, "-p", ASSET, "-D", tmp, "--clobber", check=False)
        if p.returncode != 0:
            die(f"no prebuilt bundle {tag} in {repo} ({p.stderr.strip()}).\n"
                f"None was published for the LAV Filters pinned by this commit. Build and publish them locally:\n"
                f"  git submodule update --init --recursive\n"
                f"  {LAV_DIR.replace('/', os.sep)}\\build_lavfilters.bat Build x64 Release\n"
                f"  python ci/prebuilt.py publish")
        with zipfile.ZipFile(Path(tmp) / ASSET) as z:
            metadata = json.loads(z.read(METADATA))
            if metadata.get("tag") != tag:
                die(f"{ASSET} of release {tag} was built for {metadata.get('tag')}")
            if (dest / LAV_OUT).exists():
                shutil.rmtree(dest / LAV_OUT)
            dest.mkdir(parents=True, exist_ok=True)
            for name in z.namelist():
                if name == METADATA:
                    continue
                target = dest / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(name) as fin, open(target, "wb") as fout:
                    shutil.copyfileobj(fin, fout)
                if metadata["files"].get(name) != sha256(target):
                    die(f"checksum mismatch for {name}")
    print(f"{tag}: LAV Filters {metadata['lav_describe']} -> {dest}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", help="owner/name of the GitHub repository (default: origin, or GITHUB_REPOSITORY in CI)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("key", help="print the release tag for the current checkout")
    b = sub.add_parser("build", help="build LAV Filters x64 Release locally")
    b.add_argument("--toolchain", choices=["MSVC", "GCC"], default="GCC",
                   help="toolchain for LAV's ffmpeg (default: GCC, as upstream releases; it needs the MSYS2 "
                        "setup of docs/Compilation.md Part H. MSVC needs msvc/regen/regen.cmd after LAV bumps)")
    b.add_argument("--rebuild", dest="type", action="store_const", const="Rebuild", default="Build",
                   help="clean before building")
    b.add_argument("--ignore-stale", action="store_true",
                   help="build with MSVC even if its generated project files are older than the pinned ffmpeg")
    p = sub.add_parser("publish", help="package and upload the locally built LAV Filters")
    p.add_argument("--d3dx9", help=f"path of the x64 {D3DX9} to bundle")
    p.add_argument("--no-d3dx9", action="store_true", help=f"do not bundle {D3DX9}")
    p.add_argument("--force", action="store_true", help="replace the assets of an existing release")
    p.add_argument("--dry-run", action="store_true", help="validate and package, but do not upload")
    f = sub.add_parser("fetch", help="download and unpack the bundle")
    f.add_argument("--dest", help=f"output directory (default: {BIN_DIR.relative_to(ROOT)})")
    args = ap.parse_args()

    if args.cmd == "key":
        print(tag_for_checkout())
    elif args.cmd == "build":
        cmd_build(args)
    elif args.cmd == "publish":
        cmd_publish(args)
    else:
        cmd_fetch(args)


if __name__ == "__main__":
    main()
