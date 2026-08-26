# OBS Replay Timeline

Planning repository for a native C++/Qt OBS Studio plugin that associates saved Replay Buffer clips with their corresponding intervals in a simultaneous long-form recording.

The working product name is **OBS Replay Timeline**. The first release will provide an OBS dock for tagged replay capture, session review, notes, search, and CSV marker export.

## Status

Architecture and implementation planning only. Main plugin development intentionally starts after the plan is reviewed.

## Target

- OBS Studio 32.2.2
- Qt 6 from the matching OBS dependency bundle
- C++17, CMake, and the official OBS plugin-template build layout
- Windows x64 first; macOS and Linux after the event and media-probe behavior is validated
- No Python or Lua in the core plugin

## Documents

- [Prior product discussion](OBS_PLUGIN_IDEAS_CONVERSATION.md)
- [Official-source research](docs/RESEARCH.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [Planned source layout](src/README.md)
- [Test strategy](tests/README.md)

## Scope boundary

The differentiator is replay-to-long-recording timeline association. File renaming and folder organization are not primary features.
