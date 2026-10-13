# Game comparison 2026-10-13T17:30:17

Isolate NGG culling on the same fc066b9 RADV / DXVK 2.7.1 Heaven container. Driver-only comparison at fixed performance mode; does not predict normal-mode FPS.

Same container: heaven-radv. Fixed mode: True; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Positive gain means faster; range is observed, not a confidence interval.
Visual review: PENDING; timings remain provisional

| Candidate | Valid pairs | Baseline FPS median | Candidate FPS median | Paired gain median | Paired range |
|---|---:|---:|---:|---:|---:|

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.30 | 8.46 | 400.0 | 60.5 | 44.0 |
| warm-up | no_culling | True (ran) | 49.69 | 9.95 | 395.7 | 64.6 | 45.0 |
| 1/no_culling | baseline | False (temperature-not-ready) | 0.00 | 0.00 | 0.0 | 0.0 | 46.0 |

Stopped: pair1-no_culling-baseline failed measurement requirements; stopping
