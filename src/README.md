# Source layout

```text
src/
  plugin/       module entry point and lifetime composition
  obs/          frontend callbacks, recording output signals, and thread handoff
  controller/   capture-session state, tagged hotkeys, correlation, and probe queue
  domain/       pure pause-aware timeline mapping value types and algorithms
  persistence/  SQLite schema, recovery, queries, and transactions
  media/        asynchronous FFmpeg probing and derived thumbnail generation
  ui/           dock, searchable replay model, editing, and embedded OBS Media Source preview
  export/       deterministic UTF-8/RFC 4180 CSV writer
```

The pure timeline engine knows nothing about OBS, Qt, SQLite, or FFmpeg. OBS callbacks copy their data and queue events to the controller; media probes run in the global Qt thread pool and return results to the controller thread.
