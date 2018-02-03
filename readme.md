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
* Not sure if necessary but you may need to use convpng on the CLIENT\src\gfx
  folder to build all the images. To do that, navigate to that folder, 
  open the command prompt, type `convpng`, then push enter.
* Open the command prompt and navigate to where the makefile is. Type `make`,
  push enter, and watch it go.
* If it all worked, TRICARDS.8xp will be in the CLIENT\bin folder.

Controls
--------
TODO

License and Copyright
---------------------
TODO

I'll do the proper writeup later. The most of the images are not mine.
The ones that obviously came from Final Fantasy 8 is owned by the guys who own
that game. The stuff that isn't mine isn't mine. The stuff that is mine is mine.

For the stuff that's mine, I'll stick in the BSD 2-clause license someplace and
call it good.



