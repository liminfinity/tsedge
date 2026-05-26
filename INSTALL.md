# Installing TSEdge From a Release Archive

TSEdge release archives contain the public header, shared and static libraries,
example programs, benchmark utilities, documentation and license text.

## Unpack

```bash
tar -xzf tsedge-0.1.0-*.tar.gz
cd tsedge-0.1.0-*
```

The archive layout is:

```text
include/tsedge.h
lib/libtsedge.so or lib/libtsedge.dylib
lib/libtsedge.a
bin/tsedge_demo
bin/tsedge_bench
bin/file_bench
bin/sqlite_bench, if SQLite was available during build
docs/
README.md
INSTALL.md
LICENSE
```

## Compile an Application

```bash
cc app.c -Itsedge-0.1.0-*/include -Ltsedge-0.1.0-*/lib -ltsedge -o app
```

On Linux, run an application linked with the shared library like this:

```bash
LD_LIBRARY_PATH=tsedge-0.1.0-*/lib ./app
```

On macOS:

```bash
DYLD_LIBRARY_PATH=tsedge-0.1.0-*/lib ./app
```

For a static link, use the static archive directly:

```bash
cc app.c -Itsedge-0.1.0-*/include tsedge-0.1.0-*/lib/libtsedge.a -o app
```
