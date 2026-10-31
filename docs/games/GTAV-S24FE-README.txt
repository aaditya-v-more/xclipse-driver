GTA V (Legacy) on the Galaxy S24 FE with the Xclipse driver
=============================================================

What's in this zip
  radv-xclipse-26.3.0-devel-xd-4f88558.zip  The GPU driver. Import it as it is; don't unzip it.
  GTAV-S24FE.config.json                    GameNative settings for GTA V: driver, DXVK, Proton,
                                            CPU emulation and environment.
  commandline.txt                           Makes the game fill the screen (1404x648, fullscreen).

Before you start
  - A Galaxy S24 FE with the Exynos 2400e (model SM-S721B). The Snapdragon models (SM-S721U,
    SM-S721W, SM-S7210) can't use this driver.
  - GameNative v1.3.0-prerelease:
    https://github.com/utkarshdalal/GameNative/releases/tag/v1.3.0-prerelease
    Other versions may name things differently.
  - GTA V Legacy on Steam, and about 120 GB free, so the 256 GB or 512 GB phone (the S24 FE has
    no SD card slot).
  - Use the Steam copy, not Epic. GameNative's Rockstar sign-in support is for Steam games
    only. An Epic copy starts Rockstar's own launcher, whose installer failed on an S24 FE with
    "Please make sure all instances of the Rockstar Games Launcher have been closed". Force-stopping
    GameNative and launching again may get past it once, but the Epic copy hasn't been seen to
    reach the game.
  - A Rockstar Games account. GameNative asks you to sign in the first time the game starts.
  - Story Mode only. GTA Online doesn't work under Wine (BattlEye).

Steps
  1. Unzip this on the phone, for example into Download/GTAV/. Leave the driver zip inside it
     zipped.
  2. In GameNative, sign in to Steam and install Grand Theft Auto V Legacy.
  3. Import the driver: Menu (top right) > Settings > Emulation > Driver Manager >
     Import ZIP from device > pick radv-xclipse-26.3.0-devel-xd-4f88558.zip.
  4. Open GTA V's page > Options > Import config > pick GTAV-S24FE.config.json.
     Let GameNative download anything it says is missing (Proton, DXVK, FEXCore).
  5. Open GTA V's container settings and set Screen Size to Custom, 1404x648. (The config
     import doesn't carry the screen size.) On the General tab, check that Launch Steam Client
     is on and Steam Type is Headless; the config sets both. GameNative only shows its
     Rockstar sign-in in that mode (or with Experimental Bionic Steam). Without it the game
     tries to install Rockstar's own launcher and fails. Save.
  6. Options > Import Files > pick commandline.txt, and copy it into the GTA V game folder (the
     one with GTA5.exe and PlayGTAV.exe, under Program Files (x86)/Steam/steamapps/common).
     If you can't find that folder, skip this step and set the game's Settings > Graphics >
     Resolution to 1404x648 and Screen Type to Fullscreen once it's running.
  7. Launch it. Sign in to Rockstar when asked. The first launch takes a few minutes: expect 2-3
     minutes from launch to the prologue.

Settings the config sets, and why
  Graphics Driver        Wrapper, RADV Xclipse (Mesa 26.3.0-devel-xd-4f88558)
  BCn Emulation          none. With auto or full, GTA V crashes while loading.
  DX Wrapper             DXVK 2.7.1 with DXVK_CONFIG=dxvk.enableDescriptorBuffer=False
                         (DXVK 2.7.1 faults at startup with descriptor buffers on)
  Wine / Container       proton-10.0-arm64ec-2, bionic, FEXCore Intermediate, Box64 Compatibility
  Max Device Memory      512 MB
  Quiet logging          WINEDEBUG=-all (logging slows loading)
  Launch Steam Client    on, Steam Type Headless (needed for GameNative's Rockstar sign-in)

If it doesn't start
  - "An error has occurred. Please make sure all instances of the Rockstar Games Launcher have
    been closed": the game is trying to install Rockstar's own launcher. On a Steam copy, turn
    on Launch Steam Client with Steam Type Headless (step 5). On an Epic copy, see "Before you
    start".
  - Check the game files in Steam (verify). A missing audio file once crashed the game while
    loading, with no hint of the cause.
  - Make sure the driver shows as RADV Xclipse in the container's Graphics Driver Version.

Known issue: in 1 of 3 test launches the driver logged one GPU page fault in GTA V's launcher,
before the game itself started. The game was unaffected.

How this was tested: these graphics settings were measured on an S24 FE in the GTA V prologue
against Samsung's driver (see the repository's README). That copy was started directly, not
through Steam, so the Steam and Rockstar sign-in steps above (GameNative's own) were not part of
the test.

Source, results and newer builds: https://github.com/aaditya-v-more/xclipse-driver
