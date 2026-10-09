# AtomS3 / StickS3 display simulator

This host tool renders the production `InventoryDisplay.h` with M5GFX's SDL
backend. The generated 128 x 128 captures use the same drawing primitives,
positions, colors, fonts, and text as the firmware.
The `sticks3_arm` environment generates 135 x 240 captures with an example
battery level of 82% so the StickS3 header can be reviewed before flashing.

## macOS

Requirements: Xcode Command Line Tools, PlatformIO, SDL2, and Python with
Pillow. The Xcode IDE is not used. On the first run, macOS may require accepting
the Apple developer tools license with `sudo xcodebuild -license`.

```sh
brew install sdl2
cd tools/display-simulator
HOMEBREW_PREFIX="$(brew --prefix)" pio run -e native_arm
mkdir -p ../../docs/assets/display
INVENTORY_CAPTURE_DIR="$(cd ../../docs/assets/display && pwd)" \
  SDL_VIDEODRIVER=dummy .pio/build/native_arm/program
python3 convert_captures.py ../../docs/assets/display
```

For StickS3 previews, replace `native_arm` with `sticks3_arm` and set
`INVENTORY_CAPTURE_DIR` to a separate output folder. The captured battery
percentage is a simulator fixture; the device reads its value from M5Unified.

Intel macOS can use `-e native` and `.pio/build/native/program`.

## Linux

Install `build-essential`, `libsdl2`, and `libsdl2-dev`. Remove the Homebrew
include/library flags from `platformio.ini`, then use the `native` environment.

## Windows

Follow the upstream LovyanGFX SDL setup and use the native PlatformIO target:

https://github.com/lovyan03/LovyanGFX/blob/master/examples_for_PC/README.md

## Output

The program creates PPM files first so the C++ capture path has no image-library
dependency. `convert_captures.py` converts them to PNG without resizing. If the
default `python3` has no working Pillow installation, use a Python environment
with Pillow installed.

## Save status fixtures and state tests

Each device capture run creates 29 synthetic states, including reader startup,
boot-mode resume, recovery hold, fixed-batch
progress, a read during retry, rejection after storage space frees, two-digit
counts, long codes, and battery values 0%, 100%, and unavailable. `UNSENT`
includes the request in flight. AtomS3 shows batch progress in the bottom panel
while background synchronization runs; StickS3 shows code and progress together.
The foreground read and its result take priority over background completion.

Run the production FIFO and status tests without device or network operations:

```sh
python3 tests/run_tests.py
```

The ACK JSON test requires ArduinoJson 7.2.1. After a PlatformIO firmware build,
the runner finds its installed headers automatically. For a standalone test,
set `ARDUINOJSON_INCLUDE` to the library's `src` directory. Missing headers fail
the test run rather than skipping ACK parsing. CI checks out the same version.
These tests cover durable metadata
failure, the 17th read rejection, FIFO wrap and restart, strict typed ACK fields,
fixed batch progress, and foreground scan ownership. They do not measure
physical scan intervals or confirm a live Google Sheets save.

Reader startup tests also cover one boot-exit request, persistence before the
request, restart while confirmation is pending, and success-only guard clearing.
While pending, only the normal reader address is probed: even an empty probe of
the bootloader can repeat its previous command. These are synthetic tests, not
proof that a connected Unit supports or has executed the return command.
