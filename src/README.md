# Planned source layout

Main source files will be added only after the implementation plan is reviewed.

```text
src/
  plugin/       module entry point and lifetime composition
  obs/          frontend callbacks, output signals, OBS RAII wrappers
  domain/       entities and immutable events
  core/         session state machine and timeline mapper
  storage/      repository interface and SQLite implementation/migrations
  media/        asynchronous FFmpeg duration probing
  hotkeys/      configurable tagged hotkey registration and correlation
  ui/           dock, Qt models, delegates, and settings dialog
  export/       CSV writer and later editor formats
```

Dependencies point inward: `domain` and the pure timeline engine know nothing about OBS, Qt widgets, SQLite, or FFmpeg. UI and OBS adapters communicate through controller/repository interfaces.
