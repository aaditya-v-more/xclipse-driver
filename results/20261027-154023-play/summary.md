# LifeIsStrange3 play benchmark, 2026-10-27T15:40:23

Scene capture 75 s per run (10 s warm-up, 60 s measured), 2 rounds, variants in turn. `am kill-all` through the load: yes. Cool-down to GPU 42 C, skin 38 C; fixed performance mode: no. Phone: SM-S721B S721BXXUEEZIF, Android 17, GPU max clock 800000 kHz.

Container: Screen Size: 1280x720; Graphics Driver: Wrapper-gamenative; DX Wrapper: dxvk; Box64 Preset: COMPATIBILITY; FEXCore Preset: INTERMEDIATE

Driver: RADV Xclipse (Mesa 26.3.0-devel-xd-299e141)

| variant | properties | ran | FPS avg | 1% low | p95 ms | p99 ms | stutters/min | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | skin C max | SSIM vs default |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| default | debug.radv_xclipse_log=1 | 2/3 | 23.3 | 6.9 | 66.6 | 104.1 | 311.7 | 98 | 290 | 1558 | 2221 | 472 | 772 | 43.1 | 0.955 (min 0.955, 2 pairs) |
| gpuswap100 | debug.radv_xclipse_gpuswap=100, debug.radv_xclipse_log=1 | 2/2 | 22.2 (-4.8%) | 8.1 (+17.3%) | 66.6 | 91.6 | 315.1 | 96 | 299 | 1347 | 2221 (+0.0%) | 520 | 739 | 43.3 | 0.431 (min 0.428, 4 pairs) |

SSIM floor (baseline against itself): min 0.955; GameNative's overlay band (top 10%) is excluded, the on-screen controls are the same in every shot.

## Runs

| run | variant | status | to play s | FPS avg | 1% low | p95 | p99 | stutters/min | frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | GPU C before | skin C max |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0-default | default | killed-low-memory | 96 | - | - | - | - | - | - | - | - | - | - | - | - | 34.0 | - |
| r0-default-retry | default | ran | 126 | 23.6 | 7.0 | 66.6 | 108.3 | 284.1 | 1415 | 98 | 295 | 1672 | 2223 | 448 | 793 | 41.0 | 43.3 |
| r0-gpuswap100 | gpuswap100 | ran | 106 | 23.2 | 7.9 | 66.6 | 99.9 | 277.6 | 1387 | 97 | 297 | 1395 | 2223 | 540 | 754 | 41.0 | 43.3 |
| r1-gpuswap100 | gpuswap100 | ran | 112 | 21.2 | 8.3 | 66.7 | 83.3 | 352.7 | 1273 | 95 | 301 | 1298 | 2219 | 499 | 724 | 41.0 | 43.3 |
| r1-default | default | ran | 116 | 23.1 | 6.7 | 66.6 | 99.9 | 339.2 | 1387 | 98 | 286 | 1443 | 2219 | 497 | 752 | 41.0 | 43.0 |

## Failed runs

- r0-default (default): killed-low-memory. 10-27 15:41:52.608   609   609 I lmkd    : Reclaim 'app.gamenative' (13971), uid 10402, oom_score_adj 0, state 2(8) to free 54304kB rss, 23292kB anon rss, 142840kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 2
