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

- The current client targets both single-file `Tri2Pak!` packs and the new multi-file manifest format `Tri2Mft!` (with shard appvars using the `Tri2Shd!` magic). Do not assume the legacy `TriCrPak` builder path is interchangeable with the active runtime.
- Pack discovery is driven by the payload magic string, not just file extension: the client scans installed appvars whose payload starts with `Tri2Pak!` or `Tri2Mft!`. Manifest appvars are treated as a logical pack that references one or more `Tri2Shd!` shard appvars.
- `tricard_pack_header_t` is fixed at 32 bytes and `tricard_card_metadata_t` is fixed at 16 bytes. Keep those layouts packed and stable.
- Record stride is derived at runtime from `string_table_offset - record_table_offset`; the loader does not trust a hardcoded record width.
- Card palettes are slot-local. Internal UI colors live at low palette indices, and runtime card palettes start at `CARD_PALETTE_BASE_INDEX` (64) with one transparent/background slot plus the per-card opaque entries.
- Each runtime card slot owns its own `palette_base_index`. The pack selector/browser sets that base color to the file-explorer background, while gameplay uses player background colors so captured-card transitions can animate by rewriting that slot-local base entry.
- Gameplay card slot count was reduced so gameplay and preview slots overlap; the runtime now uses 11 gameplay slots and reuses a preview slot to reduce memory pressure. See `CLIENT/src/tricards.h` for constants (GAME_CARD_SLOT_COUNT, CARD_BROWSER_PREVIEW_SLOT).
- Generated internal graphics use the shared `internal_palette`; loaded pack cards use per-card palettes embedded in the pack records. Keep those two palette systems separate.
- `tkit2.py` expects pack JSON entries in the 9-field form `[rank, name, type, up, right, down, left, element, image.png]` and validates ranks/stats against the inclusive `1..10` range.
- `tkit2.py` supports both framed and frameless source art. If a card image does not match the configured frame template closely enough, the builder skips frame masking and crop-box trimming and resizes the original source image directly.
- Builder CLI additions and behavior:
  - New option: `--format` (auto|single|multi). `auto` (default) attempts a single-file pack and falls back to manifest+shards when it doesn't fit. `single` forces single-file output, `multi` forces manifest+shards.
  - New option: `--palette-colors N` controls the per-card quantized color count (default unchanged).
  - Manifest and shard magics: `Tri2Mft!` (manifest) and `Tri2Shd!` (shard). Shards contain local pack payloads (32-byte header + local record/string/image tables) so the client can reuse local-pack parsing logic.
  - Sharding is deterministic: shard names are derived from the manifest var-name. Partitioning maximizes cards per shard under the platform's ~64KB payload limit using a binary-search heuristic.
  - When sharding occurs but a single-file build would have fit with only a small delta, the builder emits a "near-fit" warning (threshold: 1024 bytes by default) to help tune compression or palette size.
- Client runtime notes:
  - The client implements a logical-pack layer that treats manifest appvars as logical packs and resolves global card indices to the correct shard payload. Key helpers: resolvepackpayload(), openmanifestshard(), loadcardslot().
  - The client decompresses card images into per-slot in-memory buffers and closes shards when possible; it no longer keeps persistent pointers into opened appvar payloads.
  - Missing or incompatible shards currently cause load failures at runtime; consider adding manifest validation at selection time to improve UX.

# Additional instructions

The client software is built in C and runs on the TI-84+ CE platform. The
overall documentation for the toolchain used in this is available at
https://ce-programming.github.io/toolchain/index.html. At that site is additional
information regarding the following topics:
- graphx library: https://ce-programming.github.io/toolchain/libraries/graphx.html
- Nonstandard file I/O: https://ce-programming.github.io/toolchain/libraries/fileioc.html
- Keyboard input: https://ce-programming.github.io/toolchain/libraries/keypadc.html
- Decompression routines: https://ce-programming.github.io/toolchain/headers/compression.html
- Linking and using ASM routines: https://ce-programming.github.io/toolchain/static/asm.html

The TI-84+ CE memory layout is documented at https://ce-programming.github.io/toolchain/static/faq.html, but here's the key takeaways:
- The .text (code), .data, and .rodata sections are loaded into user RAM at runtime. The program will not run if the user does not have enough free RAM.
Runtime libraries are also loaded at runtime and are loaded into this area.
- The stack is also located in user RAM and is fixed to 4KB. The stack uses
a hardware guard to trigger a crash if it overflows.
- The heap and the .bss share a separate area of RAM that is typically used for
buffering the graph and homescreen, so it can be freely used and discarded as
needed. Thus, freeing heap allocations is unnecessary at the end of the program.
A call to a dedicated full screen buffer clear is sufficient. Nobody does this.
This area is 60989 bytes large.
- The graphx library should be used for all graphics operations. The memory
layout of the screen is documented there.

The documentation for modifying client/src/gfx/convimg.yaml and regenerating the graphics code is available at https://github.com/mateoconlechuga/convimg. The convimg tool is used to generate optimized sprite sheets and palette data from source PNGs, and the generated .c/.h files are checked in as part of the repository. To modify the internal graphics, edit the source PNGs and/or `convimg.yaml`, then run `make gfx` from the `CLIENT\` directory to regenerate the graphics code.