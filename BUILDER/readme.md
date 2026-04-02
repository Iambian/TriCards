# TriCards Builder Notes

This directory contains two different card-pack builders:

1. `build.bat` / `tools\tkit.py` builds the older `TriCrPak` format.
2. `tools\tkit2.py` builds the newer `Tri2Pak!` format used by the current client.

If you are building packs for the current client, use **`tkit2.py`**.

## Quick start

From the repository root:

```bat
python BUILDER\tools\tkit2.py --compression zx0 --var-name CRP7FF8
```

That command builds the default source pack from `BUILDER\src\ff8packorig`,
writes preview images to `BUILDER\obj\tkit2`, and writes the appvar to
`BUILDER\bin`.

## Defaults

`tkit2.py` defaults to:

| Setting | Default |
| --- | --- |
| Source directory | `BUILDER\src\ff8packorig` |
| Data file | `BUILDER\src\ff8packorig\data.json` |
| Preview directory | `BUILDER\obj\tkit2` |
| Output directory | `BUILDER\bin` |
| Output var name | derived from pack identifier in JSON |
| Target card size | `52x52` |
| Palette colors per card | `7` |
| Shared transparent color | `0x7C1F` |
| Compression | `zx7` |

## Important usage note for the default FF8 source pack

The script default compression is `zx7`, but the default FF8 source pack in this
repository currently exceeds the format size limit with that setting. Use
`--compression zx0` for the default FF8 build.

## Full build usage

```bat
python BUILDER\tools\tkit2.py ^
  --source-dir BUILDER\src\mypack ^
  --data-file BUILDER\src\mypack\data.json ^
  --preview-dir BUILDER\obj\mypack ^
  --bin-dir BUILDER\bin ^
  --var-name MYPACK ^
  --palette-colors 7 ^
  --transparent-color 0x7C1F ^
  --compression zx0
```

### CLI options

| Option | Meaning |
| --- | --- |
| positional `image` | Inspect one image instead of building a whole pack |
| `--source-dir` | Directory containing card images and optional frame PNGs |
| `--data-file` | JSON metadata file |
| `--card-type` | Card type override for single-image inspection |
| `--preview-dir` | Directory for masked/cropped/resized/preview PNGs |
| `--bin-dir` | Output directory for the `.8xv` file |
| `--var-name` | TI appvar variable name |
| `--palette-colors` | Opaque palette entry count stored per card |
| `--transparent-color` | Shared RGB1555 transparent color |
| `--compression` | `zx7` or `zx0` |
| `--compressor-path` | Explicit compressor executable path |

## Single-image inspection mode

To inspect how one card will be processed:

```bat
python BUILDER\tools\tkit2.py 001.png --compression zx0
```

If the image is not listed in the JSON, provide a card type:

```bat
python BUILDER\tools\tkit2.py custom.png --card-type Monster --source-dir BUILDER\src\mypack --data-file BUILDER\src\mypack\data.json
```

Inspection mode writes the intermediate/preview PNGs but does not build a full
pack.

## Source image behavior

`tkit2.py` supports both framed and frameless source art.

### Framed source art

If a card image appears to match the configured frame for its card type, the
builder will:

1. mask the frame away
2. crop using that type's configured crop box
3. resize the cropped art to `52x52`

This preserves the current FF8 source-art workflow.

### Frameless source art

If a card image does **not** appear to match the configured frame, the builder
will:

1. skip frame masking
2. skip crop-box trimming
3. resize the original source image directly to `52x52`

This allows custom packs to use source images without frames, including images
that are not `256x256`.

## JSON format

The pack JSON uses the same 9-field card entries as the other builder path:

```json
[
  "Pack description",
  "PACKID",
  [rank, "name", "type", up, right, down, left, "element", "image.png"]
]
```

Supported card types:

- `Monster`
- `Boss`
- `GF`
- `Player`

Supported elements:

- `none`
- `poison`
- `fire`
- `wind`
- `earth`
- `water`
- `ice`
- `thunder`
- `holy`

## Constants worth changing in `tools\tkit2.py`

If CLI options are not enough, these constants control the builder's behavior:

| Constant | Purpose |
| --- | --- |
| `DEFAULT_SOURCE_DIR` | Default input art directory |
| `DEFAULT_PREVIEW_DIR` | Default preview output directory |
| `DEFAULT_BIN_DIR` | Default `.8xv` output directory |
| `DEFAULT_DATA_FILE` | Default JSON metadata path |
| `TARGET_SIZE` | Final in-pack card image size |
| `DEFAULT_PALETTE_COLORS` | Default opaque palette count |
| `DEFAULT_TRANSPARENT_COLOR` | Default shared transparent RGB1555 color |
| `QUANTIZE_USE_DITHER` | Enables/disables quantization dithering |
| `ALPHA_OPAQUE_THRESHOLD` | Alpha cutoff used before quantization |
| `CROP_BOXES` | Per-card-type crop boxes used when a frame is detected |
| `FRAME_FILES` | Per-card-type frame template filenames |
| `FRAME_BRIGHTNESS_THRESHOLD` | Which frame-template pixels count as border/mask candidates |
| `FRAME_MATCH_THRESHOLD` | Allowed per-channel color difference when matching frame pixels |
| `FRAME_DETECTION_THRESHOLD` | Minimum overall frame-match ratio required before masking/cropping are applied |
| `COMPRESSION_METHODS` | Available compression backends and file paths |

## Output artifacts

When building a full pack, `tkit2.py` writes:

1. preview/intermediate images in the preview directory
2. a contact sheet in the preview directory
3. the final `.8xv` appvar in the bin directory

## Legacy builder

`build.bat` still runs the older builder:

```bat
BUILDER\build.bat BUILDER\src\ff8pack FF8PACK
```

Use that path only when you specifically need the older `TriCrPak` format.
