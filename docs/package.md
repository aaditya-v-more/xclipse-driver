# Installing the driver package

The package is one zip, `radv-xclipse-26.3.0-devel-xd-<sha>.zip`, holding `meta.json` and
`vulkan.radeon.so`. It is for the Galaxy S24 FE (Xclipse 940) only. `<sha>` is the commit it was
built from; build it with `driver/build.sh` or `driver\build.cmd` (`pipeline/run.py --build-driver`
sets the tag), or take it from `driver/dist/`.

## Install in GameNative

1. Copy the zip to the phone, for example to `Download/xclipse/`.
2. Menu → Settings → Emulation → **Driver Manager** → **Import ZIP from device**, then pick the zip.
3. Edit the game's container and use the validated settings below. Save.

`pipeline/gn_driver.py` scripts the import and every container setting; see `game-setup.md`.

## Validated container settings

These are the settings measured in `PROGRESS.md` with Unigine Heaven 4.0 on GameNative
v1.3.0-prerelease. Other games may need different ones.

| Setting | Value | Why |
|---|---|---|
| Container Variant | `bionic` | |
| Graphics Driver | Wrapper | |
| Graphics Driver Version | `RADV Xclipse (Mesa 26.3.0-devel-xd-<sha>)` | |
| DX Wrapper | DXVK `2.7.1` | |
| Environment `DXVK_CONFIG` | `dxvk.enableDescriptorBuffer=False` | DXVK 2.7.1 faults at startup with descriptor buffers on |
| BCn Emulation | `none` | The driver samples BC textures natively; GameNative's BCn layer renders them black |
| FEXCore Preset | `Intermediate` | |
| Box64 Preset | `Compatibility` | |

## Defaults and switches

The defaults are the measured best choice for the 940. Each switch is a container environment
variable, or a phone property (`adb shell setprop <name> <value>`) that then applies to every app
until it is cleared with `setprop <name> ""`. The measurements used the properties; the
environment variables are read the same way but have not been checked inside a container.

| Behaviour | Default | Environment variable | Property |
|---|---|---|---|
| Linear filtering emulated for the formats the 940 point-samples (R32F, R16, R16G16 and others) | on | `RADV_XCLIPSE_FILTEREMU=0` turns it off | `debug.radv_xclipse_filteremu` |
| NGG primitive culling | off (it was slower on the 940) | `RADV_PERFTEST=nggc` turns it on | `debug.radv_perftest` |
| Per-shader register and code-size log in logcat (`[SHADER]`) | off | `RADV_XCLIPSE_LOG=1` | `debug.radv_xclipse_log` |

Never set `RADV_DEBUG=nongg`: it hangs the 940.

Leftover `debug.radv*` properties silently change later runs. `pipeline/run.py` warns when any are
set; clear them before measuring.

## Checking it works

- `pipeline/run.py --tests textures,mips,depth,render` runs the correctness tests on both drivers.
- `pipeline/game.py` benchmarks the GameNative containers and saves screenshots. Compare the
  screenshots, not just frame rates: a run that renders wrongly does not count.
