# Game comparison 2026-10-17T21:14:44

Normal-clock check of the xd-5701dfa defaults (NGG culling off, loop filter emulation on) against culling back on (nggc) and the filter emulation off, in the same Heaven container.

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
| warm-up | baseline | True (ran) | 58.90 | 15.77 | 25.1 | 14.0 | 710.7 | 73.1 | 42.0 | 35.1 |
| warm-up | culling_on | True (ran) | 58.09 | 13.78 | 33.3 | 18.0 | 742.6 | 77.0 | 36.0 | 35.9 |
| warm-up | filteremu0 | True (ran) | 58.59 | 13.97 | 32.7 | 14.0 | 683.9 | 75.2 | 36.0 | 36.0 |
| 1/culling_on | baseline | False (stalled) | 0.00 | 0.00 | 0.0 | 0.0 | 0.0 | 0.0 | 37.0 | 36.0 |

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C | Peak SKIN C | Peak thermal status |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| warm-up | baseline | 963.0 | 100.0 | 57.0 | 1633.0 | 0.7 | 38.9 | 39.8 | 0.0 |
| warm-up | culling_on | 971.0 | 169.0 | 50.0 | 1666.0 | 3.0 | 40.5 | 40.6 | 1.0 |
| warm-up | filteremu0 | 949.0 | 236.0 | 50.0 | 1718.0 | 1.2 | 39.8 | 41.0 | 1.0 |
| 1/culling_on | baseline | 716.0 | 136.0 | 50.0 | 1827.0 | 1.5 | 35.7 | 36.7 | 0.0 |

Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.
Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|
| warm-up | baseline | 315/672: 3, 315/2496: 1, 400/672: 1, 450/672: 1, 650/672: 10, 650/2400: 1, 650/2592: 1, 650/2784: 1, 700/672: 11, 700/2304: 2, 700/2496: 1, 700/2784: 4, 800/672: 21, 800/1056: 1, 800/1824: 1, 800/2016: 4, 800/2304: 4, 800/2400: 4, 800/2496: 3, 800/2592: 4, 800/2688: 1, 800/2784: 8 | excluded |
| warm-up | culling_on | 252/2304: 1, 450/2784: 1, 545/2496: 1, 545/2592: 1, 650/2496: 2, 700/672: 5, 700/1056: 1, 700/2304: 1, 700/2400: 2, 700/2496: 2, 700/2592: 4, 700/2688: 1, 700/2784: 2, 800/672: 36, 800/2112: 2, 800/2208: 1, 800/2304: 1, 800/2400: 3, 800/2496: 1, 800/2592: 6, 800/2688: 6, 800/2784: 7 | excluded |
| warm-up | filteremu0 | 315/672: 1, 350/672: 1, 350/2784: 1, 400/2400: 1, 450/672: 2, 450/2016: 1, 450/2784: 1, 545/672: 1, 545/2304: 1, 545/2400: 1, 600/1728: 1, 600/2304: 1, 600/2400: 1, 600/2496: 1, 650/672: 7, 650/1920: 1, 650/2112: 1, 650/2208: 1, 650/2304: 3, 650/2400: 1, 650/2496: 1, 650/2592: 2, 650/2784: 1, 700/672: 16, 700/1056: 1, 700/1632: 1, 700/1920: 1, 700/2112: 2, 700/2304: 1, 700/2400: 2, 700/2496: 3, 700/2592: 3, 800/672: 12, 800/2016: 3, 800/2304: 1, 800/2400: 2, 800/2496: 1, 800/2592: 2, 800/2688: 3, 800/2784: 3 | excluded |
| 1/culling_on | baseline | missing | excluded |

Stopped: pair1-culling_on-baseline failed measurement requirements; stopping
