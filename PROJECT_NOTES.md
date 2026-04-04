# TriCards Project Notes

## Overview

This repository contains two mostly separate pieces:

1. `BUILDER/` builds TI-84+ CE appvars containing card packs.
2. `CLIENT/` is the calculator game/browser that loads those appvars and renders the cards.

There is also a second Python tool, `BUILDER/tools/toolkit.py`, which is not part of the card-game build path. It is a general-purpose video packager for the same platform and appears to be an older side tool that shares some utility code.

## Current Status Note

The client-side custom palette migration is now complete, and some of the older
notes below are historical context rather than a perfect description of the
live code.

Completed so far:

1. The client was split into `main.c`, `card_loading.c`, `gameplay.c`, and `tricards.h`.
2. Internal client graphics were moved to a generated convimg palette flow driven by `CLIENT/src/gfx/convimg.yaml`.
3. Internal built-in art now uses a shared generated palette loaded at runtime.
4. Loaded card art now uses per-slot palette slices starting at palette index `64`.
5. The pack loader now derives runtime record width from the pack table offsets instead of assuming it from the header palette count alone.
6. The card browser no longer reloads a palette-bearing slot just to print list names, and its selected-card preview now uses a dedicated runtime slot.
7. Each runtime card slot now tracks its own base palette entry so the serialized `255` pixels can be remapped to a slot-local background color instead of one fixed shared color.
8. The pack selector and browser preview set that slot-local base entry to the file-explorer background color, while gameplay sets it to `PLAYER1_BG` or `PLAYER2_BG` so each player's cards can keep their own background tint.

Still in progress:

1. Builder/runtime format alignment is still in transition, although the browser now tolerates record-width discrepancies more defensively.
2. Some sections below still describe the earlier xlibc-only behavior for historical/reference purposes.

## Repository Map

### Root

- `readme.md`: basic usage notes and build commands.
- `PROJECT_NOTES.md`: this file.

### Builder

- `BUILDER/build.bat`: thin wrapper around `python tools\tkit.py %1 %2`.
- `BUILDER/tools/tkit.py`: the actual card-pack compiler.
- `BUILDER/tools/toolkit.py`: unrelated TI-84+ CE video converter/packager.
- `BUILDER/tools/zx7.exe`: compressor used by both Python tools.
- `BUILDER/src/ff8pack/data.json`: full Final Fantasy VIII sample pack definition.
- `BUILDER/src/pack1/data.json`: minimal test/example pack definition.

### Client

- `CLIENT/makefile`: CE C SDK project configuration.
- `CLIENT/src/main.c`: app loop, menu flow, browser flow, and runtime startup wiring.
- `CLIENT/src/card_loading.c`: pack parsing, card decompression, and per-card palette remapping.
- `CLIENT/src/gameplay.c`: board rendering, selection, and battle/game setup helpers.
- `CLIENT/src/tricards.h`: shared constants, types, and declarations used by the client sources.
- `CLIENT/src/gfx/convimg.yaml`: static sprite conversion and palette-generation rules for convimg.
- `CLIENT/src/gfx/*.png`: UI/element/number/card-back art sources.

## Build Flow

### Card pack build

Input:

- a folder of card PNGs
- exactly one JSON file describing the pack and the cards

Command:

```bat
BUILDER\build.bat BUILDER\src\ff8pack FF8PACK
```

Pipeline:

1. `tkit.py` reads the JSON.
2. Each card image is quantized and remapped into the xlibc-compatible palette space.
3. Each remapped image is ZX7-compressed.
4. The builder emits a custom payload beginning with `TriCrPak`.
5. That payload is wrapped into a standard TI `.8xv` appvar.

Output:

- `BUILDER/bin/<PACKNAME>.8xv`

### Version 2 card pack build

The current client targets the newer `Tri2Pak!` payload produced by
`BUILDER/tools/tkit2.py`.

Typical FF8 build command:

```bat
python BUILDER\tools\tkit2.py --compression zx0 --var-name CRP7FF8
```

Notes:

1. The script defaults to `BUILDER/src/ff8packorig` as its source directory.
2. The default preview output directory is `BUILDER/obj/tkit2`.
3. The default compression setting is `zx7`, but the default FF8 source pack in
   this repository currently exceeds the format size limit unless built with
   `--compression zx0`.
4. `tkit2.py` can now build framed and frameless source art in the same overall
   workflow. If a card image does not appear to match the expected frame
   template, the script skips frame removal and crop-box trimming and resizes the
   source art directly to `52x52`.

### Client build

Input:

- CE C SDK project in `CLIENT/`
- convimg-generated graphics from `CLIENT/src/gfx`

Pipeline:

1. `make gfx` runs convimg using `CLIENT/src/gfx/convimg.yaml`.
2. `make` builds `TRICARDS.8xp`.
3. At runtime, the program loads the internal generated palette, then scans installed appvars for packs whose payload starts with `Tri2Pak!`.

## Source File Formats

### Card pack JSON format

Observed structure from `BUILDER/src/ff8pack/data.json` and `BUILDER/src/pack1/data.json`:

```json
[
  "Pack description",
  "PACKID",
  [rank, "name", "type", up, right, down, left, "element", "image.png"],
  [rank, "name", "type", up, right, down, left, "element", "image.png"]
]
```

Field meanings:

- item `0`: human-readable pack description
- item `1`: pack identifier stored in the binary header
- item `2+`: one card entry per array

Card entry layout:

1. `rank`
2. `name`
3. `type`
4. `up`
5. `right`
6. `down`
7. `left`
8. `element`
9. `image filename`

Supported enums in `tkit.py`:

- types: `monster`, `boss`, `gf`, `player`
- elements: `none`, `poison`, `fire`, `wind`, `earth`, `water`, `ice`, `thunder`, `holy`

Notable detail:

- The comment at the top of `tkit.py` is stale. The live parser expects 9 fields per card, including `type`.

## Generated Card Pack Format

### Logical payload layout

The appvar payload generated by `tkit.py` is:

```text
TriCrPak                              8 bytes
pack identifier                       9 bytes, null-padded/truncated
description                           null-terminated string
card count                            uint16 little-endian
card records                          11 bytes each
card name string table                concatenated null-terminated strings
compressed image data                 concatenated ZX7 streams
```

### Card record layout

Each card record is 11 bytes:

```text
+0   uint8   rank
+1   uint16  absolute offset to card name string
+3   uint8   type
+4   uint8   up
+5   uint8   right
+6   uint8   down
+7   uint8   left
+8   uint8   element enum
+9   uint16  absolute offset to compressed image stream
```

Important properties:

- Name and image offsets are absolute offsets from the start of the payload, not relative to the record.
- Names are stored once in a string blob after the record array.
- Images are stored as back-to-back ZX7-compressed streams with no per-image header in the pack.
- At runtime the client already knows every card image is `52x52`, so it reconstructs the sprite header after decompression.

### Historical runtime interpretation note

Older revisions centered pack loading around `getcarddata()` in `main.c`.

That is no longer the live organization. Current pack loading is split out into `card_loading.c`, but the old note is still useful as background for understanding how the project reached the current format/palette transition.

## TI `.8xv` Wrapper Format

Both Python tools share `export8xv()`, which wraps arbitrary payloads into a TI appvar file.

The wrapper code builds:

1. a TI8X file header beginning with `**TI83F*`
2. a variable entry marked as appvar type `0x15`
3. a variable name padded/truncated to 8 characters
4. an archived flag
5. a checksum over the variable entry

This is standard calculator container work around the project-specific payload.

## Static Graphics Format

`CLIENT/src/gfx/convimg.yaml` now defines one shared internal palette plus three convert groups:

- `internal_palette`: generated palette for built-in client art
- `element_gfx`: the 8x8 element symbols plus blank symbol
- `num_gfx`: the number tilemap used for stat digits
- `misc_gfx`: currently the card back

Shared settings:

- compression: `zx7`
- palette: `internal_palette`
- palette offset: `0`
- transparent index: `0`

Implications:

- convimg emits the generated palette array in addition to the compressed sprite data.
- Built-in client art no longer depends on the old implicit full-xlibc palette model.
- Runtime code copies the generated internal palette into hardware palette memory before drawing.
- Runtime code still decompresses these assets and draws them with `gfx_TransparentSprite_NoClip()`.

## Palette Handling

This is now the live client model rather than an in-progress migration target.

### Current runtime palette model

The client no longer uses one single implicit xlibc palette model for everything.

Relevant points:

- Built-in client art now uses a generated shared palette.
- That palette is generated by convimg and currently occupies reserved low palette entries.
- The generated palette currently exposes `sizeof_internal_palette == 128`, which is 64 palette entries.
- Runtime code copies that palette into `gfx_palette` at startup.
- Internal built-in art uses transparent index `0`.
- Card images use separate per-slot palette slices starting at palette index `64`.

So the active design is a split model:

1. reserved low palette entries for internal client assets
2. per-slot palette windows for dynamically loaded card art

This is intentional and now represents the finalized client-side custom palette
direction.

### Historical note about the old xlibc-only path

Earlier versions of the project forced both static art and pack art into one xlibc-compatible palette space. The notes below remain useful background for understanding the original design and builder assumptions.

### Older card image quantization path in `tkit.py`

The legacy builder path in `tkit.py` forced card pack images into the xlibc index space.

The process in `_imagedata.__init__()` is:

1. Open the PNG as RGB.
2. Quantize it adaptively to 6 colors.
3. Read the resulting palette.
4. Snap each RGB channel down to multiples of 8 with `& ~0x7`.
5. Find the palette entry closest to white and replace it with pure white.
6. Deduplicate the small palette.
7. Pad the palette to 256 entries with white.
8. Re-quantize the source image against that temporary palette.
9. Replace the temporary palette with the full xlibc palette.
10. Re-quantize again so the pixel indices land in xlibc-compatible entries.
11. Save the 1-byte-per-pixel indexed image and ZX7-compress it.

That is how dynamic card art and static art originally ended up sharing one palette model.

### xlibc palette generation

`tkit.py` synthesizes the xlibc palette in code by iterating 256 indices and decoding them into RGB values using a 5-6-5-style layout:

```text
rrrrrggggggbbbbb
```

The builder then stores those colors as `(r|7, g|7, b|7)` before flattening them for Pillow palette use.

Important nuance:

- earlier in the pipeline the temporary palette channels are masked with `& ~0x7`
- later the xlibc palette table is generated with `| 7` on each component

This is not a contradiction in practice; it is a way of snapping colors onto the representable buckets and then materializing the high end of each bucket for the palette image used during quantization.

### Current card image palette handling

The current runtime card loader uses the newer versioned pack structure and remaps pixels into slot-local hardware palette windows.

Current behavior in `card_loading.c`:

1. Read the shared transparent RGB1555 color from the pack header.
2. Read the per-card compact palette from the card record.
3. Allocate the target slot's hardware palette slice starting at `CARD_PALETTE_BASE_INDEX + slot * (palette_entries + 1)`, with `CARD_PALETTE_BASE_INDEX` currently set to `64`.
4. Store the slot-local base color entry first.
5. Copy the per-card RGB1555 palette immediately after it.
6. Decompress the card image bytes.
7. Rewrite decompressed bytes so:
   - serialized `255` becomes the slot-local base entry
   - compact opaque indices become the slot's hardware palette entries
8. Allow callers to overwrite that base entry after load so the same card image
   can inherit the surrounding UI or ownership color without touching the rest
   of the per-card palette.

This allows each card slot to have an independent palette window without sharing built-in client art entries.

### Transparency behavior during transition

There are now two relevant transparency-related byte values:

1. Built-in client art uses GraphX transparent index `0`.
2. Serialized card-image streams still use `255` as the on-disk transparent sentinel before remapping.

The card loader rewrites that serialized sentinel into the slot-local base
palette entry during load, and runtime code can then retint that base entry per
card slot.

### Practical effect of the palette design

- Built-in client art now has a shared generated palette rather than relying on full xlibc directly.
- Card images can now own slot-local palette windows.
- Card faux-transparency can now be retinted independently per runtime card slot.
- Gameplay capture effects can now animate that slot-local base color from the
  previous owner color toward white and then down to the new owner color without
  blocking the main gameplay loop.
- The old "everything shares one global palette" assumption no longer describes the live client.
- The client-side custom palette transition should now be considered complete.

## Runtime Data Loading Notes

### Pack discovery

The client scans installed variables using `ti_Detect(&sp, card_pack_header)` where `card_pack_header` is currently `Tri2Pak!`.

### Pack preview and browsing

- `selectpack()` opens each matching appvar, shows the description and a 5-card preview.
- `card_loading.c` now contains the central pack parsing and load helpers used by both the browser and the game.
- The browser list now reads card names directly from metadata instead of repeatedly reloading slot `0`, which avoids palette-slice churn on the already-drawn preview.
- The selected browser preview uses its own runtime slot so it does not inherit transient palette updates from the pack selection preview.
- Selector preview rendering now reuses gameplay card slots, while the browser
  keeps one dedicated preview slot
  instead of borrowing the first gameplay slots.
- Preview cards in the selector and browser now retint their slot-local base
  palette entry to `FILE_EXPLORER_BGCOLOR`, so their faux-transparent pixels
  match the surrounding panel background.

### Gameplay card ownership transitions

- `cardfight()` still resolves ownership immediately for gameplay logic, but it
  now also starts a per-card background-color transition for any captured card.
- Each runtime card slot can carry its own transition state, so multiple capture
  animations can run concurrently.
- `redrawboard()` advances those transitions during the normal gameplay redraw
  loop, so input and turn flow do not have to block while the color effect plays.
- The white-flash portion of that effect is now driven with GraphX's built-in
  `gfx_Lighten()` helper on 1555 colors rather than a custom blend routine.
- `drawcard()` now uses the slot's current animated RGB1555 color for both the
  card background fill and the card's faux-transparent palette entry.

### Gameplay rule resolution

- The live startup path in `main.c` still defaults matches to
  `RULE_OPEN | RULE_RANDOM | RULE_ELEMENTAL | RULE_SUDDENDEATH`.
- `gameplay.c` now contains the rule-resolution helpers for the remaining
  feasible FF8 board rules:
  - `Same`
  - `Same Wall`
  - `Plus`
  - `Combo`
- The placed-card resolution path now runs through `resolvecardplacement()`
  instead of keeping elemental adjustment and four direct `cardfight()` calls
  inline in `main.c`.
- Elemental placement modifiers are now clamped back into the legal `1..10`
  range.
- `Same` and `Plus` are evaluated from the newly placed card only.
- `Same Wall` only extends `Same`, treating a board edge as value `10`.
- `Combo` only propagates normal greater-than captures from cards flipped by
  `Same` or `Plus`.
- `main.c` now inserts a dedicated rules-selection screen between pack selection
  and `initGame()`.
- That screen uses a centered list styled after the Card Browser rows, with a
  top `Start Game` row and one row per rule.
- Enabled rules keep a distinct non-selected text color, `Random` stays locked
  on, and pressing `Mode` backs out to card pack selection instead of silently
  accepting the current flags.

### In-memory card images

- Card images are decompressed on demand into the persistent `card_image_pool`.
- That image pool is now statically allocated rather than coming from `malloc`.
- Each runtime slot owns its sprite buffer and its hardware palette slice.
- Each runtime slot also owns the first palette entry in that slice as a
  writable background/faux-transparent color.
- Browser stability depends on keeping text-only metadata reads separate from palette-bearing card loads, because palette slices are global hardware state even when sprite buffers are slot-local.
- Gameplay uses that writable base entry to tint player 1 cards with
  `PLAYER1_BG` and player 2 cards with `PLAYER2_BG`.

## Areas Of Interest And Quirks

These are the parts most worth understanding before modifying anything.

### 1. Historical legacy-format quirk

This section describes a real issue in the older `TriCrPak`/`getcarddata()` path. It remains useful background, but it is not a description of the current split client code.

In the legacy path, `getcarddata()` read:

- `cptr[-2]` as the card count low byte
- `cptr[-1]` as a format byte

But `tkit.py` only writes a 16-bit card count before the record data. There is no separate format byte in the builder output.

Current behavior works only because the packs in this repo contain fewer than 256 cards, so the high byte of the card count is `0`, which the client interprets as format `0`.

Consequences:

- the client effectively cannot distinguish format versions yet
- card counts above 255 would collide with the supposed format byte
- the `fmt == 1` branch in `getcarddata()` is a stub and not currently reachable from builder output

This remains an important historical quirk because it helps explain why the repository moved toward a newer versioned pack structure.

### 2. The builder does less validation than the README suggests

The root README says:

- ranks must be 1 through 10
- images must be 52x52

But `tkit.py` does not actually enforce those rules.

Missing validation includes:

- image dimensions
- rank range
- stat range
- duplicate card names
- duplicate image names
- missing referenced image files
- unused extra PNG files in the source directory

As written, the builder will package every `.png` found in the input folder, even if the JSON never references it.

### 3. Historical `putcarddata()` issue

In the legacy monolithic client, `putcarddata()` called:

```c
memset(carddata,0,sizeof carddata);
```

`carddata` is a pointer, so this clears only pointer-sized bytes rather than the full `metacard_t` struct. The subsequent `memcpy()` fills the embedded `card_t`, but not the rest of the metadata. This is unrelated to palettes, but it is a real implementation hazard.

### 4. The codebase mixes polished ideas with unfinished scaffolding

Examples:

- `RULE_SAME`, `RULE_SAMEWALL`, `RULE_PLUS`, and `RULE_COMBO` now have gameplay
  resolution logic and a dedicated pre-match UI for toggling them.
- `fmt == 1` in `getcarddata()` is declared but not implemented.
- `stats` exists but only partly participates in pack selection flow.
- the root README explicitly says the proper writeup and licensing were never finished.

### 5. `toolkit.py` defines a second custom file family

This tool is not part of TriCards gameplay, but it does define its own payload headers:

- `8CEVDat` for segment files
- `8CEVDaH` for a metadata/header file

It also has its own adaptive palette mode (`encoder 6`) with per-frame palette data embedded in the frame stream. That is separate from the card-pack pipeline, but it shows the author reused the same TI appvar export pattern for multiple experiments.

## Experimental `tkit2.py` Payload Format

`BUILDER/tools/tkit2.py` now targets a separate pack payload that is intentionally
incompatible with the legacy `TriCrPak` reader.

Version 2 payload layout:

```text
Tri2Pak!                              8 bytes
version                               uint8
card count                            uint16 little-endian
palette entry count                   uint8
transparent color                     uint16 RGB1555
pack identifier                       9 bytes, null-padded/truncated
compression method                    uint8 (`0=zx7`, `1=zx0`)
description offset                    uint16 absolute payload offset
record table offset                   uint16 absolute payload offset
string table offset                   uint16 absolute payload offset
image blob offset                     uint16 absolute payload offset
card records                          card_count * (16 + 2 * palette_entry_count) bytes
string table                          null-terminated description, names, image filenames
compressed image blob                 concatenated image streams using the selected codec
```

Each card record stores:

- rank, type, up/right/down/left, element
- absolute offset to the card name string
- absolute offset to the source image filename string
- absolute offset to the card image stream
- compressed image byte length
- `palette_entry_count` RGB1555 palette entries for that card

Image streams are built from 52x52 byte-per-pixel indices before compression.
Opaque pixels are serialized using compact palette-local indices
`0..palette_entry_count-1`, and `255` is reserved as a transparent pixel
sentinel. The actual transparent RGB1555 color is stored once in the file
header and is not part of the per-card palettes.

Current preprocessing behavior before quantization:

1. If the source card appears to contain the expected type-specific frame, the
   script masks that frame away, crops using the configured box for that card
   type, and resizes the result to `52x52`.
2. If the source card does not appear to contain that frame, the script skips
   frame masking and crop-box trimming and resizes the original source image
   directly to `52x52`.

This keeps the FF8 framed source path stable while allowing custom packs to use
frameless source images or source dimensions other than `256x256`.

Intended future client flow for this format:

1. Read the pack header and note `palette_entry_count` plus the shared
   transparent color.
2. Read one card record, copy its RGB1555 palette into whatever hardware palette
   range the client decides to use, and remember the mapping from local indices
   `0..palette_entry_count-1` to those hardware indices.
3. Decompress the card image into a temporary 52x52 byte buffer using the
   codec indicated by the header compression-method byte.
4. Rewrite each opaque pixel byte from its local palette index into the chosen
   hardware palette index.
5. Preserve `255` as the transparent sentinel, or rewrite it to the runtime's
   active transparent index if the client standardizes on a different value.

## Suggested Mental Model For Future Work

If you come back to this project later, the safest high-level model is:

1. Treat xlibc palette compatibility as a global invariant.
2. Treat `TriCrPak` as the real pack payload format inside a standard TI appvar wrapper.
3. Treat `getcarddata()` and `_imagedata` as the two critical format/palette functions.
4. Treat any change to transparency or card image fidelity as a builder-plus-client change, not a local tweak.
5. Treat the current pack header as effectively version `0`, even though it is not explicitly serialized that way.

## Most Important Files To Re-open First Next Time

- `BUILDER/tools/tkit.py`
- `CLIENT/src/main.c`
- `CLIENT/src/gfx/convpng.ini`
- `BUILDER/src/ff8pack/data.json`
- `readme.md`
