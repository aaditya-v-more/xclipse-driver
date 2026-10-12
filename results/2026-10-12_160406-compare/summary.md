# Native comparison 2026-10-12T16:04:06

Fixed performance mode: True. CPU affinity: 9.
Start-temperature limit: 50 C. AB/BA pairs: 6 per candidate/test.
Shader disk cache is disabled. Each timed microbenchmark also warms up internally.
Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.
No game-FPS inference is made from these driver timings.

| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| no_culling | CPU per draw, µs | 6 | 0.1050 | 0.0890 | +17.98% | +17.78..+19.32% |
| no_culling | CPU per draw with a pipeline switch, µs | 6 | 0.5575 | 0.5075 | +9.53% | +5.69..+12.01% |
| no_culling | GPU per draw, µs | 6 | 0.3750 | 0.1210 | +209.92% | +207.44..+211.57% |
| no_culling | GPU per draw with a pipeline switch, µs | 6 | 0.5090 | 0.4495 | +14.73% | +12.14..+15.14% |
| no_culling | Graphics scratch, GPU ms | 6 | 3.4200 | 1.9435 | +73.37% | +59.90..+106.19% |
| no_culling | Compute scratch, GPU ms | 6 | 8.4755 | 9.0065 | -6.33% | -55.81..-1.79% |
| hardware_scratch | CPU per draw, µs | 6 | 0.1060 | 0.1060 | +0.47% | -1.85..+2.83% |
| hardware_scratch | CPU per draw with a pipeline switch, µs | 6 | 0.5475 | 0.5495 | -0.62% | -2.50..+3.49% |
| hardware_scratch | GPU per draw, µs | 6 | 0.3735 | 0.3755 | -0.40% | -1.06..+1.34% |
| hardware_scratch | GPU per draw with a pipeline switch, µs | 6 | 0.5080 | 0.5080 | +0.10% | -0.39..+1.77% |
| hardware_scratch | Graphics scratch, GPU ms | 6 | 3.4675 | 3.5245 | -1.39% | -58.37..+43.84% |
| hardware_scratch | Compute scratch, GPU ms | 6 | 8.7490 | 8.8560 | -1.19% | -13.20..+263.54% |

## Attempts

| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |
|---|---|---|---|---|---|---|
| 1/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 35.0..36.0 |
| 1/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 35.0..36.0 |
| 1/hardware_scratch | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 35.0..36.0 |
| 1/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 2/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 2/hardware_scratch | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 3/no_culling | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 35.0..36.0 |
| 3/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 3/hardware_scratch | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 3/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/hardware_scratch | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 35.0..37.0 |
| 4/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/no_culling | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/hardware_scratch | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 5/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/hardware_scratch | draws | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/hardware_scratch | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/hardware_scratch | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/hardware_scratch | scratch | hardware_scratch | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 2/hardware_scratch | scratch | hardware_scratch | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..38.0 |
| 2/hardware_scratch | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 2/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
| 3/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 3/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..38.0 |
| 3/hardware_scratch | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 3/hardware_scratch | scratch | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 4/hardware_scratch | scratch | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 4/hardware_scratch | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 4/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 4/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 5/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 5/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 5/hardware_scratch | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/hardware_scratch | scratch | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 37.0..37.0 |
| 6/hardware_scratch | scratch | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/hardware_scratch | scratch | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/no_culling | scratch | no_culling | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/no_culling | scratchcs | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..38.0 |
| 1/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 1/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..38.0 |
| 2/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 2/no_culling | scratchcs | no_culling | pass | 400.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 2/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 3/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 3/no_culling | scratchcs | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 3/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 3/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 4/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/no_culling | scratchcs | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 4/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/no_culling | scratchcs | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..36.0 |
| 5/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 5/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/hardware_scratch | scratchcs | hardware_scratch | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/hardware_scratch | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/no_culling | scratchcs | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
| 6/no_culling | scratchcs | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 36.0..37.0 |
