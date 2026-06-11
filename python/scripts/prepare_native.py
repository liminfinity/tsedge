#!/usr/bin/env python3
from __future__ import annotations

import os
import shutil
from pathlib import Path


LIBRARY_NAMES = (
    "libtsedge.dylib",
    "libtsedge.so",
    "tsedge.dll",
)


def candidate_paths(python_dir: Path) -> list[Path]:
    env_path = os.environ.get("TSEDGE_NATIVE_LIBRARY")
    if env_path:
        return [Path(env_path)]

    repo_root = python_dir.parent
    bases = (
        repo_root / "build",
        repo_root / "build" / "src",
        repo_root / "build" / "lib",
        python_dir / ".." / "build",
        python_dir / ".." / "build" / "src",
        python_dir / ".." / "build" / "lib",
    )
    return [base.resolve() / name for base in bases for name in LIBRARY_NAMES]


def find_library(python_dir: Path) -> Path:
    for candidate in candidate_paths(python_dir):
        if candidate.is_file():
            return candidate
    searched = "\n".join(str(path) for path in candidate_paths(python_dir))
    raise SystemExit(
        "Could not find a TSEdge native library.\n"
        "Build the C project first or set TSEDGE_NATIVE_LIBRARY.\n"
        f"Searched:\n{searched}"
    )


def main() -> None:
    python_dir = Path(__file__).resolve().parents[1]
    source = find_library(python_dir)
    native_dir = python_dir / "src" / "tsedge" / "native"
    native_dir.mkdir(parents=True, exist_ok=True)

    destination = native_dir / source.name
    shutil.copy2(source, destination)
    print(f"Copied native library: {source} -> {destination}")


if __name__ == "__main__":
    main()
