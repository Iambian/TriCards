TriCards - A Triple Triad clone
===============================
Warning:
* **This only works on the TI-84+ CE.**
* This will not work on the TI-84+ CSE.
* This will not work on any TI-84+ (SE)

Motivation
----------
Geekboy1011 asked for this fun card game on his calculator and I wanted to
work on something fun.

Building Asset Files
--------------------
* Syntax: `BUILDER\build.bat <IN_FILE_PATH> <OUT_FILE_BASE_NAME>`
* Example: `BUILDER\build.bat BUILDER\src\ff8pack FF8PACK`
Input folder must contain all image files and a JSON file formatted like the
examples provided.

Version 2 pack builder (`BUILDER\tools\tkit2.py`)
-------------------------------------------------
* `tkit2.py` builds the newer `Tri2Pak!` pack format used by the current client,
  and can also emit the manifest+shards format that the current client now
  recognizes for oversized packs.
* Default source input is `BUILDER\src\ff8packorig` and default previews go to
  `BUILDER\obj\tkit2`.
* Basic usage: `python BUILDER\tools\tkit2.py --compression zx0 --var-name CRP7FF8`
* The builder defaults to `--format auto`: it tries single-file output first,
  then falls back to a manifest plus shards if the payload does not fit in one
  appvar.
* The script still defaults to `zx7`, but the default FF8 source pack currently
  needs `--compression zx0 --format single` if you specifically want the
  checked-in client's current one-file path.
* `tkit2.py` now detects whether a removable frame is actually present. If a
  card does not appear to match the expected frame, the script skips frame
  masking and crop-box trimming and simply resizes the source art to `52x52`.
  That lets custom packs use framed or frameless card art, including non-256x256
  source images.
* Single-image inspection mode is available by passing an image filename instead
  of building a full pack. Use `--card-type` if the image is not listed in the
  pack JSON.
* The current client now supports both single-file `Tri2Pak!` packs and the new
  manifest+shards format (`Tri2Mft!` + `Tri2Shd!`).
* See `BUILDER\readme.md` for the full `tkit2.py` workflow and configuration
  details.

Building Pack Viewer / Game Player
----------------------------------
* Download and install CE C toolchain v12.1 from
  https://github.com/CE-Programming/toolchain/releases
* The client static graphics are now generated from `CLIENT\src\gfx\convimg.yaml`.
  From `CLIENT\`, run `make gfx` before `make` whenever the graphics config or
  source PNGs change.
* `make gfx` also builds the optional fullscreen board-mat halves
  `TRICARDL.8xv` and `TRICARDR.8xv` into `CLIENT\src\gfx\out`. These come from
  `board-mat-L.png` and `board-mat-R.png`, are quantized against the shared
  `internal_palette`, and use raw sprite payloads with width/height included so
  gameplay can draw them directly from the installed appvars. Gameplay uses them
  only when both appvars are present; otherwise it falls back to the solid board
  background color.
* Open the command prompt and navigate to where the makefile is. Type `make`,
  push enter, and watch it go.
* If it all worked, TRICARDS.8xp will be in the CLIENT\bin folder.

Current Graphics / Runtime Status
---------------------------------
The custom palette migration for the client is now in place.

What has been changed so far:
* `CLIENT\src\main.c` has been split so the client is now organized around
  `main.c`, `card_loading.c`, `gameplay.c`, and the shared declarations in
  `tricards.h`.
* Internal client assets are no longer using the old implicit xlibc-only setup.
  `make gfx` now generates a shared internal palette from `convimg.yaml`, and
  the client loads that palette at runtime.
* Card images now use per-slot palette slices starting at palette index `64`.
* The pack loader now derives card-record width from the pack table offsets at
  runtime, which fixed browser mismatches caused by assuming record stride from
  palette-count metadata alone.
* The card browser now keeps list-name reads separate from preview-image loads,
  preventing palette instability caused by reusing the same slot for both text
  lookup and the selected-card preview.
* Each card slot now owns a writable base palette entry used as the card's
  faux-transparent background color. The pack selector and browser set that
  entry to the file-explorer background color, while gameplay sets it to the
  existing player background colors (`PLAYER1_BG` / `PLAYER2_BG`).
* During gameplay, captured cards now animate that writable base color in a
  non-blocking sequence from the card's previous owner color toward white and
  then back down to the new owner's background color. Multiple captured cards
  can transition at the same time.
* The gameplay rule engine now supports FF8-style `Same`, `Same Wall`, `Plus`,
  and `Combo` resolution when those rule bits are enabled.
* Starting a match now opens a centered pre-match rule-selection screen before
  gameplay begins. It defaults to `Open + Random + Elemental + Sudden Death`,
  keeps `Random` locked on, and lets `[Mode]` back out to card pack selection.
* Card preview storage now reuses gameplay slots for pack selection and keeps one
  extra browser preview slot; the card image pool is statically allocated
  instead of coming from the heap.

The custom palette migration itself should now be treated as complete. The
remaining active work is around broader pack-format/runtime evolution rather
than the client still depending on the old shared palette model.

Controls
--------
In the menu:

| Keys     |  Function         |
|---------:|:------------------|
|[Mode]    | Go back/Quit      |
|[2nd]     | Select option     |
|Arrow keys| Change menu option|

On the rules-selection screen, `[2nd]` toggles the highlighted rule or starts
the match from the top `Start Game` row, and `[Mode]` returns to card pack
selection.

During the card battle:

| Keys     |  Function         |
|---------:|:------------------|
|[Mode]    | Go back/forfeit   |
|[2nd]     | Select/place card |
|Arrow keys| Change selection  |

License and Copyright
---------------------
TODO

I'll do the proper writeup later. The most of the images are not mine.
The ones that obviously came from Final Fantasy 8 is owned by the guys who own
that game. The stuff that isn't mine isn't mine. The stuff that is mine is mine.

For the stuff that's mine, I'll stick in the BSD 2-clause license someplace and
call it good.

Custom Card Pack Requirements
-----------------------------
* Card ranks may only fall inside the inclusive range of 1 to 10 (A)
* Input image dimensions must be 52 by 52


