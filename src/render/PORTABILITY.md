# Portability rules for the renderer

Every rule here is enforced by the build or by a test, because a rule that
is only written down is a rule that drifts. If you add a file, it inherits
all of them.

## The build enforces these

| Rule | How | Why |
|---|---|---|
| C99, no compiler extensions | `CMAKE_C_EXTENSIONS OFF` gives `-std=c99`, not `-std=gnu99` | A POSIX-only call (`strdup`, `strcasecmp`, `<unistd.h>`) fails here rather than on someone else's platform. |
| Strict warnings on our code only | `sc2k_warnings` interface target | `-Wconversion` catches a silent narrowing that becomes an off-by-one on screen. |
| Vendored headers are `SYSTEM` | `target_include_directories(... SYSTEM ...)` | jsmn is compiled into our translation unit; without this its warnings arrive wearing our flags. |
| No hardcoded paths | `SC2K_ASSETS` cache variable, `CMakePresets.json` | The presets configure on macOS, Linux and Windows with no edits. |

## The code follows these

- **Fixed-width types.** `int32_t`, not `int` or `long`. `long` is 32 bits on
  Win64 and 64 bits everywhere else, so it cannot be used for anything that
  is written down or compared across machines.
- **Binary mode, always.** Every `fopen` uses `"rb"` or `"wb"`. A text-mode
  read on Windows eats `\r` and every offset after the first newline is
  wrong.
- **Never cast a struct over a byte buffer.** The game's data is big-endian;
  read it a byte at a time and shift. No `#pragma pack`, no `memcpy` onto a
  struct, no assumption about padding.
- **No variable-length arrays.** MSVC does not support them.
- **Forward slashes in paths.** Win32 accepts them, so there is no need for
  a platform separator.
- **`uint8_t` for data bytes, never `char`.** `char` signedness is
  implementation-defined and differs between x86 and ARM.
- **No floating point in anything that must reproduce exactly.** The
  camera is float; the tile addressing is integer.

## The asset pipeline follows these

`tools/sc2kpack.py` runs wherever the game builds, so:

- **Standard library only.** No Pillow, no numpy. The PNG codec is written
  out in the tool, both directions.
- **`pathlib`, never string concatenation** for paths.
- **Explicit `encoding="utf-8", newline="\n"`** on every text write, so a
  sidecar written on Windows is byte-identical to one written on Linux.
- **Python 3.9 compatible.** No `match`, no `X | Y` annotations.

## The tests that hold it together

```
python3 tools/sc2kpack.py verify        # shapes round-trip through our codec
ctest --test-dir sc2k/render/build/...  # atlas loads; blit_check; invariants
python3 tools/blit_check.py             # our blit() vs $18E96, both mirrors
python3 tools/render_diff.py  CITY      # our blit list vs the game's
python3 tools/pixel_diff.py   CITY --crop x,y,w,h    # PIXELS vs the game's
```

The first two compare this project against itself and can only find internal
disagreement. The last three run the original: `$18E96` needs no toolbox, so
the emulator can point it at a framebuffer and the two pictures compared
directly. That is the only check that has ever found anything the others
could not -- the aircraft shadow reads the destination and rewrites it, so no
list of blits can show it.
