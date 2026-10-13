# Native comparison 2026-10-13T17:00:25

Fixed performance mode: True. CPU affinity: 9.
Start-temperature limit: 45.0 C. AB/BA pairs: 6 per candidate/test.
Shader disk cache is disabled. Each timed microbenchmark also warms up internally.
Positive gain means faster. Ranges are the observed paired gains, not confidence intervals.
No game-FPS inference is made from these driver timings.

| Candidate | Test / metric | Valid pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| adaptive_128_noflush | CPU per draw, µs | 6 | 0.0910 | 0.0780 | +17.42% | +15.19..+20.51% |
| adaptive_128_noflush | CPU per draw with a pipeline switch, µs | 6 | 0.5595 | 0.5305 | +5.10% | +1.82..+8.87% |
| adaptive_128_noflush | GPU per draw, µs | 6 | 0.3720 | 0.1210 | +207.44% | +207.44..+207.44% |
| adaptive_128_noflush | GPU per draw with a pipeline switch, µs | 6 | 0.5035 | 0.4845 | +4.03% | +2.43..+5.25% |
| adaptive_128_noflush | Graphics scratch, GPU ms | 6 | 3.5785 | 3.6030 | +3.00% | -4.01..+16.89% |

## Attempts

| Pair | Test | Variant | Status | GPU MHz range | X4 MHz range | GPU C range |
|---|---|---|---|---|---|---|
| 1/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..0.0 | 1632.0..1632.0 | 38.0..38.0 |
| 1/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 2/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 2/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 3/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..0.0 | 1632.0..1632.0 | 38.0..38.0 |
| 3/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 4/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..39.0 |
| 4/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..39.0 |
| 5/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..39.0 |
| 5/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..39.0 |
| 6/adaptive_128_noflush | culling | adaptive_128_noflush | pass | 0.0..0.0 | 1632.0..1632.0 | 38.0..38.0 |
| 6/adaptive_128_noflush | culling | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..38.0 |
| 1/adaptive_128_noflush | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..39.0 |
| 1/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 38.0..39.0 |
| 2/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..40.0 |
| 2/adaptive_128_noflush | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..40.0 |
| 3/adaptive_128_noflush | draws | adaptive_128 | pass | 400.0..400.0 | 1632.0..1632.0 | 40.0..41.0 |
| 3/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..40.0 |
| 4/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..40.0 |
| 4/adaptive_128_noflush | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 5/adaptive_128_noflush | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 5/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 6/adaptive_128_noflush | draws | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 6/adaptive_128_noflush | draws | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..41.0 |
| 1/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 1/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 2/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 400.0..400.0 | 1632.0..1632.0 | 40.0..41.0 |
| 2/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..41.0 |
| 3/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 3/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 4/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 4/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 5/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 5/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 39.0..40.0 |
| 6/adaptive_128_noflush | scratch | adaptive_128_noflush | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
| 6/adaptive_128_noflush | scratch | adaptive_128 | pass | 0.0..400.0 | 1632.0..1632.0 | 40.0..40.0 |
