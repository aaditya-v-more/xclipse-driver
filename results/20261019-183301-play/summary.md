# LifeIsStrange3 play benchmark, 2026-10-19T18:33:01

Scene capture 75 s per run (10 s warm-up, 60 s measured), 3 rounds, variants in turn. `am kill-all` through the load: yes. Cool-down to GPU 42 C, skin 38 C. Phone: SM-S721B S721BXXUEEZIF, Android 17, GPU max clock 800000 kHz.

Container: Screen Size: 1280x720; Graphics Driver: Wrapper-gamenative; DX Wrapper: dxvk; Box64 Preset: COMPATIBILITY; FEXCore Preset: INTERMEDIATE

Driver: RADV Xclipse (Mesa 26.3.0-devel-xd-ac77309)

| variant | properties | ran | FPS avg | 1% low | p95 ms | p99 ms | stutters/min | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | skin C max | SSIM vs default |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| default | none | 1/1 | 26.3 | 6.4 | 58.3 | 99.9 | 212.2 | 95 | 307 | 1840 | 2219 | 446 | 741 | 43.0 | - |
| nggc | debug.radv_perftest=nggc | 0/1 | - | - | - | - | - | - | - | - | - | - | - | - | - |
| filteremu0 | debug.radv_xclipse_filteremu=0 | 0/1 | - | - | - | - | - | - | - | - | - | - | - | - | - |

## Runs

| run | variant | status | to play s | FPS avg | 1% low | p95 | p99 | stutters/min | frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | GPU C before | skin C max |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0-default | default | ran | 114 | 26.3 | 6.4 | 58.3 | 99.9 | 212.2 | 1575 | 95 | 307 | 1840 | 2219 | 446 | 741 | 42.0 | 43.0 |
| r0-nggc | nggc | killed-low-memory | 122 | - | - | - | - | - | 0 | - | - | - | 2278 | 0 | 2074 | 41.0 | 42.8 |
| r0-filteremu0 | filteremu0 | killed-low-memory | 119 | - | - | - | - | - | - | - | - | - | - | - | - | 42.0 | - |

## Failed runs

- r0-nggc (nggc): killed-low-memory. 10-19 18:42:01.228   614   614 I lmkd    : Reclaim 'app.gamenative' (16037), uid 10402, oom_score_adj 0, state 2(8) to free 65576kB rss, 36108kB anon rss, 121600kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 1
- r0-filteremu0 (filteremu0): killed-low-memory. 10-19 18:47:16.647   614   614 I lmkd    : Reclaim 'app.gamenative' (31823), uid 10402, oom_score_adj 0, state 2(8) to free 65180kB rss, 27928kB anon rss, 136684kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 2
