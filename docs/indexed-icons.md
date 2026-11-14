# Indexed built-in icons

The bitmap icons in `assets/material`, `assets/material_46` and `assets/zbolt`
use `LV_IMG_CF_INDEXED_8BIT`: 256 BGRA palette entries followed by one byte per
pixel. All original colors and alpha values fit exactly; no quantization is
performed. `assets/zbolt/console_img.c` is a font and is unchanged.

## Build requirements

Apply both patches inside the pinned LVGL submodule, in this order:

```sh
patch -d lvgl -p1 < patches/0006-lvgl-draw-indexed-8bit-decoded-images.patch
patch -d lvgl -p1 < patches/0007-lvgl-draw-indexed-8bit-images-whole.patch
```

Then perform a clean build using the normal platform build procedure. Adding
patch files to the main repository does not apply them inside LVGL. Do not
apply them twice. Existing LVGL theme patches can remain in place.

Patch 0006 adds software drawing for indexed whole-image buffers. Its indexed
antialiasing uses the existing true-color sampling algorithm, retaining the
original appearance. Patch 0007 makes the built-in decoder return the static
icon buffer directly, supporting zoom without an expanded image allocation.
The patches do not add a PNG decoder or modify libhv. No other new library
patch is required. Converted icons fail compilation if the 0006 feature
marker is missing; 0007 is also required for the whole-image path.

## Regeneration

```sh
python3 assets/icons_to_indexed.py assets/material/*.c assets/material_46/*.c assets/zbolt/*.c
```

Already converted files and font assets are skipped. More than 256 exact
colors is an error, never a request for lossy conversion.

## Size of all icon arrays in each set (32-bit source)

| Set | Bitmap icons | Before | Indexed | Reduction |
| --- | ---: | ---: | ---: | ---: |
| material | 72 | 1152.0 KiB | 360.0 KiB | 792.0 KiB |
| material_46 | 52 | 429.8 KiB | 159.5 KiB | 270.4 KiB |
| zbolt | 54 | 862.3 KiB | 269.6 KiB | 592.7 KiB |

Only the selected set is linked, and `--gc-sections` discards unused icons.
These reductions are not a prediction of process RSS. `GUPPY_SMALL_SCREEN`
selects material_46; FF5M explicitly selects material, and the zbolt theme
selects zbolt.

## Validation

On the project's 32-bit-color software renderer, all 178 original BGRA
buffers were compared with reconstructed indexed buffers. The converter
reproduced all files, including the unchanged font.

A local rendering check compared original true-color images with indexed
icons: 9 cases per icon, 1602 comparisons total. Cases cover normal size,
zoom, antialiasing, opaque and partial recoloring. All rendered RGB pixels
matched exactly. The built-in decoder's whole-image pointer was also checked
for every icon. Nineteen existing local application checks passed with the new
renderer and material icons. Test infrastructure is not included in this change.

ARM/MIPS target builds, physical display checks and target RSS measurements
remain to be performed. Check the main screen, sensor icon zoom, pressed and
disabled buttons, all relevant panels, and startup/reconnect on the device.
