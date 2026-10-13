# Native comparison 2026-10-13T17:02:38

Fixed performance mode: False. CPU affinity: 9.
Start-temperature limit: 45.0 C. AB/BA pairs: 6 per candidate/test.
Shader disk cache is disabled. Each timed microbenchmark also warms up internally.
Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.
No game-FPS inference is made from these driver timings.

| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|

## Attempts

| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |
|---|---|---|---|---|---|---|
| 1/x11 | compute | android | pass | 0.0..252.0 | 672.0..2016.0 | 38.0..39.0 |
| 1/x11 | compute | x11 | pass | 0.0..0.0 | 672.0..672.0 | 37.0..38.0 |
| 2/x11 | compute | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 2/x11 | compute | android | pass | 0.0..252.0 | 672.0..2112.0 | 39.0..39.0 |
| 3/x11 | compute | android | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 3/x11 | compute | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 4/x11 | compute | x11 | pass | 252.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 4/x11 | compute | android | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 5/x11 | compute | android | pass | 0.0..0.0 | 672.0..672.0 | 39.0..40.0 |
| 5/x11 | compute | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 6/x11 | compute | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..40.0 |
| 6/x11 | compute | android | pass | 0.0..0.0 | 672.0..672.0 | 39.0..39.0 |
| 1/x11 | render | android | pass | 0.0..252.0 | 672.0..2304.0 | 39.0..39.0 |
| 1/x11 | render | x11 | pass | 0.0..252.0 | 672.0..2304.0 | 39.0..39.0 |
| 2/x11 | render | x11 | pass | 252.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 2/x11 | render | android | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 3/x11 | render | android | pass | 0.0..0.0 | 672.0..2784.0 | 39.0..39.0 |
| 3/x11 | render | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 4/x11 | render | x11 | pass | 0.0..0.0 | 672.0..672.0 | 39.0..39.0 |
| 4/x11 | render | android | pass | 0.0..252.0 | 672.0..672.0 | 38.0..39.0 |
| 5/x11 | render | android | pass | 0.0..252.0 | 672.0..2304.0 | 39.0..40.0 |
| 5/x11 | render | x11 | pass | 252.0..252.0 | 672.0..2304.0 | 39.0..40.0 |
| 6/x11 | render | x11 | pass | 252.0..252.0 | 672.0..960.0 | 38.0..39.0 |
| 6/x11 | render | android | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 1/x11 | descriptors | android | pass | 252.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 1/x11 | descriptors | x11 | pass | 0.0..0.0 | 672.0..672.0 | 39.0..39.0 |
| 2/x11 | descriptors | x11 | pass | 0.0..0.0 | 672.0..2496.0 | 39.0..40.0 |
| 2/x11 | descriptors | android | pass | 252.0..252.0 | 672.0..672.0 | 39.0..40.0 |
| 3/x11 | descriptors | android | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 3/x11 | descriptors | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..40.0 |
| 4/x11 | descriptors | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 4/x11 | descriptors | android | pass | 0.0..252.0 | 672.0..1728.0 | 39.0..39.0 |
| 5/x11 | descriptors | android | pass | 252.0..252.0 | 672.0..2496.0 | 39.0..40.0 |
| 5/x11 | descriptors | x11 | pass | 0.0..252.0 | 672.0..672.0 | 39.0..40.0 |
| 6/x11 | descriptors | x11 | pass | 252.0..252.0 | 672.0..672.0 | 39.0..39.0 |
| 6/x11 | descriptors | android | pass | 0.0..0.0 | 672.0..2016.0 | 40.0..40.0 |
| 1/x11 | culling | android | pass | 252.0..252.0 | 2016.0..2016.0 | 41.0..41.0 |
| 1/x11 | culling | x11 | pass | 0.0..252.0 | 2016.0..2016.0 | 41.0..42.0 |
| 2/x11 | culling | x11 | pass | 0.0..400.0 | 2016.0..2016.0 | 42.0..43.0 |
| 2/x11 | culling | android | pass | 400.0..400.0 | 2784.0..2784.0 | 43.0..43.0 |
| 3/x11 | culling | android | pass | 400.0..400.0 | 2016.0..2016.0 | 44.0..45.0 |
| 3/x11 | culling | x11 | pass | 400.0..400.0 | 2016.0..2016.0 | 44.0..44.0 |
| 4/x11 | culling | x11 | pass | 0.0..400.0 | 2016.0..2016.0 | 44.0..45.0 |
| 4/x11 | culling | android | pass | 0.0..0.0 | 2016.0..2016.0 | 43.0..44.0 |
| 5/x11 | culling | android | pass | 0.0..400.0 | 2016.0..2016.0 | 43.0..43.0 |
| 5/x11 | culling | x11 | pass | 0.0..400.0 | 2784.0..2784.0 | 44.0..44.0 |
| 6/x11 | culling | x11 | pass | 400.0..400.0 | 1056.0..1056.0 | 45.0..45.0 |

Stopped: cooldown target not reached; comparison stopped
