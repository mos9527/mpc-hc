#!/usr/bin/env python3
"""Apply MPC-HC-owned LAV patches without changing the submodule HEAD or index.

Build wrappers use `apply`; publishing uses `verify`. Only a wholly pristine or
exactly patched worktree is accepted. No user changes are discarded or reversed.
The expected blobs are computed in an isolated index using Git's normal filters,
so CRLF working copies are compared to the canonical source, not raw text diffs.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

LAV_DIR = Path(__file__).resolve().parent
SOURCE = LAV_DIR / "src"


class PatchError(RuntimeError):
    pass


def find_git():
    candidates = []
    if os.environ.get("MPCHC_GIT"):
        base = Path(os.environ["MPCHC_GIT"])
        candidates += [base / "cmd" / "git.exe", base / "bin" / "git.exe"]
    if sys.platform == "win32":
        for folder in (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"),
                       str(Path(os.environ.get("LOCALAPPDATA", "")) / "Programs")):
            if folder:
                candidates.append(Path(folder) / "Git" / "cmd" / "git.exe")
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    executable = shutil.which("git")
    if not executable:
        raise PatchError("Git not found; install Git for Windows or set MPCHC_GIT")
    return executable


def verify_sources(*, apply=False, require_applied=False, git_executable=None):
    """Return 'pristine' or 'patched'; applying requires a verified pristine tree.

    A temporary index represents HEAD plus the complete ordered patch series.
    The real index must remain at HEAD. Unexpected modifications (including
    nested submodule changes), untracked non-ignored files, partial application,
    or edited managed files are errors, even when Git could still apply a hunk.
    """
    executable = git_executable or find_git()
    # Never inherit a caller's alternate index into the real-index checks.
    environment = os.environ.copy()
    environment.pop("GIT_INDEX_FILE", None)

    def git(*args, env=None, check=True):
        proc = subprocess.run([executable, *args], cwd=SOURCE,
                              env=env or environment, capture_output=True)
        if check and proc.returncode:
            detail = proc.stderr.decode("utf-8", errors="replace").strip()
            raise PatchError(f"git {' '.join(args)} failed: {detail}")
        return proc

    toplevel = Path(os.fsdecode(git("rev-parse", "--show-toplevel").stdout.strip())).resolve()
    if toplevel != SOURCE:
        raise PatchError("LAV submodule is not initialized at src; run git submodule update --init --recursive")
    pinned = git("-C", str(LAV_DIR.parents[2]), "rev-parse",
                 "HEAD:src/thirdparty/LAVFilters/src").stdout.strip()
    if git("rev-parse", "HEAD").stdout.strip() != pinned:
        raise PatchError("LAV HEAD differs from MPC-HC's pinned upstream commit; update the submodule before applying patches")
    patches = sorted((LAV_DIR / "patches").glob("*.patch"))
    if not patches:
        raise PatchError("No managed LAV patches found")
    staged = git("diff", "--cached", "--quiet", "--ignore-submodules=none", check=False)
    if staged.returncode:
        raise PatchError("LAV has staged changes; the managed patch workflow never modifies the real index")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z").stdout
    if untracked:
        raise PatchError("LAV has untracked source files: " + untracked.decode("utf-8", errors="replace").replace("\0", " "))

    with tempfile.TemporaryDirectory(prefix="mpchc-lav-patches-") as temporary:
        env = environment.copy()
        env["GIT_INDEX_FILE"] = str(Path(temporary) / "index")
        git("read-tree", "HEAD", env=env)
        arguments = []
        for patch in patches:
            # Git for Windows may check out the tracked patch itself as CRLF.
            # Apply canonical LF input to both the virtual index and worktree.
            normalized = Path(temporary) / patch.name
            normalized.write_bytes(patch.read_bytes().replace(b"\r\n", b"\n"))
            arguments.append(str(normalized))
            git("apply", "--cached", "--whitespace=nowarn", str(normalized), env=env)
        changed = git("diff", "--cached", "--name-only", "-z", "HEAD", env=env).stdout
        paths = [os.fsdecode(name) for name in changed.split(b"\0") if name]
        if not paths:
            raise PatchError("Managed patch series has no source changes")
        actual = git("diff", "--name-only", "-z", "--ignore-submodules=none", "HEAD").stdout
        actual_paths = {os.fsdecode(name) for name in actual.split(b"\0") if name}
        unexpected = actual_paths - set(paths)
        if unexpected:
            raise PatchError("Unmanaged LAV changes: " + ", ".join(sorted(unexpected)))

        pristine = patched = True
        for name in paths:
            # These local fixes modify regular files only. Add/delete/mode-change
            # patches require an explicit extension of this verifier, not a bypass.
            before = git("ls-tree", "HEAD", "--", name).stdout.split()
            after = git("ls-files", "--stage", "--", name, env=env).stdout.split()
            if len(before) < 3 or len(after) < 3 or before[0] != b"100644" or after[0] != b"100644":
                raise PatchError(f"Managed patches must modify existing regular files: {name}")
            path = SOURCE / name
            if not path.is_file() or path.is_symlink():
                raise PatchError(f"Missing or non-regular managed source: {name}")
            blob = git("hash-object", f"--path={name}", "--", name).stdout.strip()
            pristine = pristine and blob == before[2]
            patched = patched and blob == after[1]
        if not pristine and not patched:
            raise PatchError("LAV managed files are edited or only partly patched; inspect git diff inside the submodule")
        if patched:
            return "patched"
        if apply:
            # Git validates the complete series first; no best-effort/reject mode.
            git("apply", "--check", "--whitespace=nowarn", *arguments)
            git("apply", "--whitespace=nowarn", *arguments)
            return verify_sources(require_applied=True, git_executable=executable)
        if require_applied:
            raise PatchError("Managed LAV patches are not applied; run apply_patches.py apply and rebuild LAV Filters")
        return "pristine"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "verify", "check"),
                        help="apply once; verify exactly patched; check pristine or exactly patched")
    args = parser.parse_args()
    try:
        state = verify_sources(apply=args.action == "apply", require_applied=args.action == "verify")
    except (PatchError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"LAV managed source patches: {state}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
