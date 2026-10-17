# Game comparison 2026-10-17T15:31:42

Isolate NGG culling on the same fc066b9 RADV / DXVK 2.7.1 Heaven container. Driver-only comparison at fixed performance mode; does not predict normal-mode FPS.

Same container: heaven-radv. Fixed mode: True; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Fixed-mode scores additionally require identical single GPU/X4 clocks
with at least ten active samples per run. This does not lock every CPU cluster or establish normal-mode performance.
Positive gain means faster; range is observed, not a confidence interval.
Visual review: PENDING; timings remain provisional

Clock review: See observed GPU/X4 profiles below.

| Candidate | Metric | Timing pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|
| no_culling | Average FPS | 2 | 39.27 | 50.16 | +27.73% | +27.57..+27.89% |
| no_culling | 1% low FPS | 2 | 8.79 | 10.59 | +20.45% | +20.44..+20.46% |

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.91 | 8.37 | 400.0 | 56.2 | 40.0 |
| warm-up | no_culling | True (ran) | 50.17 | 10.62 | 395.6 | 61.8 | 49.0 |
| 1/no_culling | baseline | True (ran) | 39.40 | 8.78 | 391.3 | 60.8 | 47.0 |
| 1/no_culling | no_culling | True (ran) | 50.26 | 10.58 | 395.6 | 64.6 | 48.0 |
| 2/no_culling | no_culling | True (ran) | 50.06 | 10.60 | 391.4 | 64.8 | 49.0 |
| 2/no_culling | baseline | True (ran) | 39.14 | 8.80 | 395.7 | 63.1 | 49.0 |
| 3/no_culling | baseline | True (ran) | 39.48 | 8.54 | 395.7 | 65.2 | 49.0 |
| 3/no_culling | no_culling | True (ran) | 34.96 | 5.35 | 244.0 | 60.3 | 50.0 |
| 4/no_culling | no_culling | True (ran) | 35.34 | 5.39 | 241.1 | 58.6 | 49.0 |
| 4/no_culling | baseline | True (ran) | 36.96 | 7.49 | 368.2 | 62.6 | 50.0 |
| 5/no_culling | baseline | True (ran) | 26.39 | 4.34 | 246.9 | 56.2 | 49.0 |
| 5/no_culling | no_culling | True (ran) | 46.41 | 8.34 | 367.6 | 64.3 | 50.0 |
| 6/no_culling | no_culling | True (ran) | 34.49 | 5.47 | 249.0 | 58.7 | 49.0 |
| 6/no_culling | baseline | True (ran) | 35.50 | 6.68 | 363.1 | 61.1 | 49.0 |

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C |
|---|---|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | 936.0 | 169.0 | 57.0 | 1664.0 | 3.5 | 37.2 |
| warm-up | no_culling | 930.0 | 61.0 | 46.0 | 1617.0 | 3.1 | 39.4 |
| 1/no_culling | baseline | 930.0 | 52.0 | 50.0 | 1832.0 | 2.3 | 40.9 |
| 1/no_culling | no_culling | 964.0 | 36.0 | 45.0 | 1684.0 | 1.5 | 42.5 |
| 2/no_culling | no_culling | 974.0 | 35.0 | 45.0 | 2022.0 | 0.1 | 43.6 |
| 2/no_culling | baseline | 951.0 | 144.0 | 57.0 | 1849.0 | 1.8 | 43.6 |
| 3/no_culling | baseline | 930.0 | 166.0 | 47.0 | 1930.0 | 2.9 | 44.5 |
| 3/no_culling | no_culling | 950.0 | 36.0 | 45.0 | 1888.0 | 1.1 | 44.7 |
| 4/no_culling | no_culling | 954.0 | 35.0 | 45.0 | 2039.0 | 1.1 | 44.3 |
| 4/no_culling | baseline | 941.0 | 128.0 | 48.0 | 1659.0 | 2.0 | 44.5 |
| 5/no_culling | baseline | 942.0 | 36.0 | 48.0 | 1993.0 | 0.4 | 44.6 |
| 5/no_culling | no_culling | 965.0 | 36.0 | 46.0 | 2075.0 | 0.0 | 44.8 |
| 6/no_culling | no_culling | 963.0 | 36.0 | 46.0 | 1844.0 | 0.4 | 44.7 |
| 6/no_culling | baseline | 946.0 | 36.0 | 57.0 | 1760.0 | 0.6 | 44.6 |

Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.
Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|
| warm-up | baseline | 400/1632: 90 | excluded |
| warm-up | no_culling | 400/1632: 89 | excluded |
| 1/no_culling | baseline | 400/1632: 90 | pass |
| 1/no_culling | no_culling | 400/1632: 90 | pass |
| 2/no_culling | no_culling | 400/1632: 91 | pass |
| 2/no_culling | baseline | 400/1632: 91 | pass |
| 3/no_culling | baseline | 400/1632: 92 | clock-excluded |
| 3/no_culling | no_culling | 252/1056: 78, 315/1152: 5 | clock-excluded |
| 4/no_culling | no_culling | 252/1056: 72, 315/1152: 9 | clock-excluded |
| 4/no_culling | baseline | 252/1056: 3, 252/1152: 2, 252/1248: 1, 315/1152: 4, 315/1248: 7, 350/1248: 2, 350/1344: 4, 400/1440: 7, 400/1536: 12, 400/1632: 48 | clock-excluded |
| 5/no_culling | baseline | 252/1056: 79, 315/1056: 5 | clock-excluded |
| 5/no_culling | no_culling | 252/1056: 7, 252/1152: 2, 315/1152: 2, 315/1440: 1, 350/1248: 6, 350/1344: 9, 350/1440: 1, 350/1536: 2, 400/1344: 1, 400/1440: 5, 400/1536: 14, 400/1632: 38 | clock-excluded |
| 6/no_culling | no_culling | 252/1056: 84 | clock-excluded |
| 6/no_culling | baseline | 252/1056: 5, 315/1056: 2, 315/1152: 7, 315/1248: 2, 350/1152: 4, 350/1248: 7, 350/1344: 6, 400/1248: 2, 400/1344: 6, 400/1440: 30, 400/1536: 13, 400/1632: 4 | clock-excluded |
