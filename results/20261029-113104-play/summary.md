# GTAV play benchmark, 2026-10-29T11:31:04

Scene capture 75 s per run (10 s warm-up, 60 s measured), 3 rounds, variants in turn. `am kill-all` through the load: yes. Cool-down to GPU 42 C, skin 38 C; fixed performance mode: no. Phone: SM-S721B S721BXXUEEZIF, Android 17, GPU max clock 700000 kHz.

Container: Screen Size: 1404x648; Graphics Driver: Wrapper-gamenative; DX Wrapper: dxvk; Box64 Preset: COMPATIBILITY; FEXCore Preset: INTERMEDIATE

Driver: RADV Xclipse (Mesa 26.3.0-devel-xd-4f88558)

| variant | properties | ran | FPS avg | 1% low | p95 ms | p99 ms | stutters/min | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | skin C max | SSIM vs radv |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| radv | driver:RADV Xclipse (Mesa 26.3.0-devel-xd-4f88558), set:BCn Emulation=none | 3/3 | 31.1 | 15.0 | 41.7 | 50.0 | 17.7 | 55 | 371 | 1121 | 800 | 1425 | 1143 | 40.4 | 0.980 (min 0.974, 6 pairs) |
| stock | driver:System, set:BCn Emulation=full | 3/3 | 30.7 (-1.3%) | 8.9 (-40.7%) | 41.7 | 50.0 | 21.0 | 76 | 404 | 1654 | 894 (+11.7%) | 1144 | 1023 | 42.7 | 0.954 (min 0.941, 9 pairs) |

SSIM floor (baseline against itself): min 0.974; GameNative's overlay band (top 10%) is excluded, the on-screen controls are the same in every shot.

GPU page faults before the scene (the sgpu kernel's counters; each resets the GPU and cancels the faulting process's work): radv 1/3 runs.

## Runs

| run | variant | status | to play s | faults to play | FPS avg | 1% low | p95 | p99 | stutters/min | frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | GPU C before | skin C max |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0-radv | radv | ran | 136 | 1 | 31.2 | 16.9 | 41.7 | 50.0 | 22.0 | 1873 | 53 | 406 | 1081 | 768 | 1721 | 987 | 42.0 | 39.8 |
| r0-stock | stock | ran | 198 | 0 | 30.6 | 7.4 | 41.7 | 50.1 | 20.0 | 1832 | 74 | 408 | 1693 | 883 | 849 | 1088 | 41.0 | 42.6 |
| r1-stock | stock | ran | 205 | 0 | 30.5 | 7.6 | 41.7 | 50.0 | 19.0 | 1832 | 74 | 409 | 1700 | 917 | 1299 | 994 | 42.0 | 42.7 |
| r1-radv | radv | ran | 136 | 0 | 31.1 | 14.8 | 41.7 | 50.0 | 12.0 | 1866 | 51 | 366 | 1186 | 834 | 1127 | 1305 | 41.0 | 40.8 |
| r2-radv | radv | ran | 128 | 0 | 31.1 | 13.3 | 41.7 | 50.0 | 19.0 | 1864 | 62 | 340 | 1096 | 798 | 1427 | 1137 | 41.0 | 40.7 |
| r2-stock | stock | ran | 199 | 0 | 31.0 | 11.7 | 41.7 | 50.0 | 24.0 | 1863 | 80 | 394 | 1569 | 881 | 1284 | 988 | 41.0 | 42.8 |
