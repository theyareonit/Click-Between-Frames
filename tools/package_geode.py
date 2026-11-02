#!/usr/bin/env python3
"""Build a .geode release package for Click Before Frames.

A .geode file is a ZIP archive containing mod.json, the in-game metadata
files, resources and the platform binaries. This script packages everything
that is available in the repository, and optionally merges in compiled
binaries produced by `geode build` / the CI workflow.

Examples:
    python3 tools/package_geode.py
    python3 tools/package_geode.py --binaries bin --strict
    python3 tools/package_geode.py --output dist/Click-Before-Frames-v1.5.1.geode

With --strict the script refuses to produce a package without at least one
platform binary (use this for real releases); without it, a metadata-only
package is produced for inspection/distribution of the assets.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Files Geode reads from the package (other than the binaries).
STATIC_FILES = (
    "mod.json",
    "logo.png",
    "about.md",
    "changelog.md",
    "support.md",
    "LICENSE",
)

# Longest suffixes first so ".android64.so" is not mistaken for ".so".
BINARY_SUFFIXES = (
    ".android32.so",
    ".android64.so",
    ".ios.dylib",
    ".dll",
    ".dylib",
    ".so",
)

# Only these entries are allowed inside the archive, to keep it clean.
SKIP_NAMES = {".DS_Store", "Thumbs.db", ".gitkeep"}


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def suffix_of(name: str) -> str | None:
    for suffix in BINARY_SUFFIXES:
        if name.endswith(suffix):
            return suffix
    return None


def collect_binaries(mod_id: str, directories: list[Path]) -> dict[str, Path]:
    """Map archive filename -> source path for every platform binary found."""
    binaries: dict[str, Path] = {}
    for directory in directories:
        if not directory.is_dir():
            fail(f"binary directory not found: {directory}")
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.name in SKIP_NAMES:
                continue
            suffix = suffix_of(path.name)
            if suffix is None:
                continue
            arcname = f"{mod_id}{suffix}"
            if arcname in binaries:
                print(f"warning: duplicate {arcname}, keeping {binaries[arcname]}")
                continue
            binaries[arcname] = path
    return binaries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", "-o", type=Path, help="output .geode path (default: <mod id>.geode in the repo root)")
    parser.add_argument(
        "--binaries", "-b", type=Path, action="append", default=[],
        help="directory with compiled binaries (repeatable). Defaults to ./bin when it exists.",
    )
    parser.add_argument("--strict", action="store_true", help="fail when no platform binary is available")
    parser.add_argument("--no-checksum", action="store_true", help="do not write a .sha256 sidecar file")
    args = parser.parse_args()

    mod_json_path = ROOT / "mod.json"
    try:
        meta = json.loads(mod_json_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read mod.json: {exc}")

    mod_id = meta.get("id")
    version = meta.get("version")
    if not mod_id or not version:
        fail("mod.json must contain 'id' and 'version'")

    output: Path = (args.output or (ROOT / f"{mod_id}.geode")).resolve()
    if output.suffix.lower() != ".geode":
        fail(f"output file must end in .geode: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    entries: dict[str, Path] = {}
    for name in STATIC_FILES:
        path = ROOT / name
        if path.is_file():
            entries[name] = path
        elif name == "mod.json":
            fail("mod.json is required")

    resources = ROOT / "resources"
    if resources.is_dir():
        for path in sorted(resources.rglob("*")):
            if path.is_file() and path.name not in SKIP_NAMES:
                entries[path.relative_to(ROOT).as_posix()] = path

    binary_dirs = args.binaries or ([ROOT / "bin"] if (ROOT / "bin").is_dir() else [])
    binaries = collect_binaries(mod_id, binary_dirs)
    for arcname, path in binaries.items():
        if arcname in entries:
            fail(f"binary would overwrite a resource: {arcname}")
        entries[arcname] = path

    if args.strict and not binaries:
        fail("no platform binaries found; pass --binaries <dir> with the CI/geode build output")

    # Deterministic archive: fixed timestamps + sorted names + fixed permissions.
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for arcname in sorted(entries):
            path = entries[arcname]
            info = zipfile.ZipInfo(arcname, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100644 << 16) | 0o644
            archive.writestr(info, path.read_bytes())

    # Validate what was written.
    with zipfile.ZipFile(output) as archive:
        broken = archive.testzip()
        if broken:
            fail(f"corrupt entry in package: {broken}")
        names = archive.namelist()
        if "mod.json" not in names:
            fail("mod.json missing from package")
        packaged = json.loads(archive.read("mod.json"))
        if packaged.get("id") != mod_id or packaged.get("version") != version:
            fail("mod.json inside package does not match the repository")

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    if not args.no_checksum:
        checksum_path = output.with_suffix(output.suffix + ".sha256")
        checksum_path.write_text(f"{digest}  {output.name}\n", encoding="utf-8")

    print(f"Package : {output}")
    print(f"Mod     : {meta.get('name')} ({mod_id} {version})")
    print(f"Size    : {output.stat().st_size:,} bytes")
    print(f"SHA-256 : {digest}")
    print("Contents:")
    for arcname in sorted(entries):
        tag = " [binary]" if arcname in binaries else ""
        print(f"  {arcname}{tag}")
    if binaries:
        print(f"Platform binaries : {', '.join(sorted(binaries))}")
    else:
        print("warning: no platform binaries included — this is a metadata/source package, "
              "not an installable mod. Add the CI build output with --binaries and use --strict.", file=sys.stderr)
    print(f"Built   : {datetime.now().isoformat(timespec='seconds')}")


if __name__ == "__main__":
    main()
