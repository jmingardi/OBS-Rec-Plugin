# Architecture

## Product invariant

A replay entry must remain useful even when an exact long-recording match is impossible. It always retains its saved path, tag, note, rating, actual duration, audio validation, request/save times, and an explicit mapping confidence. Long-recording associations are additional spans, never destructive guesses.

## Time model

Store all event times as integer nanoseconds from OBS's monotonic clock (`os_gettime_ns()`). Store UTC wall-clock timestamps only for display, grouping, and recovery; never use wall time for interval arithmetic.

Three clocks must remain distinct:

1. **Monotonic session time**: elapsed real time, immune to system-clock changes.
2. **Recording-run media time**: accumulated time while that recording run is active, excluding recording pauses.
3. **Recording-segment media time**: time local to one physical split file.

For a monotonic instant `t` in a recording run:

```text
run_media_time(t) = (t - run_started) - sum(overlap of pause intervals before t)
```

When a plugin hotkey requests a saved replay at `t_request`, first compute its recording end position from `run_media_time(t_request)`. Once the replay's actual duration `d` is known, the provisional recording interval is:

```text
[max(0, end_position - d), end_position]
```

The interval is then intersected with known recording-segment ranges. A replay can therefore map to zero, one, or multiple physical recording files. The association is represented as rows, not as a single path field.

This calculation is labeled provisional until integration tests validate Replay Buffer behavior across recording pauses and independently configured encoders. If replay media advances during a period absent from the long recording, the mapper must emit multiple overlap spans or lower confidence rather than invent continuous footage.

## Session boundaries

A capture session is the period during which at least one relevant output is active:

- Start a session when recording or Replay Buffer successfully starts and no session is open.
- Keep it open while either output remains active.
- Close it when both outputs have successfully stopped.
- A recording stop/restart while Replay Buffer stays active creates a new recording run inside the same session.
- A Replay-Buffer-only session is valid and produces replays with no long-recording spans.

Unexpected OBS shutdown leaves the session in `interrupted` state. On the next load, recovery closes open intervals at their last durable event and preserves pending requests as `abandoned` rather than deleting them.

## Components

```text
OBS callbacks / output signals / hotkeys
                  |
                  v
            ObsEventBridge
                  |
          immutable domain events
                  v
           SessionController
        /          |           \
TimelineMapper  Repository   MediaProbeQueue
        \          |           /
                  v
              DockModel
                  |
                  v
        Replay Timeline Qt Dock
```

- **ObsEventBridge** owns OBS registrations and retained output references. It copies callback data and performs no storage or media work.
- **SessionController** is the single writer of capture state and enforces the lifecycle state machine.
- **TimelineMapper** is a pure C++ library with deterministic, table-driven unit tests and no OBS/Qt dependency.
- **Repository** owns schema migrations and serialized transactions.
- **MediaProbeQueue** probes duration in the background and posts results back as domain events.
- **DockModel** exposes session/replay rows, search, edits, and status without embedding OBS logic in widgets.

## Persistence

Use SQLite rather than JSON for the MVP. Search, schema evolution, atomic updates, request/save correlation, and multi-segment associations are relational concerns. Keep a single database in the plugin's OBS module config directory and enable foreign keys, WAL mode, and a busy timeout.

The initial schema should contain:

- `schema_migrations`
- `sessions`
- `recording_runs`
- `recording_segments`
- `recording_pauses`
- `replay_requests`
- `replays`
- `replay_recording_spans`
- `tags`

Important fields include monotonic boundaries, UTC display timestamps, segment order/path, replay duration/probe status,
audio-stream count/status, request source, correlation status, notes, rating, capture application/window/source metadata,
and mapping confidence/reason. Schema version 2 adds the rating, audio, and capture metadata columns without rewriting
existing session rows.

Use SQLite on one worker/owner thread. UI queries should return value objects; Qt models must not hold live database cursors. Writes are transactional, and editable notes use debounced updates.

Before choosing Qt SQL, perform a packaging spike to confirm the matching OBS distribution ships the `QSQLITE` driver on all targets. If it does not, link a pinned SQLite amalgamation directly. Do not depend on an unverified system SQLite or silently fall back to JSON.

## Replay correlation

Each plugin hotkey press creates a unique pending request before calling OBS save. A replay-saved event resolves the oldest compatible pending request. Compatibility includes:

- same Replay Buffer generation (incremented on every start)
- request occurred before save confirmation
- request has not timed out or already resolved
- observed path differs from the last resolved replay

If no compatible request exists, store an `external_or_native` replay with unknown request time and lower mapping confidence. If multiple requests are possible, persist the ambiguity instead of choosing by tag recency.

## Recording paths and splits

On `RECORDING_STARTED`, retain the current recording output, read the initial path, and capability-test/connect the output's `file_changed` signal. Each callback closes the previous segment boundary and opens the supplied next path. On `RECORDING_STOPPED`, finalize the current segment and release the output reference.

After files are finalized, media probing may refine segment durations and boundaries. Never rewrite raw event timestamps; store refined values separately so mappings are auditable and migrations are reversible.

## UI outline

The MVP dock contains:

- output status and active session summary
- search box and tag filter
- session selector
- replay table with recording interval, tag, rating, note, application/window, audio validation, replay path, recording
  path(s), duration, and confidence/status
- actions to edit note/tag, open containing folder, copy path/timecode, retry probe, and export CSV
- settings for tags and hotkey guidance
- a destination-volume status that refreshes every 30 seconds and warns below 10 GiB free

Use a model/view table with a proxy filter rather than one widget per row. File opening must require a user action and use Qt desktop services only from the UI thread.

## CSV contract

Export UTF-8 CSV with RFC 4180 quoting and stable columns:

```text
session_id,replay_id,saved_utc,recording_run,recording_segment,run_start,run_end,segment_start,segment_end,tag,note,rating,application_name,window_title,capture_source,replay_duration,audio_tracks,audio_status,replay_path,recording_path,mapping_confidence,probe_status,mapping_reason
```

A replay spanning multiple files produces one row per association span with the same replay ID. `run_start` and
`run_end` locate that span on the cumulative recording-run timeline, while `segment_start` and `segment_end` are local
to the physical file named by `recording_path`. Replay-only entries produce one row with empty recording fields.
`rating` is `0` for unrated entries. `audio_tracks` is empty and `audio_status` is `unknown` until a successful probe;
zero detected streams produces `missing`. Application fields are best-effort snapshots taken when the replay was
requested, not live lookups performed during export.

## Compatibility and failure policy

- Minimum OBS version: 32.2.x for the first release.
- Refuse to load on unsupported older versions with a clear log message.
- Missing split signal: continue with diagnostic status and no false split claim.
- Duration probe failure: retain replay and allow retry.
- Missing/moved media: retain metadata and display path status.
- Database migration failure: open read-only/recovery UI where possible; do not overwrite the database.
- All OBS object ownership and callback disconnection paths are RAII-wrapped and tested during plugin unload.
