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

Building Pack Viewer / Game Player
----------------------------------
* Download and install the ZDS-based CE C Software Development Kit (v7.5)
  https://github.com/CE-Programming/toolchain/releases/tag/v7.5 and install it.
  idk how to modify to make it work on v8.x, will add instructions later if needed.
* The client static graphics are now generated from `CLIENT\src\gfx\convimg.yaml`.
  From `CLIENT\`, run `make gfx` before `make` whenever the graphics config or
  source PNGs change.
* Open the command prompt and navigate to where the makefile is. Type `make`,
  push enter, and watch it go.
* If it all worked, TRICARDS.8xp will be in the CLIENT\bin folder.

Current Graphics / Runtime Status
---------------------------------
The graphics and palette work is currently in transition.

What has been changed so far:
* `CLIENT\src\main.c` has been split so the client is now organized around
  `main.c`, `card_loading.c`, `gameplay.c`, and the shared declarations in
  `tricards.h`.
* Internal client assets are no longer using the old implicit xlibc-only setup.
  `make gfx` now generates a shared internal palette from `convimg.yaml`, and
  the client loads that palette at runtime.
* Card images now use per-slot palette slices starting at palette index `100`.
* The pack loader now derives card-record width from the pack table offsets at
  runtime, which fixed browser mismatches caused by assuming record stride from
  palette-count metadata alone.
* The card browser now keeps list-name reads separate from preview-image loads,
  preventing palette instability caused by reusing the same slot for both text
  lookup and the selected-card preview.

What is still not finished:
* The palette migration is not considered complete yet.
* More cleanup and validation are still needed around remaining assumptions in
  the client, asset colors, and overall runtime behavior.
* The pack-format transition is also still in progress, so builder/runtime
  alignment should not be assumed to be fully settled yet.

Controls
--------
In the menu:

| Keys     |  Function         |
|---------:|:------------------|
|[Mode]    | Go back/Quit      |
|[2nd]     | Select option     |
|Arrow keys| Change menu option|

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


