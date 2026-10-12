# Native comparison 2026-10-12T16:08:46

Fixed performance mode: True. CPU affinity: 9.
Start-temperature limit: 50 C. AB/BA pairs: 6 per candidate/test.
Shader disk cache is disabled. Each timed microbenchmark also warms up internally.
Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.
No game-FPS inference is made from these driver timings.

| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| log_guard | CPU per draw, µs | 6 | 0.1055 | 0.0905 | +17.68% | +12.90..+20.22% |
| log_guard | CPU per draw with a pipeline switch, µs | 6 | 0.5480 | 0.5370 | +2.05% | +0.93..+5.81% |
| log_guard | GPU per draw, µs | 6 | 0.3735 | 0.3760 | -0.53% | -1.06..+1.07% |
| log_guard | GPU per draw with a pipeline switch, µs | 6 | 0.5080 | 0.5065 | +0.11% | -3.26..+1.79% |

## Attempts

| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |
|---|---|---|---|---|---|---|
| 1/log_guard | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 35.0..36.0 |
| 1/log_guard | draws | log_guard | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/log_guard | draws | log_guard | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/log_guard | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 3/log_guard | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 3/log_guard | draws | log_guard | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/log_guard | draws | log_guard | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 4/log_guard | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
| 5/log_guard | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
| 5/log_guard | draws | log_guard | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
| 6/log_guard | draws | log_guard | pass | 400.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/log_guard | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
