# Soup Engine — original Canon EOS M

Experimental personal port for **Canon EOS M, firmware 2.0.2**, built against
official Magic Lantern `dev` commit
`142472e95054d73640f50789ac0fd66325499d82` (2026-10-04; latest checked 2026-10-27).
**Limited camera testing:** on 2026-10-27, a user confirmed that the module loads
and generates film JPEGs on an original EOS M after the I/O fix. Reported time
was about **45 seconds per photo**; the selected output size was not confirmed.
This is an experimental personal port, with no broad stability validation.

## What this port does

Loads as `soup.mo` through ML's module loader (API 8). It retains ML's menus and
uses ML's file, memory, task and menu APIs. It contains no 5D firmware addresses,
binary patches, display register writes, bootflag code, or installer FIR.

The original 13 `.fs` stocks and JPEG look engine are retained. Two signed shifts
in the JPEG encoder were changed to equivalent multiplication after UBSan caught
undefined behaviour on negative DCT coefficients. Shoot in
RAW or RAW+JPEG; the module develops the JPEG already embedded in the CR2, with
curves, hue controls, saturation, split tone, halation and grain. Select a colour
Picture Style (Standard, Neutral or Faithful); Monochrome removes the input's
colour. sRGB is recommended because this engine expects sRGB input.

When enabled, it scans **all CR2 files in DCIM camera folders, including existing
photos**. It saves `IMG_1234.FLM.JPG` beside `IMG_1234.CR2`. Original CR2 and Canon
JPEG files are opened read-only. Existing film JPEGs, even zero-byte ones, are
skipped. A `.FLM.TMP` file is written first and renamed after success. Failed or
interrupted jobs retry on a later scan or restart. Removing a film JPEG allows
that source to be developed again with the currently selected stock.

Half preview is the default; full preview is also available. Sizes come from
the actual embedded JPEG, not hard-coded 5D dimensions. Processing is entirely in
software and can be slow; see the limited camera report above. The worker yields
to Canon and waits during photo capture.
It cancels on disable, movie mode, ML shutdown or module unload. New captures are
picked up on subsequent scans; a stock is selected when processing begins.

This port does not provide live preview filters, MLV/video grading, the optional
sensor-RAW/spool development path, or a custom playback screen. The generated
JPEGs are intended for viewing off-camera; Canon playback recognition is untested.

## Build

Needs Python 3 with docutils (`rst2html` on PATH, used by ML's module metadata
builder), make, a complete GNU ARM `arm-none-eabi` toolchain with standard
C headers, and GNU ARM binutils with `ar --output` support.

From this repository:

```sh
PATH="$PWD/vendor/build-venv/bin:$PATH" python3 src/tools/build-eosm.py --ml ../magiclantern \
  --arm-bin /usr/local/playdate/gcc-arm-none-eabi-9-2019-q4-major/bin \
  --ar /opt/homebrew/bin/arm-none-eabi-ar
```

These paths match the machine used for this port; docutils is in the local
`vendor/build-venv` environment. Elsewhere, install docutils in your build
environment and point `--arm-bin`
at your toolchain. Omitting `--ml` clones the official repository into `vendor/`;
the script requires the pinned revision and an unmodified core checkout. On a
newer upstream HEAD, use a separate checkout at the pin. The script copies this
adapter into `modules/soup`, builds it with warnings as errors, builds the EOS M
core and its camera-selected standard modules (including RAW video), checks
every Soup module import, and stages `dist-eosm/card` plus
`dist-eosm/soup-eosm-202-experimental.zip`. It never writes to a camera/card.

ML's existing core emits warnings with GCC 9, so the core build uses its
documented `FATAL_WARNINGS=n` option. The EOS M adapter itself uses strict warnings.
`BUILD.json` records source pins, toolchain, hardware-test status and file hashes.
USB debugging is disabled in this build.

## Desktop validation

With a read-only original EOS M CR2 sample:

```sh
python3 src/ml/eosm/tests/run.py /path/to/sample.CR2
```

This checks all 13 stocks at half and full preview against the upstream desktop
pipeline, under AddressSanitizer and UndefinedBehaviorSanitizer. It also exercises
short reads, injected I/O failures, cancellation, output collisions, DCIM folder
changes and preservation of the source. Use `--quick` for one half-size stock.

## Install on an already working ML card

1. Confirm **original EOS M**, Canon **2.0.2**, and a working official ML setup.
   For first-time ML installation, follow https://builds.magiclantern.fm/EOSM.html.
2. Back up the card, including its existing `autoexec.bin` and matching
   `ML/modules/EOSM_202.sym`. Use a spare card with backed-up test photographs
   for the first hardware run.
3. Copy the contents of `dist-eosm/card` to that card. Use the supplied
   `autoexec.bin` **together with** its matching `EOSM_202.sym`. This is a local
   build of the pinned official source, not an official prebuilt release.
   The package also replaces the standard EOS M modules with builds from that
   source, so they match its module API. Disable any third-party modules until
   they have been rebuilt for this core.
4. Boot ML, enable `soup.mo` in Modules and restart. In **Shoot → Soup Engine**,
   choose a film stock and output size, then enable Soup Engine. It starts off.
5. For initial testing, set Canon Auto power off to Off. Start with one CR2,
   watch the Status row, and wait for it to return to Watching DCIM. Inspect the
   JPEG and compare the source CR2 checksum on a computer before testing bursts.
   Orderly shutdown, live-view responsiveness and capture coexistence still need
   physical-camera validation.

Stock files live in `ML/FILM`, listed in `INDEX.TXT`; up to 24 stocks can load.
Restart after editing them. Preferences are saved through ML's normal config.
If a job fails, the Status row reports the engine error; turn the feature off to
stop retries. A leftover `.FLM.TMP` is scratch and is removed before retrying.
The I/O diagnostic build uses ML's `FIO_CreateFile` for its scratch output and
opens CR2 input with `O_SYNC`, as ML's own file-copy code does. Status identifies
open, seek, read, create, write, temporary-file removal and JPEG publication
failures separately. A user confirmed that this fix resolved the first
physical-camera I/O failure. Scratch-file
creation checks for an existing path first; only Soup's single worker should
write its `.FLM.TMP` paths.

Disable `soup.mo` and restart to remove the feature. Restore your backed-up
`autoexec.bin` and matching symbol file to return to your previous ML build.
The upstream repository's `card/AUTOEXEC.BIN` and `card/SE.BIN` are **5D-only**;
never copy those files onto the EOS M card. Use `dist-eosm/card` only.

## Sources

- Soup Engine: https://github.com/jiijii0/soup-engine, commit
  `c6bede518eccd0b792f83718337ef3add2cf8ee5`.
- Official ML source: https://github.com/reticulatedpines/magiclantern_simplified.
- Test CR2: https://raw.pixls.us/getfile.php/1224/nice/Canon%20-%20EOS%20M%20-%20RAW%20(3%3A2).CR2,
  CC0, SHA-256 `27d151efe86f04bea8fcf6ec9f3a722ae3ee24a3f56a6ebbd27ee89b215cf569`.

GPL-2.0; see LICENSE. This personal adapter and build/test tooling were generated
with OpenAI Codex. They have not been submitted to Magic Lantern.
