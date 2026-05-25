# Contributing

TSEdge is a small embedded time-series storage library written in C11.

Development rules:

- The core library must not use external dependencies.
- The public API is defined in `include/tsedge.h`.
- Do not change the public API without a clear reason.
- Add tests for new behavior or bug fixes.
- Compression must remain lossless.
- Compression tests for `double` values must compare the exact bit representation.
- Do not add SQL, a server, HTTP, MQTT, sockets, or networking APIs.
- Do not add complex features outside the project requirements.
- Before committing, run the build and tests:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
ctest --output-on-failure
```
