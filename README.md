# OBS Replay Timeline

Native C++/Qt OBS Studio plugin that associates saved Replay Buffer clips with their corresponding intervals in a simultaneous long-form recording.

The working product name is **OBS Replay Timeline**. The first release will provide an OBS dock for tagged replay capture, session review, notes, search, and CSV marker export.

## Status

The native MVP and the 0.2 feature set are implemented and build on Windows x64. The plugin includes the OBS dock, tagged replay hotkeys, recording/pause/split tracking, FIFO request/save correlation, actual-duration and audio-track probing, split-aware timeline mapping, SQLite recovery, searchable/editable session review, ratings, application metadata, disk-space warnings, probe retry, and CSV export. Automated tests pass; the 0.2 additions still require live OBS testing before they are merged from `test` to `main`.

## Use

1. Enable Replay Buffer and optionally start a simultaneous recording.
2. Open **Settings → Hotkeys** and assign the `Replay Timeline: Save Replay — Funny/Kill/Bug/Keep` actions.
3. Open **Docks → Replay Timeline**.
4. Use the plugin hotkeys for precise request timestamps. Replays saved through OBS's built-in hotkey are still catalogued as `External`, with lower mapping confidence.
5. Search replay rows, edit tag/note/rating cells, retry failed media probes, or export the selected session to CSV. A
   rating of `0` means unrated; valid ratings are whole numbers from `1` through `5`. Use
   **Export all CSV** to combine every stored session.
6. When Recording and Replay Buffer are stopped, use **Clear sessions…** to remove stored timeline metadata. The
   confirmation explicitly preserves every replay and recording media file.

Tag names can be changed from the dock. Slot IDs remain stable so existing OBS key assignments survive renamed tags.

The dock checks the recording and replay destination volumes every 30 seconds. It shows a red `LOW` warning when the
least-free destination has less than 10 GiB available. New replay probes also count audio streams: `missing` means the
saved replay contains no detectable audio track, while `unknown` means the file has not been probed successfully yet.
For Game Capture and Window Capture sources, new replay requests store the hooked executable, window title, and OBS
source name when OBS exposes them.

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

## Windows release packages

The Windows GitHub Actions job produces both a manual-install ZIP and an Inno Setup installer. The installer defaults
to OBS's recommended system plugin directory, `C:\ProgramData\obs-studio\plugins\obs-replay-timeline`, supports
in-place upgrades and clean binary removal, and deliberately leaves session metadata in the OBS profile untouched.
Select a different destination in the installer for a custom or portable OBS setup.

Every push or merge to `main` builds a Windows ZIP and Inno Setup installer, then publishes a development pre-release
named `OBS Replay Timeline - X.Y.Z`, where `X.Y.Z` comes from `buildspec.json`. Its internal tag remains unique to the
workflow run. The release description contains a short list of pushed commit subjects plus SHA-256 checksums. Pushes to
`test` do not run this release workflow. A semantic version tag matching `buildspec.json` creates the normal draft
multi-platform release after all configured builds succeed. Installers are unsigned until Windows code-signing
credentials are configured, so Windows SmartScreen may display a warning.

## Metadata and privacy

Session metadata is stored in `replay-timeline.sqlite3` under OBS's module configuration directory. The database uses foreign keys, WAL mode, a busy timeout, versioned schema metadata, and restart recovery. Existing databases are migrated in place to add 0.2 fields; previous rows remain intact and show unrated/unknown/blank values until edited or reprobed. Back it up alongside the OBS profile if the review history matters.

Media paths appear in the dock and database by design, but are only emitted to the OBS log at debug level. No media is copied, renamed, or uploaded.

If another application immediately moves a saved replay into a game-specific subfolder, the plugin searches beneath
the original OBS output directory for a unique save-time match and stores the relocated path. It also replaces OBS's
initial directory-only recording path with the finalized file path. Older failed rows can be repaired with
**Retry probe** after upgrading. Multiple selected rows can be retried together.

## Current limitations

- Mapping is labeled `approximate` until Replay Buffer pause/shared-encoder behavior is validated across the supported OBS output matrix.
- A recording that was already active when the plugin loaded is tracked only from plugin load onward.
- Split detection uses the standard OBS output `file_changed(next_file)` signal; custom outputs without it cannot produce verified split boundaries.
- Moved-file recovery intentionally refuses ambiguous matches; in that case leave the row failed instead of linking the wrong media file.
- Automatic application metadata is best-effort. It depends on an active Game Capture or Window Capture source and the
  capture source's `get_hooked` data; older rows and unsupported sources remain blank.
- Audio validation counts media streams but does not judge loudness, channel routing, or whether the expected OBS track
  was selected.
- The 10 GiB disk threshold is fixed in 0.2 and reports the least-free relevant destination volume.
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
