# OBS Replay Timeline version roadmap

Stable baseline: **0.3.3**. Current development target: **0.4.0**.

## Versioning rules

- Use `0.N.0` for a coherent new user-facing capability.
- Use `0.N.1`, `0.N.2`, and `0.N.3` only for corrections and hardening of that capability.
- Skip an unused patch number instead of placing an unrelated feature in it.
- Keep every `0.x.x` release marked as a pre-release.
- Do not assign hosted team services, remote control, audience verification, or AI verification to a release until the
  local products have passed their validation gates.
- Documentation and repository maintenance do not change the product version.

## Planned fix versions

| Fix version | Type | Feature or fix scope | Release gate |
|---|---|---|---|
| `0.3.4` | Patch | Embedded-preview lifecycle, mute/monitoring, thumbnail-cache, and column-state fixes found during broader live OBS testing | Preview survives repeated load, play, mute, expand, hide, dock close, and OBS shutdown cycles without leaking audio or crashing |
| `0.3.5` | Patch | Replay correlation, recording pause/split, moved-file recovery, media probing, and SQLite recovery hardening | Supported Windows recording and Replay Buffer combinations preserve correct timestamps and media paths across restart |
| `0.4.0` | Feature | Editing export pack: DaVinci Resolve and Adobe Premiere marker formats, alongside the existing CSV exports | Controlled sessions import at the correct timeline positions in both editors |
| `0.4.1` | Patch | Fractional-FPS, drop-frame/timebase, escaping, Unicode, and path-handling corrections in editor exports | Golden-file tests cover supported frame rates and troublesome filenames |
| `0.4.2` | Patch | Export validation, destination errors, overwrite behavior, and actionable user feedback | Failed exports never leave a misleading success state or silently incomplete file |
| `0.5.0` | Feature | Advanced replay review: detachable preview and safe deletion of selected timeline metadata while preserving media files | Pop-out/dock state is recoverable, and every destructive metadata action explicitly preserves recordings and replays |
| `0.5.1` | Patch | Detached-window, multi-monitor, audio-monitoring, focus, and shutdown lifecycle corrections | Repeated detach/reattach and monitor changes are stable in live OBS |
| `0.5.2` | Patch | Selection persistence, thumbnail cleanup, database migration, and bulk-operation consistency fixes | Review operations remain atomic and restart-safe with old and current databases |
## Version 0.6.0 and later

The project owner will revise and schedule `0.6.0` and later versions. The existing product briefs remain research inputs,
not committed fix versions.
