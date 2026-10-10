# Game benchmark run 2026-10-11T01:30:33 (stopped early, no results.json)

Heaven once on each driver, then GravityMark. Stopped during GravityMark's first run so the session could hand off.
The containers had `WINEDLLOVERRIDES=msvcr100,msvcp100=b` (Heaven) and `WRAPPER_NATIVE_BCN=1` (all four) for the first time.

| Benchmark | Driver | What happened | Screenshot |
|---|---|---|---|
| heaven | stock | Got past the msvcr100 crash. Unigine stopped at its error box: DXVK refused the DXT1/DXT5 (BC1/BC3) textures and a 2048x2048 D24 depth texture ("Direct3D11 error: invalid arg"). `game.py` printed "ran 8.8 fps over 15 s", but those frames were the error box being drawn, not the benchmark. | `run1-heaven-stock-screen.png` |
| heaven | radv | Created its textures, then hung on a black screen. Wine's log: `radv/amdgpu: Failed to allocate a buffer: size 256 bytes, alignment 4 bytes, domains 8`, which is GDS. `no-frames`. | `run1-heaven-radv-screen.png` |

`heaven-stock-path-error.png` is from the run before (2026-10-11_012151, stopped by hand): with `-data_path A:/`, Heaven looked for `A:/bin/A:/data/heaven_4.0.cfg`. Fixed by passing `../` paths.
