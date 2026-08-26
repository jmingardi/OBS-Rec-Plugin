# Test strategy

```text
tests/
  timeline-mapper-test.cpp  pause normalization, clamping, replay-only, and split spans
  repository-test.cpp       migrations, recovery, transactions, and Unicode metadata
  csv-exporter-test.cpp     stable columns, timecodes, RFC 4180 quoting, and replay-only rows
```

Run all native tests with:

```powershell
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

The remaining highest-risk behavior is OBS runtime integration: output-specific split signals, Replay Buffer save latency, pause behavior with shared/separate encoders, rapid saves, and unload while outputs are active. These require the manual compatibility matrix described in `docs/IMPLEMENTATION_PLAN.md`; deterministic tests do not use sleeps.
