# Weather Station Dashboard and TSEdge Diagnostics

Local web application for the TSEdge demo agent.

The application has two pages:

- `/` - end-user weather station dashboard;
- `/diagnostics` - engineering diagnostics for TSEdge.

The weather station dashboard shows current measurements, connection status,
battery, air quality and station events. The diagnostics page shows the write
path:

```text
Sensors -> C agent -> API -> WAL -> buffer -> block -> segment files
```

The website is not a TSEdge server. It reads `live_state.json` and sends
commands through `command.json`. The database remains an embedded C library.

The C agent code lives in `examples/ecopost/`. It is split into modules for
configuration, state, sensors, commands, TSEdge operations, JSON output and
filesystem helper functions.

## Running

Build the C project:

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DTSEDGE_SEGMENT_MAX_BYTES=8192 ..
cmake --build .
ctest --output-on-failure
```

Run the C agent:

```bash
./tsedge_ecopost_agent --live --interval-ms 1000
```

Run the website in another terminal:

```bash
cd demo/system-simulator
npm install
TSEDGE_LIVE_OUTPUT=../../build/ecopost_live_output npm run dev
```

Open:

```text
http://localhost:3000
```

Open diagnostics:

```text
http://localhost:3000/diagnostics
```

## Commands

- Pause;
- Resume;
- Drop connection;
- Restore connection;
- PM2.5 spike;
- Add 1 point;
- Add 100;
- Add 1000;
- Add the selected number of points;
- Batch write;
- Flush buffer;
- Read range;
- AVG;
- MIN/MAX;
- Window aggregation;
- Crash;
- Recover from WAL;
- Delete old data;
- Export CSV;
- Verify database;
- Export CSV for the selected series;
- Create test series `debug.temp`;
- Delete test series `debug.temp`;
- Reset demo.

Commands are written to `ecopost_live_output/command.json`. The C agent reads
the file, executes the command and deletes it.

For CSV export, pass a series:

```json
{"command":"export_csv","series":"pm25.concentration"}
```

For custom writes, pass the exact number of points:

```json
{"command":"append_custom","points":10000}
```

For downsampling, run window aggregation on the selected series:

```json
{"command":"window_aggregate","series":"air.temperature","window_size":1000}
```

The agent calls `tsedge_aggregate_windowed` and stores only a summary in
`live_state.json`: window count, input point count, ratio, query time, global
MIN/MAX/AVG and the first and last window.

## Interface Check

Before the defense/demo presentation:

```bash
cd build
./tsedge_ecopost_agent --live --interval-ms 100
```

Use the website controls:

- "Drop connection" - writes should continue locally;
- "Restore connection" - the connection indicator should turn green again;
- "PM2.5 spike" - an event should appear in the feed;
- "Add 100" - the result panel should show how many points were added;
- "Add" in the custom count field - adds exactly the requested number of points;
- "Batch write" - verifies batch writes through the API;
- "Flush buffer" - buffered points should become zero;
- "Read range", "AVG", "MIN/MAX" - a query result should appear;
- "Window aggregation" - select a series and window size, then inspect the compact summary;
- "Crash" - the agent enters the crashed state;
- "Recover from WAL" - the agent returns after the crash;
- "Delete old data" - old segment files are removed, or the UI reports that none exist;
- "Export CSV" - select a series; the file is prepared only when connected;
- "Verify database" - the panel shows the verification result;
- "Create test series" - creates `debug.temp` and writes points to it;
- "Delete test series" - deletes `debug.temp` without touching weather station series;
- "Export CSV" on the weather station dashboard - shows CSV status.

If the agent is not running, the website should show short startup
instructions instead of a blank page. The website does not connect directly to
the database and is not a TSEdge server.
