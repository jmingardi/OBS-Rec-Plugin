# Official OBS research

Research date: 2026-08-25

Only official OBS documentation and `obsproject` source repositories are used for implementation decisions below.

## Supported baseline

The current stable OBS release is 32.2.2. The project should compile against that tag and must not compile against newer OBS headers while claiming 32.2.2 compatibility; OBS 32 prevents loading plugins built for a newer release.

The official plugin template currently specifies:

- CMake 3.28 through 3.30
- Visual Studio 2022 on Windows
- Xcode 16 on macOS
- Ninja and Ubuntu 24.04 on Linux
- `libobs`, `obs-frontend-api`, and optional Qt 6 targets
- Qt `Core` and `Widgets` with `AUTOMOC`, `AUTOUIC`, and `AUTORCC`

The template's build machinery is the correct base, but its sample `buildspec.json` is stale as of this research: it pins OBS 31.1.1 and the 2025-07-11 dependencies. OBS 32.2.2 uses the 2026-07-15 OBS dependency bundles. When implementation starts, copy the template structure and update all source/dependency versions and checksums as one reviewed change.

Use the exact Qt 6 distribution from the matching OBS dependency bundle instead of a separately installed Qt SDK. This avoids Qt ABI and deployment mismatches. OBS itself currently uses C++17, so the plugin should also use C++17 without compiler extensions.

## Public API feasibility

### Dock

`obs_frontend_add_dock_by_id()` adds a `QWidget` to OBS and a toggle to the Docks menu. It was introduced in OBS 30, so it is available at the 32.2.2 baseline. The widget must be removed with `obs_frontend_remove_dock()` during module unload.

### Recording lifecycle

The frontend event callback provides:

- `OBS_FRONTEND_EVENT_RECORDING_STARTING`
- `OBS_FRONTEND_EVENT_RECORDING_STARTED`
- `OBS_FRONTEND_EVENT_RECORDING_PAUSED`
- `OBS_FRONTEND_EVENT_RECORDING_UNPAUSED`
- `OBS_FRONTEND_EVENT_RECORDING_STOPPING`
- `OBS_FRONTEND_EVENT_RECORDING_STOPPED`

Use successful `STARTED`/`STOPPED` events as persisted boundaries. `STARTING`/`STOPPING` are useful transient UI states but must not be treated as successful media boundaries.

`obs_frontend_get_recording_output()` returns a new output reference that must be released. `obs_frontend_get_current_record_output_path()` returns the current recording path and must be freed with `bfree()`.

### Recording splits

There is no frontend “file split completed” event. The standard FFmpeg muxer and the hybrid MP4/MOV output register an output signal with this signature:

```text
void file_changed(string next_file)
```

The signal is emitted after the output changes to the generated next filename. The plugin should connect to the active recording output's signal handler after recording starts, copy `next_file` immediately, and marshal the event to its controller thread. Because this signal belongs to output implementations rather than the frontend contract, support must be capability-tested against every supported recording output. Unsupported outputs should continue recording a session with a visible “split tracking unavailable” diagnostic.

`obs_frontend_recording_split_file()` only requests a split; its successful return value does not mean the split completed. It is not a substitute for the output signal.

### Replay request and save

The frontend API provides Replay Buffer start/stop events and `OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED`. After the saved event, `obs_frontend_get_last_replay()` returns an allocated copy of the saved path and must be freed with `bfree()`.

Tagged hotkeys should be registered with `obs_hotkey_register_frontend()`. On the press edge, the plugin records `os_gettime_ns()`, creates a pending request containing the chosen tag, and calls `obs_frontend_replay_buffer_save()`.

OBS's replay output internally records its own monotonic `save_ts` when either its output hotkey or `save()` procedure runs. That timestamp is private output state. The public API exposes the successful save event and final path, but it does not expose a third-party callback for the built-in Save Replay hotkey's press time.

Consequences:

- Plugin-owned untagged and tagged save hotkeys can provide precise request-time association.
- Replays saved with OBS's built-in hotkey can still be catalogued, but their request timestamp is unknown and their mapping confidence must be marked lower.
- The dock must explain this distinction and steer users toward the plugin hotkeys without disabling OBS's native hotkey.

Pending requests should be FIFO and have explicit timeout/failure states. Never silently attach a saved file to an unrelated tag after a timeout or Replay Buffer restart.

### Media duration

The saved replay's configured maximum duration is not its actual duration. Keyframe placement, startup fill, size limits, and container timestamps can produce a shorter file.

Probe the completed file off the OBS/UI thread with FFmpeg `libavformat`, using the FFmpeg build supplied by the matching OBS dependencies. Prefer container duration when valid and fall back to the maximum end timestamp across audio/video streams. Persist both the duration and probe status; failed probes remain reviewable and retryable.

### Threading

Frontend callbacks and output signals must do minimal work. In particular, `file_changed` can originate from an output/muxing thread. Copy callback values before return and enqueue an immutable event to a Qt/controller thread. Media probing and database I/O must never run in an OBS real-time callback or block the OBS UI.

## Sources

- OBS Studio 32.2.2 release: https://github.com/obsproject/obs-studio/releases/tag/32.2.2
- OBS plugin documentation: https://docs.obsproject.com/plugins
- OBS frontend API: https://docs.obsproject.com/reference-frontend-api
- OBS output API: https://docs.obsproject.com/reference-outputs
- OBS callback API: https://docs.obsproject.com/reference-libobs-callback
- Official plugin template: https://github.com/obsproject/obs-plugintemplate
- Plugin template CMake file: https://github.com/obsproject/obs-plugintemplate/blob/master/CMakeLists.txt
- Plugin template build specification: https://github.com/obsproject/obs-plugintemplate/blob/master/buildspec.json
- OBS 32.2 dependency presets: https://github.com/obsproject/obs-studio/blob/32.2.2/CMakePresets.json
- OBS dependency release: https://github.com/obsproject/obs-deps/releases/tag/2026-07-15
- Frontend API header: https://github.com/obsproject/obs-studio/blob/32.2.2/frontend/api/obs-frontend-api.h
- Replay Buffer implementation: https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/obs-ffmpeg/obs-ffmpeg-mux.c
- Hybrid MP4/MOV split implementation: https://github.com/obsproject/obs-studio/blob/32.2.2/plugins/obs-outputs/mp4-output.c
- FFmpeg CMake finder: https://github.com/obsproject/obs-studio/blob/32.2.2/cmake/finders/FindFFmpeg.cmake
- OBS code style and C++ version: https://github.com/obsproject/obs-studio/blob/32.2.2/CODESTYLE.md
