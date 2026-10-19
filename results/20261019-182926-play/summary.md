# LifeIsStrange3 play benchmark, 2026-10-19T18:29:26

Scene capture 75 s per run (10 s warm-up, 60 s measured), 1 rounds, variants in turn. `am kill-all` through the load: yes. Cool-down to GPU None C, skin None C. Phone: SM-S721B S721BXXUEEZIF, Android 17, GPU max clock 800000 kHz.

Container: Screen Size: 1280x720; Graphics Driver: Wrapper-gamenative; DX Wrapper: dxvk (Config: 'version=2.7.1,framerate=0,maxDeviceMemory=0,async=0,asyncCache=0,vkd3dVersion=2.14.1,vkd3dLevel=12_1,ddrawrapper=none,csmt=3,gpuName=NVIDIA GeForce GTX 480,videoMemorySize=2048,strict_shader_math=1,OffscreenRenderingMode=fbo,renderer=gl'); WoW64 Mode: true; Box64 Version: 0.4.2; Box64 Preset: COMPATIBILITY; FEXCore Preset: INTERMEDIATE; Env Vars (Container Base): WRAPPER_MAX_IMAGE_COUNT=0 ZINK_DESCRIPTORS=lazy ZINK_DEBUG=compact MESA_SHADER_CACHE_DISABLE=false MESA_SHADER_CACHE_MAX_SIZE=512MB mesa_glthread=true WINEESYNC=1 MESA_VK_WSI_PRESENT_MODE=mailbox TU_DEBUG=noconform VKD3D_SHADER_MODEL=6_0 PULSE_LATENCY_MSEC=144 DXVK_CONFIG=dxvk.enableDescriptorBuffer=False; Env Vars (Final Guest): DXVK_STATE_CACHE_PATH=/data/data/app.gamenative/files/imagefs/home/xuser/.cache DXVK_LOG_LEVEL=none DXVK_CONFIG_FILE=/data/user/0/app.gamenative/files/imagefs/home/xuser/.config/dxvk.conf DXVK_CONFIG=dxvk.enableDescriptorBuffer=False VK_ICD_FILENAMES=/data/user/0/app.gamenative/files/imagefs/usr/share/vulkan/icd.d/wrapper_icd.aarch64.json GALLIUM_DRIVER=zink LIBGL_KOPPER_DISABLE=true ADRENOTOOLS_DRIVER_PATH=/data/user/0/app.gamenative/files/contents/adrenotools/RADV Xclipse (Mesa 26.3.0-devel-xd-ac77309)/ ADRENOTOOLS_HOOKS_PATH=/data/user/0/app.gamenative/files/imagefs/usr/lib ADRENOTOOLS_DRIVER_NAME=vulkan.radeon.so WRAPPER_VK_VERSION=1.3.0 WRAPPER_EXTENSION_BLACKLIST= WRAPPER_VMEM_MAX_SIZE=512 MESA_VK_WSI_PRESENT_MODE=mailbox WRAPPER_RESOURCE_TYPE=auto WRAPPER_DISABLE_PRESENT_WAIT=0 WRAPPER_EMULATE_BCN=0 WRAPPER_USE_BCN_CACHE=0 WRAPPER_BCN_GPU=0 WRAPPER_ASTC_BLOCK=8x8 LC_ALL=en_US.utf8 MESA_DEBUG=silent MESA_NO_ERROR=1 WINEPREFIX=/data/user/0/app.gamenative/files/imagefs/home/xuser/.wine SDL_XINPUT_ENABLED=1 SDL_DIRECTINPUT_ENABLED=1 SDL_JOYSTICK_HIDAPI=1 SDL_JOYSTICK_WGI=0 SDL_JOYSTICK_RAWINPUT=0 SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1 SDL_HINT_FORCE_RAISEWINDOW=0 SDL_ALLOW_TOPMOST=0 SDL_MOUSE_FOCUS_CLICKTHROUGH=1 WINEDEBUG=-all WINEPATH=A:\ WRAPPER_MAX_IMAGE_COUNT=0 ZINK_DESCRIPTORS=lazy ZINK_DEBUG=compact MESA_SHADER_CACHE_DISABLE=false MESA_SHADER_CACHE_MAX_SIZE=512MB mesa_glthread=true WINEESYNC=1 TU_DEBUG=noconform VKD3D_SHADER_MODEL=6_0 PULSE_LATENCY_MSEC=144 PULSE_SERVER=/data/user/0/app.gamenative/files/imagefs/tmp/.sound/PS0

| variant | properties | ran | FPS avg | 1% low | p95 ms | p99 ms | stutters/min | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | skin C max | SSIM vs default |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| default | none | 0/1 | - | - | - | - | - | - | - | - | - | - | - | - | - |

## Runs

| run | variant | status | to play s | FPS avg | 1% low | p95 | p99 | stutters/min | frames | GPU busy % | GPU MHz | X4 MHz | GPU MB max | RSS MB max | avail MB min | GPU C before | skin C max |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0-default | default | killed-low-memory | 125 | - | - | - | - | - | - | - | - | - | - | - | - | 43.0 | - |

## Failed runs

- r0-default (default): killed-low-memory. 10-19 18:31:23.043   614   614 I lmkd    : Reclaim 'app.gamenative' (15875), uid 10402, oom_score_adj 0, state 2(8) to free 62944kB rss, 29368kB anon rss, 130912kB swap, 40405kB dmabuf_pss, 108048kB dmabuf_rss; reason: min2x watermark is breached even after kill; level: 1
