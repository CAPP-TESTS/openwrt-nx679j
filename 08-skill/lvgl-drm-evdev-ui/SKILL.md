---
name: lvgl-drm-evdev-ui
description: "Use when adding an LVGL UI to a bare DRM/KMS panel app."
version: 1.0.0
author: hermes-kernel-re
license: MIT
metadata:
  hermes:
    tags: [lvgl, drm, kms, evdev, embedded-ui, linux]
    related_skills: [drm-atomic-userspace-bringup, vendor-display-userspace-bringup, linux-driver-authoring]
---

# LVGL on bare DRM/KMS + evdev

## When to Use
- A hand-written C DRM/KMS client already draws/flips frames and reads `/dev/input/eventN`, and you now need menus, sliders, buttons without pulling in GTK/Qt/Wayland.

## What LVGL gives you (v9.6 / master, verified by building)
- Display driver: `src/drivers/display/drm/` — `lv_linux_drm_create()`, `lv_linux_drm_set_file(disp, "/dev/dri/cardX", -1)`, `lv_linux_drm_find_device_path()` (scans `/sys/class/drm`). Uses libdrm's **atomic** API. Three backends via `LV_LINUX_DRM_BACKEND`: `_FBDEV` (dumb buffers, software render, default), `_GBM`, `_EGL`. Set `LV_LINUX_DRM_AUTO_BACKEND 0` when picking one explicitly.
- Input driver: `src/drivers/evdev/` — `lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0")`, `lv_evdev_set_calibration()`, `lv_evdev_set_swap_axes()`, `lv_evdev_discovery_start()` (inotify hotplug).
- Renderer: built-in software renderer (`LV_USE_DRAW_SW`, default 1) — no GPU/EGL/GBM/Wayland/cairo needed.
- Fonts: compiled-in C arrays, Montserrat 8–48 px + `unscii_8/16` + DejaVu Persian/Hebrew + Source Han Sans CJK. No font files, no FreeType. Default `LV_FONT_MONTSERRAT_14`. Source weight: MS14 = 93 KB of C, unscii_8 = 17 KB, CJK ≈ 1 MB each.
- License: MIT (whole project, commercial OK). LVGL Pro tooling is separately licensed.

## Rules
1. **Build LVGL with your own compile line, not its CMake, if you want zero new dependencies.** `env_support/cmake/dependencies/evdev.cmake` hard-`FATAL_ERROR`s when libevdev is missing and `LV_USE_EVDEV=1`, and the docs repeat "the driver always requires libevdev". The driver source (`src/drivers/evdev/lv_evdev.c`) uses only `<linux/input.h>` + inotify and links with libc alone. Build with `gcc ... -ldrm -lm -lpthread` and no `-levdev`.
2. Glob all of `lvgl/src/**/*.c` but exclude `drivers/(sdl|x11|wayland|opengles|windows|uefi|qnx|nuttx)` so their libraries are never referenced.
3. Compile with `-Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections`. Without LTO+gc-sections the same UI is ~1.5× larger (888 KB vs 538 KB x86_64).
4. Use `LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888` for DRM dumb buffers; `LV_COLOR_DEPTH` is deprecated in 9.6 and removed in v10.
5. Enable only the widgets and `LV_FONT_*` you need; each font is real ROM/RAM.
6. On aarch64 enable `LV_DRAW_SW_ASM LV_DRAW_SW_ASM_NEON` — the Kconfig default is `NONE`, so ARM builds leave the NEON blend path unused.
7. Reading an event node needs root or the `input` group; a plain user gets `EACCES` (errno 13) from `lv_evdev_create()`, not a driver bug.
8. Never grab DRM master on a machine running a compositor (KWin/Wayland/Xorg) to test — the modeset fights the live session. Test on the target panel.

## Measured size (aarch64, XRGB8888, dumb buffers, evdev, MS14, -Os+LTO)
- Minimal (label+button): 330 KB stripped, `.text` 284 KB.
- Menu UI (label/button/slider/switch/bar/table/roller, MS 14+20+28): 461 KB stripped, `.text` 440 KB.
- Links `libdrm.so` + `libc` only.

## Version notes
- `master` reports 10.0.0; **v9.6.0 (16 Sep 2026) is the last v9 release** and what the online docs describe. v9.6 made the software blend/transform paths word-at-a-time (~20–25 % faster render). v10 removes `LV_COLOR_DEPTH` and the auto-backend.

## Working recipe
```sh
gcc -Os -w -flto -ffunction-sections -fdata-sections \
    -I lvgl -I lvgl/include $(pkg-config --cflags libdrm) \
    -DLV_CONF_PATH="$PWD/lv_conf.h" \
    -o app $(find lvgl/src -name '*.c' | grep -vE 'drivers/(sdl|x11|wayland|opengles|windows|uefi|qnx|nuttx)') \
    main.c -Wl,--gc-sections -ldrm -lm -lpthread
```
`LV_CONF_PATH` needs the value quoted *inside* the argument: `-DLV_CONF_PATH='"/abs/path/lv_conf.h"'`.
