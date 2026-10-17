# Game comparison 2026-10-17T15:30:15

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

| Pair | Variant | Accepted | FPS | 1% low | GPU MHz | GPU C | Start C |
|---|---|---|---:|---:|---:|---:|---:|

Whole-attempt memory peaks include launch. Summed process RSS can double-count shared pages;
driver-accounted GPU memory can omit imported DMA-BUF allocations. Neither is total physical RAM.
USB battery charge/voltage readings do not measure system power or energy per frame.

| Pair | Variant | Peak RSS MiB | Peak swap MiB | Accounted GPU MiB | Min available MiB | Peak memory PSI % | Peak battery C |
|---|---|---:|---:|---:|---:|---:|---:|

Observed GPU/X4 profiles in the measured window (from rendering start + warm-up; first surface frame for older results), excluding GPU-off samples.
Telemetry aligns with the frame window within one adb round trip. Missing or changing clocks exclude fixed-mode pairs.

| Pair | Variant | GPU/X4 MHz: samples | Timing status |
|---|---|---|---|

Stopped: uiautomator dump failed 4 times: ''
