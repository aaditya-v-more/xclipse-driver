# Game comparison 2026-10-17T22:52:03

Normal-clock check of the xd-85cd575 defaults (NGG culling off, loop filter emulation on) against culling back on (nggc) and the filter emulation off, in the same Heaven container.

Same container: heaven-radv. Fixed mode: False; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Fixed-mode scores additionally require identical single GPU/X4 clocks
with at least ten active samples per run. This does not lock every CPU cluster or establish normal-mode performance.
Positive gain means faster; range is observed, not a confidence interval.
Visual review: PENDING; timings remain provisional

Clock review: See observed GPU/X4 profiles below.

| Candidate | Metric | Timing pairs | Baseline median | Candidate median | Paired gain median | Paired range |
|---|---|---:|---:|---:|---:|---:|

| Pair | Variant | Accepted | FPS | 1% low | p99 ms | >50 ms/min | GPU MHz | GPU C | Start C | Start SKIN C |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 58.71 | 15.31 | 25.3 | 14.0 | 706.4 | 73.6 | 40.0 | 36.0 |
| warm-up | culling_on | True (ran) | 57.79 | 14.49 | 33.3 | 15.3 | 758.4 | 75.7 | 37.0 | 35.9 |
| warm-up | filteremu0 | True (ran) | 57.69 | 13.28 | 33.3 | 14.7 | 659.6 | 72.5 | 40.0 | 36.0 |
| 1/culling_on | baseline | True (ran) | 58.69 | 14.47 | 25.5 | 14.0 | 695.7 | 75.5 | 38.0 | 36.0 |

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C | Peak SKIN C | Peak thermal status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | 949.0 | 38.0 | 46.0 | 1745.0 | 3.0 | 40.6 | 40.6 | 1.0 |
| warm-up | culling_on | 952.0 | 294.0 | 57.0 | 1552.0 | 2.6 | 40.6 | 40.5 | 1.0 |
| warm-up | filteremu0 | 960.0 | 65.0 | 50.0 | 1596.0 | 1.6 | 40.4 | 40.4 | 1.0 |
| 1/culling_on | baseline | 789.0 | 201.0 | 50.0 | 1767.0 | 2.2 | 40.8 | 40.7 | 1.0 |

Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.
Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|
| warm-up | baseline | 350/1632: 1, 350/1728: 1, 350/2112: 2, 400/1824: 1, 450/672: 2, 450/1728: 1, 450/1824: 1, 450/2016: 1, 450/2112: 1, 650/672: 4, 650/2304: 1, 650/2400: 3, 650/2592: 2, 650/2784: 1, 700/672: 11, 700/2400: 2, 700/2592: 1, 700/2688: 2, 700/2784: 5, 800/672: 24, 800/1728: 1, 800/2016: 1, 800/2400: 3, 800/2496: 6, 800/2592: 4, 800/2688: 2, 800/2784: 2 | excluded |
| warm-up | culling_on | 545/672: 1, 600/672: 1, 600/2400: 1, 650/672: 5, 650/2496: 1, 650/2688: 1, 700/672: 3, 700/2016: 1, 700/2208: 1, 700/2400: 1, 700/2496: 3, 700/2592: 1, 700/2688: 1, 700/2784: 1, 800/672: 31, 800/1056: 1, 800/1248: 1, 800/2016: 7, 800/2112: 1, 800/2208: 2, 800/2304: 5, 800/2400: 4, 800/2496: 5, 800/2592: 4, 800/2688: 1, 800/2784: 4 | excluded |
| warm-up | filteremu0 | 315/1728: 1, 350/1728: 2, 450/1536: 2, 450/1632: 2, 450/1728: 5, 500/1632: 6, 545/1920: 1, 600/2688: 1, 650/672: 6, 650/1920: 1, 650/2112: 2, 650/2208: 1, 650/2400: 2, 650/2496: 1, 650/2784: 1, 700/672: 9, 700/2016: 1, 700/2304: 1, 700/2400: 1, 700/2496: 2, 700/2592: 5, 700/2784: 2, 800/672: 14, 800/1920: 1, 800/2016: 1, 800/2112: 3, 800/2208: 1, 800/2304: 1, 800/2400: 1, 800/2496: 3, 800/2592: 1, 800/2784: 2 | excluded |
| 1/culling_on | baseline | 315/672: 2, 350/672: 1, 350/2496: 1, 400/672: 1, 450/1728: 1, 450/2592: 1, 500/2784: 1, 650/672: 9, 650/2496: 1, 700/672: 11, 700/1632: 1, 700/2016: 3, 700/2112: 1, 700/2208: 1, 700/2400: 3, 700/2496: 8, 700/2592: 4, 700/2688: 2, 700/2784: 1, 800/672: 13, 800/1056: 1, 800/1536: 1, 800/1920: 1, 800/2016: 1, 800/2112: 2, 800/2304: 4, 800/2400: 3, 800/2496: 1, 800/2592: 5, 800/2688: 1 | pass |
