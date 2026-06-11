#!/usr/bin/env python3
from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path


LIBRARY_BY_SYSTEM = {
    "Linux": "libtsedge.so",
    "Darwin": "libtsedge.dylib",
}


def run(command: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=cwd, env=env, check=True)


def remove_old_native_libraries(native_dir: Path) -> None:
    native_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.dylib", "*.so", "*.dll"):
        for path in native_dir.glob(pattern):
            path.unlink()


def find_built_library(build_dir: Path, library_name: str) -> Path:
    matches = sorted(
        path for path in build_dir.rglob(library_name)
        if path.is_file() or path.is_symlink()
    )
    if matches:
        return matches[0]

    searched = "\n".join(str(path) for path in sorted(build_dir.rglob("*")))
    raise SystemExit(
        f"Could not find {library_name} under {build_dir}.\n"
        f"Build tree contents:\n{searched}"
    )


def cmake_configure_args(repo_root: Path, build_dir: Path) -> list[str]:
    system = platform.system()
    args = [
        "cmake",
        "-S",
        str(repo_root),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
    ]

    if system == "Darwin":
        target = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0")
        os.environ["MACOSX_DEPLOYMENT_TARGET"] = target
        args.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={target}")

    return args


def main() -> None:
    system = platform.system()
    if system == "Windows":
        raise SystemExit("Windows wheels are not supported in this workflow yet.")

    library_name = LIBRARY_BY_SYSTEM.get(system)
    if not library_name:
        raise SystemExit(f"Unsupported platform for bundled TSEdge wheel: {system}")

    python_dir = Path(__file__).resolve().parents[1]
    repo_root = python_dir.parent
    build_dir = repo_root / "build-python-wheel"
    native_dir = python_dir / "src" / "tsedge" / "native"
    remove_old_native_libraries(native_dir)

    configure_args = cmake_configure_args(repo_root, build_dir)
    env = os.environ.copy()

    run(configure_args, cwd=repo_root, env=env)
    run(
        ["cmake", "--build", str(build_dir), "--config", "Release", "--target", "tsedge"],
        cwd=repo_root,
        env=env,
    )

    source = find_built_library(build_dir, library_name)
    destination = native_dir / library_name
    shutil.copy2(source, destination)
    print(f"Copied native library: {source} -> {destination}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode) from exc
