# LifeIsStrange3 play benchmark, 2026-10-19T18:49:16

Scene capture 75 s per run (10 s warm-up, 60 s measured), 3 rounds, variants in turn. `am kill-all` through the load: yes. Cool-down to GPU 42 C, skin 38 C; fixed performance mode: yes. Phone: SM-S721B S721BXXUEEZIF, Android 17, GPU max clock 400000 kHz.

Container: Screen Size: 1280x720; Graphics Driver: Wrapper-gamenative; DX Wrapper: dxvk; Box64 Preset: COMPATIBILITY; FEXCore Preset: INTERMEDIATE

Driver: RADV Xclipse (Mesa 26.3.0-devel-xd-ac77309)

| variant | properties | ran | FPS avg | 1% low | p95 ms | p99 ms | stutters/min | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | skin C max | SSIM vs default |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| default | none | 0/2 | - | - | - | - | - | - | - | - | - | - | - | - | - |
| nggc | debug.radv_perftest=nggc | 0/1 | - | - | - | - | - | - | - | - | - | - | - | - | - |
| filteremu0 | debug.radv_xclipse_filteremu=0 | 0/0 | - | - | - | - | - | - | - | - | - | - | - | - | - |

## Runs

| run | variant | status | to play s | FPS avg | 1% low | p95 | p99 | stutters/min | frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | GPU C before | skin C max |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0-default | default | killed-low-memory | 154 | - | - | - | - | - | - | - | - | - | - | - | - | 41.0 | - |
| r0-default-retry | default | killed-low-memory | 160 | - | - | - | - | - | - | - | - | - | - | - | - | 41.0 | - |
| r0-nggc | nggc | killed-low-memory | 84 | - | - | - | - | - | 23 | - | - | - | 1240 | 878 | 975 | 41.0 | 39.3 |

## Failed runs

- r0-default (default): killed-low-memory. 10-19 18:52:33.418   614   614 I lmkd    : Reclaim 'app.gamenative' (15195), uid 10402, oom_score_adj 0, state 2(8) to free 70848kB rss, 36560kB anon rss, 108688kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 1
- r0-default-retry (default): killed-low-memory. 10-19 18:57:16.324   614   614 I lmkd    : Reclaim 'app.gamenative' (30460), uid 10402, oom_score_adj 0, state 2(8) to free 68092kB rss, 36228kB anon rss, 125080kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 1
- r0-nggc (nggc): killed-low-memory. 10-19 19:01:14.258   614   614 I lmkd    : Reclaim 'app.gamenative' (17832), uid 10402, oom_score_adj 700, state 10(3) to free 90396kB rss, 31252kB anon rss, 123912kB swap, 10200kB dmabuf_pss, 17432kB dmabuf_rss; reason: min2x watermark is breached and swap is low (9199156kB < 5158992kB); level: 2
