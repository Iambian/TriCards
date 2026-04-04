# TriCards repository instructions

## Build commands

### Current card-pack builder (`Tri2Pak!`, used by the client)

Run from the repository root:

```bat
python BUILDER\tools\tkit2.py --compression zx0 --var-name CRP7FF8
```

- Default source pack: `BUILDER\src\ff8packorig`
- Output appvar: `BUILDER\bin\*.8xv`
- Preview/intermediate images: `BUILDER\obj\tkit2`
- The default FF8 pack currently needs `--compression zx0`; the script default is `zx7`, but that build exceeds the format size limit for the checked-in FF8 source pack.

To inspect one card instead of building a full pack:

```bat
python BUILDER\tools\tkit2.py 001.png --compression zx0
```

If the image is not listed in the pack JSON, also pass `--card-type`.

### Legacy card-pack builder (`TriCrPak`, not the current client path)

`BUILDER\build.bat` uses relative paths and must be run from inside `BUILDER\`:

```bat
cd BUILDER
build.bat src\pack1 PACK1
```

This writes the output pack to `BUILDER\bin`. Use this path only when you specifically need the legacy `TriCrPak` format; the active client targets `Tri2Pak!` packs from `tkit2.py`.

### Client build

Run from `CLIENT\`:

```bat
make gfx
make
```

- Run `make gfx` before `make` whenever `CLIENT\src\gfx\convimg.yaml` or the source PNGs change.
- Final program output: `CLIENT\bin\TRICARDS.8xp`
- The client build depends on the CE C SDK / `cedev-config` toolchain described in `readme.md`.

## Test and lint commands

No repository-defined automated test or lint targets were found in the checked-in build files. There is no single-test command documented in this repository.

## High-level architecture

This repository has two main parts that meet at the pack format boundary:

1. `BUILDER\` creates TI appvars containing card packs.
2. `CLIENT\` is the TI-84+ CE game/browser that discovers those appvars and renders cards from them.

The active path is `BUILDER\tools\tkit2.py` -> `Tri2Pak!` payload in a `.8xv` appvar -> `CLIENT\src\card_loading.c` and the runtime in `CLIENT\src\main.c` / `CLIENT\src\gameplay.c`.

On the client side:

- `CLIENT\src\main.c` owns the main loop, mode switching, pack selection UI, and card browser flow.
- `CLIENT\src\card_loading.c` owns pack I/O, record parsing, decompression, and palette remapping for loaded cards.
- `CLIENT\src\gameplay.c` owns board drawing, hand/grid movement, capture resolution, and the card color-transition animation.
- `CLIENT\src\tricards.h` is the shared contract for constants, packed file structs, slot counts, palette macros, and public function declarations.

Graphics in `CLIENT\src\gfx\` are partly generated. `convimg.yaml` defines the internal palette and generated sprite sources, and `make gfx` regenerates the corresponding `.c` / `.h` files. Treat those generated files as build artifacts derived from the PNGs and `convimg.yaml`.

On the builder side, `tkit2.py` reads `data.json` plus source images, detects whether each card still has a removable frame, masks/crops only when a frame is detected, resizes to `52x52`, quantizes each card to a compact per-card palette, compresses the indexed pixels, then serializes a versioned `Tri2Pak!` payload. `BUILDER\build.bat` still points at the older `tkit.py` path and produces the legacy `TriCrPak` format.

## Key repository-specific conventions

- The current client only targets `Tri2Pak!` packs. Do not assume the legacy `TriCrPak` builder path is interchangeable with the active runtime.
- Pack discovery is driven by the payload magic string, not just the file extension: the client scans installed appvars whose payload starts with `Tri2Pak!`.
- `tricard_pack_header_t` is fixed at 32 bytes and `tricard_card_metadata_t` is fixed at 16 bytes. Keep those layouts packed and stable.
- Record stride is derived at runtime from `string_table_offset - record_table_offset`; the loader does not trust a hardcoded record width.
- Card palettes are slot-local. Internal UI colors live at low palette indices, and runtime card palettes start at `CARD_PALETTE_BASE_INDEX` (`100`) with one transparent/background slot plus the per-card opaque entries.
- Each runtime card slot owns its own `palette_base_index`. The pack selector/browser set that base color to the file-explorer background, while gameplay uses player background colors so captured-card transitions can animate by rewriting that slot-local base entry.
- There are dedicated non-gameplay card slots for previews: gameplay uses slots `0-9`, pack selection uses additional preview slots, and the card browser uses its own preview slot. Reusing gameplay slots for previews can reintroduce palette instability.
- Generated internal graphics use the shared `internal_palette`; loaded pack cards use per-card palettes embedded in the pack records. Keep those two palette systems separate.
- `tkit2.py` expects pack JSON entries in the 9-field form `[rank, name, type, up, right, down, left, element, image.png]` and validates ranks/stats against the inclusive `1..10` range.
- `tkit2.py` supports both framed and frameless source art. If a card image does not match the configured frame template closely enough, the builder skips frame masking and crop-box trimming and resizes the original source image directly.
