# OBS Replay Timeline product brief

## Purpose

OBS Replay Timeline is a native OBS Studio plugin for people who record long-form sessions while using Replay Buffer.
It connects each saved replay clip to the corresponding interval in the long recording, making highlights easier to
review, annotate, search, and export.

The plugin observes the normal OBS Recording and Replay Buffer outputs. It does not start a hidden recording, modify
the encoded media, or place work on OBS's real-time audio/video processing paths.

## Primary workflow

1. The user starts Recording, Replay Buffer, or both in OBS.
2. A plugin hotkey records an exact monotonic request time and an optional tag.
3. OBS confirms the saved replay and supplies its media path.
4. The plugin probes the replay's actual duration and audio streams.
5. The replay interval is mapped onto the active recording timeline, accounting for pauses and split files.
6. The user reviews the session in an OBS dock and can edit tags, notes, and ratings or export markers to CSV.

Replay-Buffer-only sessions remain useful: clips are catalogued and reviewable without inventing a long-recording
association.

## Product goals

- Reliably preserve replay metadata even when probing or path resolution fails.
- Make the confidence and limitations of every replay-to-recording mapping visible.
- Support tagged capture without breaking OBS's native Replay Buffer workflow.
- Keep review work inside OBS through search, editing, thumbnails, and embedded playback.
- Preserve existing recordings and replay files during metadata cleanup or plugin upgrades.
- Store all metadata locally and avoid uploading or copying media.

## Current feature set

- Recording, pause, resume, split-file, and Replay Buffer lifecycle tracking.
- FIFO correlation between tagged replay requests and OBS save confirmations.
- Actual-duration and audio-stream probing through FFmpeg.
- Split-aware replay intervals on the long recording timeline.
- SQLite persistence with migrations, restart recovery, and editable tags, notes, and ratings.
- Best-effort captured application, window title, and OBS source metadata.
- Searchable session review and selected-session or all-session CSV export.
- Destination-aware disk-space reporting and low-space warnings.
- Asynchronous cached thumbnails and an embedded, seekable replay preview.
- Monitor-only preview audio that starts muted and never enters recordings or streams.
- Expandable preview focus mode, persistent table/preview sizing, and a separate lifecycle-diagnostics window.

## Mapping model

For a replay requested at recording time `00:42:15` with an actual duration of 60 seconds, the expected interval is
approximately `00:41:15–00:42:15`. Mapping uses monotonic time rather than wall-clock time and subtracts known recording
pauses. When an interval crosses a split-recording boundary, the association records every relevant recording segment.

Native OBS replay saves that do not originate from a plugin tag hotkey are retained as external requests with lower
confidence. Ambiguous file matches remain unresolved rather than linking the wrong media.

## Data and path behavior

- Recording and replay destinations come from the active user's OBS configuration and save events.
- Different folders and drives are supported without hard-coded paths.
- If another application relocates a replay beneath its original OBS output directory, a unique save-time match can
  repair the path.
- Files moved outside the configured output tree may require manual recovery in a future version.
- Session metadata and the derived thumbnail cache live under the current user's OBS module configuration directory.

## Technical direction

- Native C++17 and Qt 6 plugin using the OBS frontend and module APIs.
- SQLite for durable metadata and schema migration.
- OBS's private FFmpeg Media Source for replay playback.
- FFmpeg libraries for media probing and thumbnail decoding.
- Windows x64 first, with macOS and Linux enabled after runtime behavior is validated.

## Scope boundaries

The differentiator is replay-to-long-recording timeline association. Automatic renaming, general file organization,
custom encoding, raw-frame analysis, and AI classification are not primary responsibilities.

Potential later additions include detachable replay preview, editor-specific marker formats, duplicate detection,
manual path repair, and optional subclip extraction.
