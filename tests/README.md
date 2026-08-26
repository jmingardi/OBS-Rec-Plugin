# Test strategy

```text
tests/
  unit/         pure timeline, state-machine, correlation, and CSV tests
  storage/      migrations, recovery, transactions, and Unicode paths
  integration/  adapter tests against supported OBS builds where practical
  fixtures/     tiny generated media fixtures and expected metadata
  manual/       reproducible OBS lifecycle test scripts/checklists
```

The highest-risk behavior is timing and lifecycle logic. Tests should use an injected monotonic clock and explicit event sequences rather than sleeping. Media fixtures must be small, redistributable, and document how they were generated.
