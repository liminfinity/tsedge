# Supported Platforms

TSEdge is primarily a Linux/POSIX project. The Python package currently publishes bundled wheels for Linux and macOS.

## Prebuilt Python wheels

| Platform | Architecture | Wheel status |
|---|---:|---|
| Linux | x86_64 | supported |
| Linux | aarch64 | supported |
| macOS | arm64 | supported |
| macOS | x86_64 | supported |
| Windows | x86_64 | not supported yet |

The Linux wheels contain `tsedge/native/libtsedge.so`. The macOS wheels contain `tsedge/native/libtsedge.dylib`.

## Native C library

The C library targets POSIX-compatible environments and is primarily designed for Linux-based edge devices.

macOS is used as a development and validation platform.

Windows support requires a separate filesystem compatibility layer and DLL build configuration, so it is tracked separately from the first public release.
