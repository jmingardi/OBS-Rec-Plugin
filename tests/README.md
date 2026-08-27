# Test strategy

```text
tests/
  timeline-mapper-test.cpp  pause normalization, clamping, replay-only, and split spans
  repository-test.cpp       v1→v2 migration, recovery, ratings, validation metadata, and Unicode
  csv-exporter-test.cpp     stable 0.2 columns, timecodes, quoting, validation data, and replay-only rows
  replay-path-resolver-test.cpp  moved-file matching, ambiguity refusal, and finalized paths
  media-probe-test.cpp      real WAV duration/audio-stream probing and validation statuses
```

Run all native tests with:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The remaining highest-risk behavior is OBS runtime integration: capture-source metadata availability, destination-volume
selection, output-specific split signals, Replay Buffer save latency, rapid saves, and unload while outputs are active.
These require the manual compatibility matrix described in `docs/IMPLEMENTATION_PLAN.md`; deterministic domain and
storage tests do not use sleeps. The media-probe test has a 15-second CTest timeout so a broken FFmpeg runtime cannot
stall CI indefinitely.
