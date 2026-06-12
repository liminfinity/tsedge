# C Example

Applications use TSEdge by including the public header and linking with the native library.

```c
#include <tsedge.h>
#include <stdio.h>

int main(void) {
    tsedge_db* db = NULL;

    if (tsedge_open("demo_db", &db) != TSEDGE_OK) {
        return 1;
    }

    tsedge_create_series(db, "air.temperature");
    tsedge_append(db, "air.temperature", 1, 10.0);
    tsedge_append(db, "air.temperature", 2, 20.0);

    double avg = 0.0;
    tsedge_aggregate(db, "air.temperature", 1, 3, TSEDGE_AGG_AVG, &avg);
    printf("avg = %.3f\n", avg);

    tsedge_close(db);
    return 0;
}
```

## Compile with `cc`

If TSEdge is installed under `$HOME/.local`:

```bash
cc app.c -I$HOME/.local/include -L$HOME/.local/lib -ltsedge -o app
LD_LIBRARY_PATH=$HOME/.local/lib ./app
```

On macOS:

```bash
cc app.c -I$HOME/.local/include -L$HOME/.local/lib -ltsedge -o app
DYLD_LIBRARY_PATH=$HOME/.local/lib ./app
```

## Build from the repository

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The repository also includes `examples/tsedge_demo.c`, which demonstrates the public API flow.
