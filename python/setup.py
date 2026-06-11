from __future__ import annotations

import os
import platform
import sys

from setuptools import setup

try:
    from setuptools.command.bdist_wheel import bdist_wheel as _bdist_wheel
except ImportError:  # Older setuptools keeps bdist_wheel in the wheel package.
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel


def _normalize_macos_platform_tag(plat: str) -> str:
    target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
    if sys.platform != "darwin" or not target:
        return plat

    version = target.split(".")
    major = version[0]
    minor = version[1] if len(version) > 1 else "0"

    normalized = []
    for tag in plat.split("."):
        if not tag.startswith("macosx_"):
            normalized.append(tag)
            continue
        parts = tag.split("_")
        arch = "_".join(parts[3:]) if len(parts) > 3 else platform.machine().replace("-", "_")
        normalized.append(f"macosx_{major}_{minor}_{arch}")
    return ".".join(normalized)


class bdist_wheel(_bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False

    def get_tag(self):
        _python, _abi, plat = super().get_tag()
        return "py3", "none", _normalize_macos_platform_tag(plat)


setup(cmdclass={"bdist_wheel": bdist_wheel})
