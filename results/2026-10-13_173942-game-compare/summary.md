# Game comparison 2026-10-13T17:39:42

Isolate NGG culling on the same fc066b9 RADV / DXVK 2.7.1 Heaven container. Driver-only comparison at fixed performance mode; does not predict normal-mode FPS.

Same container: heaven-radv. Fixed mode: True; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Fixed-mode scores additionally require identical single GPU/X4 clocks
with at least ten active samples per run. This does not lock every CPU cluster or establish normal-mode performance.
Positive gain means faster; range is observed, not a confidence interval.
Visual review: Reviewed all 18 saved screenshots, with original-resolution inspection of the airship lines. Unexplained blue streaks/lines occur in baseline pair 1 at 65 s and pair 3 at 35/65 s; a fine dotted blue line also occurs in no_culling pair 3 at 65 s. Their cause is unproven. Loading screens remain at 35 s in both pair 2 runs and the pair 4 candidate. No rendering-quality improvement or release correctness is established.

Clock review: Pair 1 differs: baseline GPU/X4 400/1632 MHz, candidate 252/1056 MHz; excluded by the post-run clock filter added in 52afc87. Pair 2 matches at 252/1056; pair 3 matches at 400/1632. Other CPU clusters vary. The two retained pairs are different operating points, not a repeatable six-pair result at one clock state.

| Candidate | Metric | Timing pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| no_culling | Average FPS | 2 | 31.58 | 41.29 | +31.17% | +29.21..+33.13% |
| no_culling | 1% low FPS | 2 | 6.15 | 6.92 | +9.74% | +3.74..+15.75% |

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.85 | 8.94 | 358.4 | 60.1 | 47.0 |
| warm-up | no_culling | True (ran) | 49.38 | 9.35 | 387.5 | 66.7 | 50.0 |
| 1/no_culling | baseline | True (ran) | 38.71 | 9.08 | 387.8 | 64.5 | 50.0 |
| 1/no_culling | no_culling | True (ran) | 33.21 | 3.90 | 240.5 | 60.7 | 50.0 |
| 2/no_culling | no_culling | True (ran) | 33.03 | 3.46 | 243.7 | 59.9 | 49.0 |
| 2/no_culling | baseline | True (ran) | 24.81 | 3.34 | 241.0 | 56.9 | 50.0 |
| 3/no_culling | baseline | True (ran) | 38.35 | 8.96 | 391.8 | 64.4 | 50.0 |
| 3/no_culling | no_culling | True (ran) | 49.55 | 10.37 | 391.3 | 68.6 | 50.0 |
| 4/no_culling | no_culling | False (ran) | 32.82 | 3.41 | 246.1 | 60.6 | 50.0 |

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C |
|---|---|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | 968.0 | 35.0 | 48.0 | 2124.0 | 0.8 | 42.7 |
| warm-up | no_culling | 976.0 | 35.0 | 46.0 | 2246.0 | 0.6 | 44.0 |
| 1/no_culling | baseline | 958.0 | 35.0 | 46.0 | 2278.0 | 0.2 | 44.6 |
| 1/no_culling | no_culling | 956.0 | 91.0 | 46.0 | 2110.0 | 0.8 | 44.6 |
| 2/no_culling | no_culling | 942.0 | 35.0 | 45.0 | 2319.0 | 0.8 | 44.6 |
| 2/no_culling | baseline | 951.0 | 35.0 | 47.0 | 2309.0 | 0.2 | 44.4 |
| 3/no_culling | baseline | 948.0 | 35.0 | 48.0 | 2143.0 | 1.1 | 43.9 |
| 3/no_culling | no_culling | 942.0 | 35.0 | 46.0 | 2123.0 | 1.0 | 45.2 |
| 4/no_culling | no_culling | 954.0 | 35.0 | 45.0 | 2223.0 | 0.6 | 45.1 |

Observed GPU/X4 profiles after first surface frame + warm-up, excluding GPU-off samples.
Profiles need not align exactly with the measured frame window. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|
| warm-up | baseline | 400/1632: 112 | excluded |
| warm-up | no_culling | 400/1632: 93 | excluded |
| 1/no_culling | baseline | 400/1632: 95 | clock-excluded |
| 1/no_culling | no_culling | 252/1056: 84 | clock-excluded |
| 2/no_culling | no_culling | 252/1056: 88 | pass |
| 2/no_culling | baseline | 252/1056: 88 | pass |
| 3/no_culling | baseline | 400/1632: 95 | pass |
| 3/no_culling | no_culling | 400/1632: 90 | pass |
| 4/no_culling | no_culling | 252/1056: 84 | excluded |

Stopped: pair4-no_culling-no_culling failed measurement requirements; stopping
