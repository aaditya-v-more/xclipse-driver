# Game comparison 2026-10-12T16:28:19

Isolate NGG culling on the same fc066b9 RADV / DXVK 2.7.1 Heaven container. Driver-only comparison at fixed performance mode; does not predict normal-mode FPS.

Same container: heaven-radv. Fixed mode: True; 90 s measured per run after warm-up.
Each variant has an unscored warm-up run; persistent shader caches are retained.
Only complete accepted pairs count. Positive gain means faster; range is observed, not a confidence interval.
Visual review: PENDING; timings remain provisional

| Candidate | Valid pairs | Baseline FPS median | Candidate FPS median | Paired gain median | Paired range |
|---|---:|---:|---:|---:|---:|
| no_culling | 1 | 38.49 | 49.73 | +29.20% | +29.20..+29.20% |

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|
| warm-up | baseline | True (ran) | 38.26 | 8.94 | 395.9 | 57.6 | 44.0 |
| warm-up | no_culling | True (ran) | 49.72 | 10.51 | 395.6 | 63.1 | 44.0 |
| 1/no_culling | baseline | True (ran) | 38.49 | 7.98 | 400.0 | 61.2 | 45.0 |
| 1/no_culling | no_culling | True (ran) | 49.73 | 10.24 | 391.7 | 65.2 | 45.0 |
| 2/no_culling | no_culling | True (ran) | 49.32 | 9.07 | 391.3 | 65.8 | 46.0 |

Stopped: Stopped at user request during pair 2 baseline; incomplete attempt excluded. Only one complete scored pair; visual review pending.

Measurement limitation: pair 2 no_culling reports 46 C before launch despite the 45 C cooldown limit. Its pair is incomplete. Recheck temperature at the actual launch boundary before a fresh comparison.

Manual cleanup succeeded: GameNative stopped, fixed performance mode released, RADV debug cleared, logging 0, refresh overrides unset, DND off, USB stay-awake 2. See results.json for command return codes and readbacks.
