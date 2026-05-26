# Release Archive

This document describes how to build a local release archive suitable for
uploading to GitHub Releases.

## Build Locally

From the repository root:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
ctest --output-on-failure
cpack
```

CPack writes a `.tar.gz` archive into the build directory. The file name includes
the project version, operating system and CPU architecture, for example:

```text
tsedge-0.1.0-linux-x86_64.tar.gz
tsedge-0.1.0-macos-arm64.tar.gz
```

## Archive Contents

The archive contains the install tree produced by CMake:

```text
tsedge-0.1.0-<system>-<arch>/
  include/
    tsedge.h
  lib/
    libtsedge.so or libtsedge.dylib
    libtsedge.a
  bin/
    tsedge_demo
    tsedge_bench
    file_bench
    sqlite_bench, if SQLite was found
  docs/
    *.md
  README.md
  INSTALL.md
  LICENSE
```

If SQLite is not found by CMake, `sqlite_bench` is not built and is not included
in the archive. The rest of the release should still build normally.

## Verify the Archive

After `cpack`, inspect the generated archive:

```bash
tar -tzf tsedge-0.1.0-*.tar.gz
```

Optionally unpack it and compile a small application:

```bash
tar -xzf tsedge-0.1.0-*.tar.gz
cc app.c -Itsedge-0.1.0-*/include -Ltsedge-0.1.0-*/lib -ltsedge -o app
LD_LIBRARY_PATH=tsedge-0.1.0-*/lib ./app
```

On macOS, use `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`.

## Upload to GitHub Releases

1. Create a tag for the release, for example `v0.1.0`.
2. Build the archive with the Release commands above.
3. Open the repository on GitHub.
4. Go to Releases and create a new release from the tag.
5. Upload the generated `.tar.gz` file as a release asset.
6. Mention the target platform in the release notes.

## GitHub Actions Artifact

The manual workflow in `.github/workflows/release.yml` builds the project in
Release mode, runs tests, runs CPack and uploads the generated `.tar.gz` file as
a workflow artifact. It does not require publishing tokens and does not create a
GitHub Release automatically.

To use it:

1. Open the Actions tab on GitHub.
2. Select `Build release archive`.
3. Run the workflow manually.
4. Download the `tsedge-release-archive` artifact from the completed workflow.
