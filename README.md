# OBS Replay Timeline

Native C++/Qt OBS Studio plugin that associates saved Replay Buffer clips with their corresponding intervals in a simultaneous long-form recording.

The working product name is **OBS Replay Timeline**. The first release will provide an OBS dock for tagged replay capture, session review, notes, search, and CSV marker export.

## Status

The first native MVP is implemented and builds on Windows x64. It includes the OBS dock, tagged replay hotkeys, recording/pause/split tracking, FIFO request/save correlation, actual-duration probing, split-aware timeline mapping, SQLite recovery, searchable/editable session review, probe retry, and CSV export. Automated tests pass; live OBS compatibility-matrix testing is still required before a public release.

## Use

1. Enable Replay Buffer and optionally start a simultaneous recording.
2. Open **Settings → Hotkeys** and assign the `Replay Timeline: Save Replay — Funny/Kill/Bug/Keep` actions.
3. Open **Docks → Replay Timeline**.
4. Use the plugin hotkeys for precise request timestamps. Replays saved through OBS's built-in hotkey are still catalogued as `External`, with lower mapping confidence.
5. Search sessions, edit tag/note cells, retry failed media probes, or export the selected session to CSV.

Tag names can be changed from the dock. Slot IDs remain stable so existing OBS key assignments survive renamed tags.

## Target

- OBS Studio 32.2.2
- Qt 6 from the matching OBS dependency bundle
- C++17, CMake, and the official OBS plugin-template build layout
- Windows x64 first; macOS and Linux after the event and media-probe behavior is validated
- No Python or Lua in the core plugin

## Build

The build and packaging infrastructure is based on official `obsproject/obs-plugintemplate` commit `3e7d7ac`. Dependency archives are pinned to OBS Studio 32.2.2 and the 2026-07-15 OBS/Qt bundle.

Windows prerequisites:

- Visual Studio 2022 with the Desktop development with C++ workload
- Windows 11 SDK 10.0.22621
- CMake 3.28 through 3.30

Configure and build:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The first configure downloads the pinned OBS sources and dependency bundles into `.deps/`.
The development output is under `build_x64/rundir/RelWithDebInfo`. Release packaging is also available through the imported official template workflows.

## Metadata and privacy

Session metadata is stored in `replay-timeline.sqlite3` under OBS's module configuration directory. The database uses foreign keys, WAL mode, a busy timeout, versioned schema metadata, and restart recovery. Back it up alongside the OBS profile if the review history matters.

Media paths appear in the dock and database by design, but are only emitted to the OBS log at debug level. No media is copied, renamed, or uploaded.

## Current limitations

- Mapping is labeled `approximate` until Replay Buffer pause/shared-encoder behavior is validated across the supported OBS output matrix.
- A recording that was already active when the plugin loaded is tracked only from plugin load onward.
- Split detection uses the standard OBS output `file_changed(next_file)` signal; custom outputs without it cannot produce verified split boundaries.
- The plugin currently targets OBS Studio 32.2 or newer and Windows x64. macOS/Linux CI scaffolding is present but not yet runtime-validated.

## Documents

- [Prior product discussion](OBS_PLUGIN_IDEAS_CONVERSATION.md)
- [Official-source research](docs/RESEARCH.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [Source layout](src/README.md)
- [Test strategy](tests/README.md)

## Scope boundary

The differentiator is replay-to-long-recording timeline association. File renaming and folder organization are not primary features.
