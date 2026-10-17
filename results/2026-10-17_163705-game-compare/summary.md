# Game comparison 2026-10-17T16:37:05

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
| no_culling | Average FPS | 5 | 38.98 | 50.16 | +28.29% | +27.54..+33.97% |
| no_culling | 1% low FPS | 5 | 8.75 | 10.64 | +21.62% | +21.57..+31.66% |

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C | Start SKIN C |
|---|---|---|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.93 | 8.76 | 400.0 | 54.4 | 40.0 | 34.0 |
| warm-up | no_culling | True (ran) | 50.10 | 10.72 | 391.3 | 59.6 | 37.0 | 36.0 |
| 1/no_culling | baseline | True (ran) | 38.98 | 8.75 | 395.6 | 56.3 | 37.0 | 35.9 |
| 1/no_culling | no_culling | True (ran) | 50.01 | 10.64 | 391.1 | 61.6 | 38.0 | 36.0 |
| 2/no_culling | no_culling | True (ran) | 50.25 | 10.68 | 395.7 | 59.6 | 37.0 | 36.0 |
| 2/no_culling | baseline | True (ran) | 39.02 | 8.78 | 391.1 | 56.8 | 36.0 | 35.8 |
| 3/no_culling | baseline | True (ran) | 37.46 | 7.90 | 390.9 | 57.2 | 36.0 | 36.0 |
| 3/no_culling | no_culling | True (ran) | 50.18 | 10.40 | 395.7 | 60.0 | 37.0 | 36.0 |
| 4/no_culling | no_culling | True (ran) | 49.64 | 10.57 | 400.0 | 59.9 | 37.0 | 35.9 |
| 4/no_culling | baseline | True (ran) | 38.92 | 8.70 | 395.7 | 57.1 | 37.0 | 35.9 |
| 5/no_culling | baseline | True (ran) | 39.27 | 8.77 | 395.7 | 56.4 | 37.0 | 35.8 |
| 5/no_culling | no_culling | True (ran) | 50.16 | 10.85 | 391.2 | 59.0 | 38.0 | 35.9 |
| 6/no_culling | no_culling | False (stalled) | 0.00 | 0.00 | 0.0 | 0.0 | 37.0 | 36.0 |

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C | Peak SKIN C | Peak thermal status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | 967.0 | 36.0 | 47.0 | 1899.0 | 0.3 | 37.0 | 36.8 | 0.0 |
| warm-up | no_culling | 950.0 | 36.0 | 50.0 | 1771.0 | 0.6 | 38.8 | 38.4 | 0.0 |
| 1/no_culling | baseline | 949.0 | 36.0 | 52.0 | 1839.0 | 0.2 | 38.3 | 37.8 | 0.0 |
| 1/no_culling | no_culling | 904.0 | 171.0 | 50.0 | 1804.0 | 3.8 | 39.3 | 39.2 | 0.0 |
| 2/no_culling | no_culling | 966.0 | 36.0 | 47.0 | 1921.0 | 0.6 | 38.8 | 38.5 | 0.0 |
| 2/no_culling | baseline | 944.0 | 52.0 | 50.0 | 1678.0 | 2.1 | 38.1 | 37.9 | 0.0 |
| 3/no_culling | baseline | 950.0 | 109.0 | 47.0 | 1720.0 | 4.2 | 38.3 | 37.9 | 0.0 |
| 3/no_culling | no_culling | 954.0 | 282.0 | 46.0 | 1821.0 | 2.6 | 38.7 | 38.7 | 0.0 |
| 4/no_culling | no_culling | 957.0 | 36.0 | 50.0 | 1662.0 | 0.6 | 38.5 | 38.6 | 0.0 |
| 4/no_culling | baseline | 944.0 | 194.0 | 50.0 | 1806.0 | 2.8 | 38.1 | 37.9 | 0.0 |
| 5/no_culling | baseline | 774.0 | 209.0 | 47.0 | 1809.0 | 2.4 | 38.1 | 37.8 | 0.0 |
| 5/no_culling | no_culling | 954.0 | 36.0 | 57.0 | 1865.0 | 0.8 | 38.3 | 38.5 | 0.0 |
| 6/no_culling | no_culling | 515.0 | 36.0 | 43.0 | 2210.0 | 0.6 | 36.3 | 36.6 | 0.0 |

Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.
Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|
| warm-up | baseline | 400/1632: 91 | excluded |
| warm-up | no_culling | 400/1632: 90 | excluded |
| 1/no_culling | baseline | 400/1632: 89 | pass |
| 1/no_culling | no_culling | 400/1632: 88 | pass |
| 2/no_culling | no_culling | 400/1632: 92 | pass |
| 2/no_culling | baseline | 400/1632: 88 | pass |
| 3/no_culling | baseline | 400/1632: 86 | pass |
| 3/no_culling | no_culling | 400/1632: 91 | pass |
| 4/no_culling | no_culling | 400/1632: 94 | pass |
| 4/no_culling | baseline | 400/1632: 91 | pass |
| 5/no_culling | baseline | 400/1632: 92 | pass |
| 5/no_culling | no_culling | 400/1632: 89 | pass |
| 6/no_culling | no_culling | missing | excluded |

Stopped: pair6-no_culling-no_culling failed measurement requirements; stopping
