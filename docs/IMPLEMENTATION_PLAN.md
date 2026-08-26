# Implementation plan

The phases are ordered to retire API and timing risk before investing in the full dock. A phase is complete only when its listed evidence exists.

Current progress: the Windows x64 MVP implementation is present. The official OBS 32.2.2/Qt scaffold configures successfully, the complete plugin builds with MSVC warnings treated as errors, and timeline, SQLite recovery/Unicode, split-span, and CSV tests pass. Live OBS output/hotkey compatibility-matrix tests and cross-platform runtime validation remain before a public release.

## Phase 0 — Build and API feasibility

1. Import the official `obsproject/obs-plugintemplate` build, packaging, formatting, and CI structure.
2. Pin OBS Studio 32.2.2 and the matching 2026-07-15 OBS/Qt dependencies with verified archive hashes.
3. Enable frontend API and Qt; use C++17 and warnings-as-errors in CI.
4. Build a minimal load/unload module and an empty dock on Windows x64.
5. Add a temporary diagnostic event logger for recording, pause, Replay Buffer, and output signals.
6. Verify initial recording paths and `file_changed(next_file)` across Simple/Advanced Output and MKV/Hybrid MP4 where applicable.
7. Verify plugin hotkey request order, save latency, returned replay paths, and behavior during rapid repeated saves.
8. Verify Replay Buffer behavior while recording is paused, with shared and separate encoders.
9. Verify an off-thread `libavformat` duration probe on every supported recording container.
10. Verify whether `Qt6::Sql` plus `QSQLITE` is deployable from the matching OBS bundle; otherwise select a pinned SQLite amalgamation.

Exit evidence: a compatibility matrix and logs demonstrating each lifecycle transition, plus a clean plugin unload. No timeline UI beyond the empty dock is required.

## Phase 1 — Domain model and timeline engine

1. Define immutable event and entity value types using integer nanoseconds.
2. Implement the capture-session and recording-run state machines.
3. Implement pause interval normalization, including duplicate/out-of-order defensive handling.
4. Implement segment boundaries and cumulative/local time conversion.
5. Implement replay-to-recording span mapping and confidence reasons.
6. Add table-driven tests for normal recording, pauses, restarts, splits, replay-only sessions, boundary-crossing replays, and interrupted sessions.

Exit evidence: pure unit tests cover every timeline example without loading OBS or Qt widgets.

## Phase 2 — Persistence and recovery

1. Create versioned SQLite migrations and a repository interface.
2. Persist lifecycle events transactionally before updating dependent UI state.
3. Implement startup recovery for interrupted sessions and abandoned requests.
4. Add repository tests using temporary databases, including migration rollback and UTF-8 paths/notes.
5. Add database backup/export diagnostics suitable for bug reports without media content.

Exit evidence: restart tests reconstruct the same domain state and never discard an unresolved replay.

## Phase 3 — OBS integration and tagged hotkeys

1. Add RAII wrappers for OBS outputs, allocated frontend strings, callback registrations, and signal connections.
2. Implement the callback-to-controller queue with explicit thread-affinity assertions.
3. Register a generic plugin Save Replay hotkey and configurable tagged hotkeys (`Funny`, `Kill`, `Bug`, `Keep` defaults).
4. Correlate saved events to pending requests and catalogue native/external saves separately.
5. Track initial recording files and output `file_changed` splits.
6. Add non-blocking media probe jobs and retry/error states.
7. Exercise start/stop/restart and module unload during every output state.

Exit evidence: an integration session produces a correct database for the Phase 0 compatibility matrix.

## Phase 4 — Review dock

1. Implement session and replay Qt models with proxy search/filtering.
2. Show output/session state, timecodes, tags, notes, paths, probe state, and mapping confidence.
3. Add safe note/tag editing and explicit file/folder actions.
4. Add settings for tag names/colors and explain why plugin hotkeys give more accurate mappings than OBS's built-in hotkey.
5. Persist and restore dock/settings state through OBS-supported module configuration paths.

Exit evidence: all MVP metadata can be created, found, edited, and reviewed without opening the database manually.

## Phase 5 — CSV export and MVP hardening

1. Implement the stable CSV contract and test quoting, Unicode, empty mappings, and multi-segment spans.
2. Add actionable logging with a consistent plugin prefix and no private content paths at normal log levels unless needed.
3. Test missing/moved files, unwritable database/export locations, corrupt media, full disks, rapid hotkeys, and abnormal OBS shutdown.
4. Run format, configure, build, unit tests, packaging, and manual OBS smoke tests in CI-supported environments.
5. Write installation, upgrade, backup, limitation, and troubleshooting documentation.

Exit evidence: packaged Windows x64 MVP plus CI builds for the supported matrix, with a published list of known limitations.

## MVP acceptance scenarios

1. A 60-second replay requested at recording media time 42:15 maps approximately to 41:15–42:15 and records its actual probed duration.
2. A replay requested after a five-minute recording pause maps against media time rather than wall time.
3. A replay crossing a recording file split produces two association spans with the correct physical paths.
4. Recording stop/restart while Replay Buffer remains active creates distinct recording runs in one session.
5. Replay-Buffer-only use creates a searchable replay with empty recording association fields.
6. A replay saved through the OBS built-in hotkey is retained as external/native with visibly lower confidence, not silently assigned a tag.
7. A failed or delayed media probe never removes the replay and can be retried.
8. OBS/plugin restart recovers interrupted sessions and marks unresolved requests explicitly.
9. CSV correctly represents notes with commas/newlines, Unicode paths, replay-only entries, and split-spanning entries.
10. Unloading the plugin while outputs are active disconnects callbacks and releases OBS references without a crash.

## Deferred until after MVP

- thumbnails and embedded preview
- ratings
- Resolve/Premiere marker formats
- disk-space and audio-track validation
- duplicate detection
- optional subclip extraction
- automatic game/application metadata
