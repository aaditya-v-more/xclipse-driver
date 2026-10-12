# Native comparison 2026-10-12T16:22:42

Fixed performance mode: True. CPU affinity: 9.
Start-temperature limit: 50 C. AB/BA pairs: 6 per candidate/test.
Shader disk cache is disabled. Each timed microbenchmark also warms up internally.
Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.
No game-FPS inference is made from these driver timings.

| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| threshold_off | CPU per draw, µs | 6 | 0.0895 | 0.0880 | +2.27% | -1.11..+3.45% |
| threshold_off | CPU per draw with a pipeline switch, µs | 6 | 0.5365 | 0.5530 | -1.75% | -5.62..-1.07% |
| threshold_off | GPU per draw, µs | 6 | 0.3720 | 0.3720 | +0.00% | -0.27..+0.00% |
| threshold_off | GPU per draw with a pipeline switch, µs | 6 | 0.5075 | 0.5085 | -0.20% | -0.98..+0.39% |
| threshold_off | Graphics scratch, GPU ms | 6 | 3.4250 | 3.3505 | +1.18% | -4.71..+110.89% |
| adaptive_128 | CPU per draw, µs | 6 | 0.0900 | 0.0900 | -0.55% | -3.26..+2.22% |
| adaptive_128 | CPU per draw with a pipeline switch, µs | 6 | 0.5365 | 0.5480 | -1.45% | -3.79..-0.54% |
| adaptive_128 | GPU per draw, µs | 6 | 0.3720 | 0.3720 | +0.00% | +0.00..+1.61% |
| adaptive_128 | GPU per draw with a pipeline switch, µs | 6 | 0.5080 | 0.5075 | +0.30% | -0.20..+2.21% |
| adaptive_128 | Graphics scratch, GPU ms | 6 | 3.2850 | 3.4090 | -2.42% | -58.48..+20.95% |
| no_culling | CPU per draw, µs | 6 | 0.0905 | 0.0750 | +21.19% | +18.67..+22.97% |
| no_culling | CPU per draw with a pipeline switch, µs | 6 | 0.5340 | 0.5110 | +4.60% | +0.38..+6.27% |
| no_culling | GPU per draw, µs | 6 | 0.3720 | 0.1210 | +207.44% | +207.44..+207.44% |
| no_culling | GPU per draw with a pipeline switch, µs | 6 | 0.5095 | 0.4425 | +14.82% | +14.06..+15.91% |
| no_culling | Graphics scratch, GPU ms | 6 | 3.5280 | 1.8875 | +86.20% | +82.50..+96.42% |

## Attempts

| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |
|---|---|---|---|---|---|---|
| 1/threshold_off | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 42.0..42.0 |
| 1/threshold_off | culling | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 42.0..42.0 |
| 1/adaptive_128 | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 42.0..43.0 |
| 1/adaptive_128 | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/no_culling | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 45.0..45.0 |
| 1/no_culling | culling | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..46.0 |
| 2/no_culling | culling | no_culling | pass | 400.0..400.0 | 1632.0..1632.0 | 46.0..47.0 |
| 2/no_culling | culling | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 47.0..48.0 |
| 2/adaptive_128 | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 47.0..48.0 |
| 2/adaptive_128 | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 48.0..48.0 |
| 2/threshold_off | culling | threshold_off | pass | 0.0..0.0 | 1632.0..1632.0 | 47.0..47.0 |
| 2/threshold_off | culling | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 48.0..48.0 |
| 3/threshold_off | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 47.0..47.0 |
| 3/threshold_off | culling | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 47.0..47.0 |
| 3/adaptive_128 | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 47.0..47.0 |
| 3/adaptive_128 | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 46.0..47.0 |
| 3/no_culling | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 46.0..47.0 |
| 3/no_culling | culling | no_culling | pass | 0.0..0.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/no_culling | culling | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/no_culling | culling | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/adaptive_128 | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/adaptive_128 | culling | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/threshold_off | culling | threshold_off | pass | 0.0..0.0 | 1632.0..1632.0 | 46.0..46.0 |
| 4/threshold_off | culling | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 46.0..46.0 |
| 5/threshold_off | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 46.0..46.0 |
| 5/threshold_off | culling | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..46.0 |
| 5/adaptive_128 | culling | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 5/adaptive_128 | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 5/no_culling | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 45.0..45.0 |
| 5/no_culling | culling | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 6/no_culling | culling | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 6/no_culling | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 44.0..44.0 |
| 6/adaptive_128 | culling | adaptive_128 | pass | 0.0..0.0 | 1632.0..1632.0 | 44.0..45.0 |
| 6/adaptive_128 | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 44.0..45.0 |
| 6/threshold_off | culling | threshold_off | pass | 0.0..0.0 | 1632.0..1632.0 | 45.0..45.0 |
| 6/threshold_off | culling | baseline | pass | 0.0..0.0 | 1632.0..1632.0 | 45.0..45.0 |
| 1/threshold_off | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..46.0 |
| 1/threshold_off | draws | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 1/adaptive_128 | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 45.0..45.0 |
| 1/adaptive_128 | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 1/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 1/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/adaptive_128 | draws | adaptive_128 | pass | 400.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 2/adaptive_128 | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 2/threshold_off | draws | threshold_off | pass | 400.0..400.0 | 1632.0..1632.0 | 45.0..46.0 |
| 2/threshold_off | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 3/threshold_off | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 3/threshold_off | draws | threshold_off | pass | 400.0..400.0 | 1632.0..1632.0 | 43.0..45.0 |
| 3/adaptive_128 | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 3/adaptive_128 | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..45.0 |
| 3/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 3/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 4/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 4/no_culling | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 4/adaptive_128 | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 4/adaptive_128 | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..45.0 |
| 4/threshold_off | draws | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 4/threshold_off | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..45.0 |
| 5/threshold_off | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..45.0 |
| 5/threshold_off | draws | threshold_off | pass | 400.0..400.0 | 1632.0..1632.0 | 44.0..44.0 |
| 5/adaptive_128 | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 5/adaptive_128 | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 5/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 5/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/no_culling | draws | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/no_culling | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/adaptive_128 | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/adaptive_128 | draws | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/threshold_off | draws | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 44.0..44.0 |
| 6/threshold_off | draws | baseline | pass | 400.0..400.0 | 1632.0..1632.0 | 44.0..44.0 |
| 1/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/threshold_off | scratch | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 1/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/no_culling | scratch | no_culling | pass | 400.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/threshold_off | scratch | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 2/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/threshold_off | scratch | threshold_off | pass | 400.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 3/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/threshold_off | scratch | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 4/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 5/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 5/threshold_off | scratch | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 5/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 42.0..43.0 |
| 5/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 42.0..43.0 |
| 5/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 42.0..43.0 |
| 5/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 6/no_culling | scratch | no_culling | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 6/no_culling | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/adaptive_128 | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..44.0 |
| 6/adaptive_128 | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 42.0..43.0 |
| 6/threshold_off | scratch | threshold_off | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
| 6/threshold_off | scratch | baseline | pass | 0.0..400.0 | 1632.0..1632.0 | 43.0..43.0 |
