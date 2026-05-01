#!/usr/bin/env python3
"""Cross-platform packaging script.

Generates a zip archive of the project source tree suitable for
submission to an automated test platform. Works on macOS, Linux,
and Windows without third-party dependencies.

Usage:
    python3 scripts/package.py                       # default output to dist/
    python3 scripts/package.py -o submission.zip     # custom output path
    python3 scripts/package.py --name myteam         # custom basename
    python3 scripts/package.py --no-git-info         # omit git sha + timestamp
    python3 scripts/package.py --list                # only list files, no zip
"""

from __future__ import annotations

import argparse
import datetime as dt
import fnmatch
import os
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Iterable

REPO_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_INCLUDES = [
    "src",
    "CMake",
    "scripts",
    "thirdparty",
    "docs",
    "CMakeLists.txt",
    "README.md",
    ".gitignore",
]

DEFAULT_EXCLUDE_DIRS = {
    ".git",
    ".github",
    ".vscode",
    ".cache",
    ".idea",
    "__pycache__",
    "build",
    "Testing",
    "CMakeFiles",
    ".antlr",
    "dist",
    "node_modules",
}

DEFAULT_EXCLUDE_PATTERNS = [
    ".DS_Store",
    "Thumbs.db",
    "*.pyc",
    "*.pyo",
    "*.log",
    "*.tmp",
    "*.temp",
    "*.zip",
    "*.tar",
    "*.tar.gz",
    "*.tgz",
    "*.o",
    "*.obj",
    "*.a",
    "*.so",
    "*.so.*",
    "*.dylib",
    "*.dll",
    "*.exe",
    "*.out",
    "compile_commands.json",
    "CMakeCache.txt",
    "cmake_install.cmake",
    "Makefile",
]


def should_exclude(rel_path: Path) -> bool:
    parts = rel_path.parts
    for part in parts:
        if part in DEFAULT_EXCLUDE_DIRS:
            return True
    name = rel_path.name
    for pat in DEFAULT_EXCLUDE_PATTERNS:
        if fnmatch.fnmatch(name, pat):
            return True
    return False


def iter_files(roots: Iterable[str]) -> Iterable[Path]:
    for entry in roots:
        path = (REPO_ROOT / entry).resolve()
        try:
            path.relative_to(REPO_ROOT)
        except ValueError:
            print(f"[skip] {entry}: outside repo root", file=sys.stderr)
            continue
        if not path.exists():
            print(f"[skip] {entry}: does not exist", file=sys.stderr)
            continue
        if path.is_file():
            rel = path.relative_to(REPO_ROOT)
            if not should_exclude(rel):
                yield path
            continue
        for dirpath, dirnames, filenames in os.walk(path):
            dp = Path(dirpath)
            # prune excluded dirs in-place for os.walk efficiency
            dirnames[:] = [
                d for d in dirnames
                if not should_exclude((dp / d).relative_to(REPO_ROOT))
            ]
            for fname in filenames:
                fp = dp / fname
                rel = fp.relative_to(REPO_ROOT)
                if not should_exclude(rel):
                    yield fp


def git_short_sha() -> str | None:
    try:
        out = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return out.stdout.strip() or None
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def build_default_output(basename: str, with_git: bool) -> Path:
    parts = [basename]
    if with_git:
        sha = git_short_sha()
        if sha:
            parts.append(sha)
        parts.append(dt.datetime.now().strftime("%Y%m%d-%H%M%S"))
    fname = "-".join(parts) + ".zip"
    return REPO_ROOT / "dist" / fname


def human_size(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.1f}{unit}" if unit != "B" else f"{n}{unit}"
        n /= 1024
    return f"{n:.1f}TB"


def main() -> int:
    parser = argparse.ArgumentParser(description="Package project into a submission zip.")
    parser.add_argument("-o", "--output", type=Path, help="Output zip path.")
    parser.add_argument("--name", default="compiler-submission",
                        help="Basename for default output filename (default: compiler-submission).")
    parser.add_argument("--no-git-info", action="store_true",
                        help="Do not append git sha + timestamp to the default filename.")
    parser.add_argument("--list", action="store_true",
                        help="List files that would be packaged and exit.")
    parser.add_argument("--include", action="append", default=None,
                        help="Override include list (repeatable). Defaults to project layout.")
    parser.add_argument("--wrap", action="store_true",
                        help="Place all files under a top-level directory inside the zip "
                             "(disabled by default; some test platforms double-nest if enabled).")
    args = parser.parse_args()

    includes = args.include if args.include else DEFAULT_INCLUDES

    files = sorted(set(iter_files(includes)))

    # Sanity check: ANTLR-generated headers must ship since the judge platform
    # typically does not have Java/ANTLR available to regenerate them.
    antlr_gen = REPO_ROOT / "src" / "frontend" / "autogenerated"
    if not antlr_gen.exists() or not any(antlr_gen.glob("MiniC*.h")):
        print(
            "[warn] src/frontend/autogenerated/ is missing or empty.\n"
            "       The judge platform won't be able to build without it.\n"
            "       Run a local build first (e.g. cmake --build build --target generate_antlr4_cpp).",
            file=sys.stderr,
        )

    if args.list:
        for f in files:
            print(f.relative_to(REPO_ROOT).as_posix())
        print(f"\nTotal: {len(files)} files", file=sys.stderr)
        return 0

    if not files:
        print("No files to package.", file=sys.stderr)
        return 1

    output = args.output
    if output is None:
        output = build_default_output(args.name, with_git=not args.no_git_info)
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)

    # archive name: optional top-level dir inside the zip. Some judge platforms
    # extract the zip into their own source folder and a wrapper dir would
    # cause double-nesting, so default to flat layout.
    archive_root = output.stem if args.wrap else ""

    print(f"Packaging {len(files)} files -> {output}")
    total = 0
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for f in files:
            rel = f.relative_to(REPO_ROOT)
            arcname = (Path(archive_root) / rel) if archive_root else rel
            zf.write(f, arcname.as_posix())
            total += f.stat().st_size

    print(f"Done. Archive: {output} ({human_size(output.stat().st_size)} compressed, "
          f"{human_size(total)} raw)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
