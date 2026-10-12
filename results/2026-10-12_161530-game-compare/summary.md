# Game comparison 2026-10-12T16:15:30

Isolate NGG culling on the same fc066b9 RADV / DXVK 2.7.1 Heaven container. Driver-only comparison at fixed performance mode; does not predict normal-mode FPS.

Same container: heaven-radv. Fixed mode: True; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Positive gain means faster; range is observed, not a confidence interval.
Visual review: PENDING; timings remain provisional

| Candidate | Valid pairs | Baseline FPS median | Candidate FPS median | Paired gain median | Paired range |
|---|---:|---:|---:|---:|---:|

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.55 | 9.04 | 395.7 | 54.8 | 42.0 |
| warm-up | no_culling | True (ran) | 49.69 | 10.46 | 395.8 | 59.9 | 42.0 |

Stopped: temperature did not settle: 43.0 C
