"""TriCards card pack builder.

The active single-file format is `Tri2Pak!` version 2. It stores one logical
pack in one appvar payload:

    Header (32 bytes, little-endian)
    Card record table (fixed-size records)
    String table (null-terminated description, names, image filenames)
    Image blob (compressed indexed pixel streams)

Header layout:
    magic[8]              = b"Tri2Pak!"
    version               = uint8
    card_count            = uint16
    palette_entry_count   = uint8
    transparent_color     = uint16 RGB1555
    pack_identifier[9]    = null-padded/truncated Latin-1
    compression_method    = uint8 (0=zx7, 1=zx0)
    description_offset    = uint16 absolute payload offset
    record_table_offset   = uint16 absolute payload offset
    string_table_offset   = uint16 absolute payload offset
    image_blob_offset     = uint16 absolute payload offset

Each card record is:
    rank                  = uint8
    type                  = uint8
    up                    = uint8
    right                 = uint8
    down                  = uint8
    left                  = uint8
    element               = uint8
    reserved              = uint8
    name_offset           = uint16 absolute payload offset
    image_name_offset     = uint16 absolute payload offset
    image_offset          = uint16 absolute payload offset
    image_size            = uint16 compressed byte length
    palette[]             = palette_entry_count * uint16 RGB1555

When a pack does not fit in one appvar, the builder can emit a manifest plus
shards:

    Manifest payload (`Tri2Mft!`, version 3)
        Common 32-byte header
        Shard directory table
        String table (currently description only)

    Shard payload (`Tri2Shd!`, version 3)
        Common 32-byte header
        Card record table
        String table
        Image blob

Shard payloads deliberately keep the same local metadata layout as `Tri2Pak!`.
The key difference is that offsets remain local to each shard. The manifest
reuses the common 32-byte header shape, but its `record_table_offset` points to
the shard directory instead of card metadata. The number of shard directory
entries is derived from `string_table_offset - record_table_offset`.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw


SCRIPT_DIR = Path(__file__).resolve().parent
BUILDER_DIR = SCRIPT_DIR.parent
REPO_DIR = BUILDER_DIR.parent
DEFAULT_SOURCE_DIR = BUILDER_DIR / "src" / "ff8packorig"
DEFAULT_PREVIEW_DIR = BUILDER_DIR / "obj" / "tkit2"
DEFAULT_BIN_DIR = BUILDER_DIR / "bin"
DEFAULT_DATA_FILE = DEFAULT_SOURCE_DIR / "data.json"
ZX7_PATH = SCRIPT_DIR / "zx7.exe"
ZX0_PATH = SCRIPT_DIR / "zx0.exe"

SINGLE_FILE_MAGIC = b"Tri2Pak!"
MANIFEST_MAGIC = b"Tri2Mft!"
SHARD_MAGIC = b"Tri2Shd!"
SINGLE_FILE_FORMAT_VERSION = 2
MULTIFILE_FORMAT_VERSION = 3
HEADER_FORMAT = "<8sBHBH9sBHHHH"
CARD_RECORD_BASE_FORMAT = "<8B4H"
SHARD_DIRECTORY_FORMAT = "<8sHHHH"
TEXT_ENCODING = "latin-1"
MAX_PAYLOAD_BYTES = 0xFFFF - 2
NEAR_SINGLE_FILE_WARNING_BYTES = 1024
SHARD_MARKER_CANDIDATES = "STUVWXYZQJKL"

TARGET_SIZE = (52, 52)
DEFAULT_PALETTE_COLORS = 7
TRANSPARENT_INDEX = 255
DEFAULT_TRANSPARENT_COLOR = 0x7C1F
QUANTIZE_USE_DITHER = False
ALPHA_OPAQUE_THRESHOLD = 128

# Edit these crop boxes to tune how much of each frame type is removed.
# Format: (left, top, right, bottom), matching PIL.Image.crop semantics.
CROP_BOXES = {
    "Monster": (16, 16, 240, 240),
    "Boss": (16, 16, 240, 240),
    "GF": (16, 16, 240, 240),
    "Player": (16, 16, 240, 240),
}

FRAME_FILES = {
    "Monster": "frame-Monster.png",
    "Boss": "frame-Boss.png",
    "GF": "frame-GF.png",
    "Player": "frame-Player.png",
}

CARD_TYPE_ENUM = ["monster", "boss", "gf", "player"]
ELEMENT_ENUM = ["none", "poison", "fire", "wind", "earth", "water", "ice", "thunder", "holy"]

FRAME_BRIGHTNESS_THRESHOLD = 24
FRAME_MATCH_THRESHOLD = 36
FRAME_DETECTION_THRESHOLD = 0.85

COMPRESSION_METHODS = {
    "zx7": {"code": 0, "path": ZX7_PATH},
    "zx0": {"code": 1, "path": ZX0_PATH},
}


@dataclass(frozen=True)
class CardSource:
    rank: int
    name: str
    card_type: str
    up: int
    right: int
    down: int
    left: int
    element: str
    image_name: str


@dataclass(frozen=True)
class PackSource:
    description: str
    pack_identifier: str
    cards: list[CardSource]


@dataclass(frozen=True)
class BuiltCard:
    source: CardSource
    type_index: int
    element_index: int
    palette_1555: list[int]
    compressed_pixels: bytes
    preview_path: Path
    frame_detected: bool


@dataclass(frozen=True)
class BuiltShard:
    var_name: str
    first_card_index: int
    built_cards: list[BuiltCard]
    payload: bytes


@dataclass(frozen=True)
class ShardDirectoryEntry:
    var_name: str
    first_card_index: int
    card_count: int
    payload_size: int


class StringTable:
    def __init__(self) -> None:
        self._offsets: dict[str, int] = {}
        self._data = bytearray()

    def add(self, value: str) -> int:
        cached = self._offsets.get(value)
        if cached is not None:
            return cached
        offset = len(self._data)
        self._data.extend(text_to_bytes(value))
        self._data.append(0)
        self._offsets[value] = offset
        return offset

    def to_bytes(self) -> bytes:
        return bytes(self._data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build versioned TriCards card packs from original source art."
    )
    parser.add_argument(
        "image",
        nargs="?",
        help="Optional image filename inside the source directory. If provided, only inspection images are generated.",
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE_DIR,
        help="Directory containing original card PNGs and frame images.",
    )
    parser.add_argument(
        "--data-file",
        type=Path,
        default=DEFAULT_DATA_FILE,
        help="Pack data JSON used for pack metadata and card definitions.",
    )
    parser.add_argument(
        "--card-type",
        choices=sorted(FRAME_FILES),
        help="Override the inferred card type when inspecting a single image.",
    )
    parser.add_argument(
        "--preview-dir",
        type=Path,
        default=DEFAULT_PREVIEW_DIR,
        help="Directory where masked/cropped/preview PNGs will be written.",
    )
    parser.add_argument(
        "--bin-dir",
        type=Path,
        default=DEFAULT_BIN_DIR,
        help="Directory where the built .8xv pack will be written.",
    )
    parser.add_argument(
        "--var-name",
        help="TI appvar output name. Defaults to the JSON pack identifier.",
    )
    parser.add_argument(
        "--palette-colors",
        type=int,
        default=DEFAULT_PALETTE_COLORS,
        help="Opaque palette entry count stored with every card.",
    )
    parser.add_argument(
        "--transparent-color",
        type=parse_color_code,
        default=DEFAULT_TRANSPARENT_COLOR,
        help="Shared transparent color encoded as RGB1555 (accepts decimal or 0x-prefixed hex).",
    )
    parser.add_argument(
        "--compression",
        choices=sorted(COMPRESSION_METHODS),
        default="zx7",
        help="Compression method for per-card image streams.",
    )
    parser.add_argument(
        "--format",
        choices=("auto", "single", "multi"),
        default="auto",
        help="Output format policy: single file only, forced manifest+shards, or auto fallback.",
    )
    parser.add_argument(
        "--compressor-path",
        type=Path,
        help="Optional path override for the selected compressor executable. If omitted, tkit2 searches common repo locations.",
    )
    return parser.parse_args()


def parse_color_code(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0xFFFF:
        raise argparse.ArgumentTypeError("transparent color must fit in uint16")
    return parsed


def text_to_bytes(value: str) -> bytes:
    return str(value).encode(TEXT_ENCODING)


def normalize_var_name(value: str) -> str:
    normalized = "".join(ch for ch in str(value).upper() if ch.isalnum())
    if not normalized:
        raise ValueError("Variable name must contain at least one alphanumeric character.")
    return normalized[:8]


def get_default_var_name(pack_identifier: str) -> str:
    return normalize_var_name(pack_identifier or "TRI2PACK")


def normalize_enum(value: str) -> str:
    return value.strip().lower()


def load_pack_source(data_file: Path) -> PackSource:
    with data_file.open("r", encoding=TEXT_ENCODING) as handle:
        raw = json.load(handle)

    if not isinstance(raw, list) or len(raw) < 2:
        raise ValueError(f"Pack JSON is malformed: {data_file}")

    cards: list[CardSource] = []
    for index, entry in enumerate(raw[2:], start=2):
        if len(entry) != 9:
            raise ValueError(f"Card entry {index} does not contain 9 fields: {entry!r}")
        cards.append(
            CardSource(
                rank=int(entry[0]),
                name=str(entry[1]).strip(),
                card_type=str(entry[2]).strip(),
                up=int(entry[3]),
                right=int(entry[4]),
                down=int(entry[5]),
                left=int(entry[6]),
                element=str(entry[7]).strip(),
                image_name=str(entry[8]).strip(),
            )
        )

    return PackSource(
        description=str(raw[0]).strip(),
        pack_identifier=str(raw[1]).strip(),
        cards=cards,
    )


def validate_pack_source(pack: PackSource, source_dir: Path, palette_colors: int) -> None:
    if not 1 <= palette_colors <= 255:
        raise ValueError("--palette-colors must be between 1 and 255.")
    if not pack.pack_identifier:
        raise ValueError("Pack identifier cannot be blank.")
    if not pack.cards:
        raise ValueError("Pack must contain at least one card.")
    if len(pack.cards) > 0xFFFF:
        raise ValueError("Pack exceeds uint16 card count limit.")

    seen_names: set[str] = set()
    seen_images: set[str] = set()
    for card in pack.cards:
        normalized_type = normalize_enum(card.card_type)
        normalized_element = normalize_enum(card.element)
        if normalized_type not in CARD_TYPE_ENUM:
            raise ValueError(f"Unsupported card type for {card.name}: {card.card_type}")
        if normalized_element not in ELEMENT_ENUM:
            raise ValueError(f"Unsupported element for {card.name}: {card.element}")
        if not 1 <= card.rank <= 10:
            raise ValueError(f"Rank out of range for {card.name}: {card.rank}")
        for label, value in {
            "up": card.up,
            "right": card.right,
            "down": card.down,
            "left": card.left,
        }.items():
            if not 1 <= value <= 10:
                raise ValueError(f"{label} stat out of range for {card.name}: {value}")
        if not card.name:
            raise ValueError("Card name cannot be blank.")
        if not card.image_name:
            raise ValueError(f"Image filename cannot be blank for card {card.name}.")
        if card.name in seen_names:
            raise ValueError(f"Duplicate card name: {card.name}")
        if card.image_name in seen_images:
            raise ValueError(f"Duplicate card image filename: {card.image_name}")
        seen_names.add(card.name)
        seen_images.add(card.image_name)
        if not (source_dir / card.image_name).is_file():
            raise FileNotFoundError(f"Card image not found: {source_dir / card.image_name}")


def get_card_entry_map(pack: PackSource) -> dict[str, CardSource]:
    return {entry.image_name: entry for entry in pack.cards}


def get_frame_mask(frame: Image.Image) -> list[bool]:
    pixels = []
    frame_pixels = frame.load()
    for y in range(frame.height):
        for x in range(frame.width):
            red, green, blue, alpha = frame_pixels[x, y]
            is_border = alpha > 0 and max(red, green, blue) >= FRAME_BRIGHTNESS_THRESHOLD
            pixels.append(is_border)
    return pixels


def color_distance(left: tuple[int, int, int], right: tuple[int, int, int]) -> int:
    return max(abs(left[0] - right[0]), abs(left[1] - right[1]), abs(left[2] - right[2]))


def mask_frame(card: Image.Image, frame: Image.Image) -> Image.Image:
    if card.size != frame.size:
        raise ValueError(f"Image/frame size mismatch: {card.size} vs {frame.size}.")

    result = card.copy().convert("RGBA")
    frame = frame.convert("RGBA")
    result_pixels = result.load()
    frame_pixels = frame.load()
    frame_mask = get_frame_mask(frame)

    index = 0
    for y in range(result.height):
        for x in range(result.width):
            should_test = frame_mask[index]
            index += 1
            if not should_test:
                continue
            card_red, card_green, card_blue, card_alpha = result_pixels[x, y]
            frame_red, frame_green, frame_blue, _frame_alpha = frame_pixels[x, y]
            if card_alpha == 0:
                continue
            if color_distance(
                (card_red, card_green, card_blue),
                (frame_red, frame_green, frame_blue),
            ) <= FRAME_MATCH_THRESHOLD:
                result_pixels[x, y] = (0, 0, 0, 0)

    return result


def get_frame_match_ratio(card: Image.Image, frame: Image.Image) -> float:
    if card.size != frame.size:
        return 0.0

    card = card.convert("RGBA")
    frame = frame.convert("RGBA")
    card_pixels = card.load()
    frame_pixels = frame.load()
    frame_mask = get_frame_mask(frame)
    tested = 0
    matched = 0
    index = 0

    for y in range(card.height):
        for x in range(card.width):
            should_test = frame_mask[index]
            index += 1
            if not should_test:
                continue
            tested += 1
            card_red, card_green, card_blue, card_alpha = card_pixels[x, y]
            frame_red, frame_green, frame_blue, _frame_alpha = frame_pixels[x, y]
            if card_alpha == 0:
                continue
            if color_distance(
                (card_red, card_green, card_blue),
                (frame_red, frame_green, frame_blue),
            ) <= FRAME_MATCH_THRESHOLD:
                matched += 1

    if tested == 0:
        return 0.0
    return matched / tested


def has_removable_frame(card: Image.Image, frame: Image.Image | None) -> bool:
    if frame is None:
        return False
    return get_frame_match_ratio(card, frame) >= FRAME_DETECTION_THRESHOLD


def resize_preview(image: Image.Image, size: tuple[int, int]) -> Image.Image:
    return image.resize(size, Image.Resampling.LANCZOS)


def get_dither_mode() -> int:
    if QUANTIZE_USE_DITHER:
        return Image.Dither.FLOYDSTEINBERG
    return Image.Dither.NONE


def flatten_alpha(image: Image.Image) -> Image.Image:
    result = image.convert("RGBA")
    pixels = result.load()
    for y in range(result.height):
        for x in range(result.width):
            red, green, blue, alpha = pixels[x, y]
            if alpha < ALPHA_OPAQUE_THRESHOLD:
                pixels[x, y] = (0, 0, 0, 0)
            else:
                pixels[x, y] = (red, green, blue, 255)
    return result


def rgb888_to_rgb1555(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3)


def quantize_card_image(
    image: Image.Image,
    palette_colors: int,
) -> tuple[Image.Image, list[int], bytes]:
    rgba = flatten_alpha(image)
    alpha = rgba.getchannel("A")
    alpha_bbox = alpha.getbbox()
    if alpha_bbox is None:
        palette_1555 = [0] * palette_colors
        pixel_bytes = bytes([TRANSPARENT_INDEX] * (rgba.width * rgba.height))
        return rgba, palette_1555, pixel_bytes

    opaque_crop = rgba.crop(alpha_bbox).convert("RGB")
    dither = get_dither_mode()
    palette_source = opaque_crop.quantize(
        colors=palette_colors,
        kmeans=3,
        method=Image.Quantize.FASTOCTREE,
        dither=dither,
    )

    source_palette = list(palette_source.getpalette() or [])
    source_palette.extend([0] * (768 - len(source_palette)))
    palette_rgb: list[tuple[int, int, int]] = []
    for index in range(palette_colors):
        base = index * 3
        palette_rgb.append(
            (
                source_palette[base + 0],
                source_palette[base + 1],
                source_palette[base + 2],
            )
        )

    palette_data: list[int] = []
    for rgb in palette_rgb:
        palette_data.extend(rgb)
    filler = palette_rgb[0] if palette_rgb else (0, 0, 0)
    while len(palette_data) < 768:
        palette_data.extend(filler)
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(palette_data[:768])

    quantized_indices = rgba.convert("RGB").quantize(
        palette=palette_image,
        dither=dither,
    )
    quantized_rgba = quantized_indices.convert("RGBA")
    quantized_rgba.putalpha(alpha)
    quantized_rgba = flatten_alpha(quantized_rgba)

    alpha_pixels = alpha.load()
    index_pixels = quantized_indices.load()
    quantized_palette = list(quantized_indices.getpalette() or [])
    quantized_palette.extend([0] * (768 - len(quantized_palette)))

    used_colors: list[tuple[int, int, int]] = []
    color_to_index: dict[tuple[int, int, int], int] = {}
    for y in range(rgba.height):
        for x in range(rgba.width):
            if alpha_pixels[x, y] < ALPHA_OPAQUE_THRESHOLD:
                continue
            source_index = index_pixels[x, y]
            base = source_index * 3
            rgb = (
                quantized_palette[base + 0],
                quantized_palette[base + 1],
                quantized_palette[base + 2],
            )
            if rgb not in color_to_index:
                color_to_index[rgb] = len(used_colors)
                used_colors.append(rgb)

    if len(used_colors) > palette_colors:
        raise ValueError(
            f"Quantized image used {len(used_colors)} opaque colors, exceeds palette size {palette_colors}."
        )
    compact_palette_1555 = []
    for rgb in used_colors:
        compact_palette_1555.append(rgb888_to_rgb1555(*rgb))
    compact_palette_1555.extend([0] * (palette_colors - len(compact_palette_1555)))

    pixel_bytes = bytearray()
    for y in range(rgba.height):
        for x in range(rgba.width):
            if alpha_pixels[x, y] < ALPHA_OPAQUE_THRESHOLD:
                pixel_bytes.append(TRANSPARENT_INDEX)
            else:
                source_index = index_pixels[x, y]
                base = source_index * 3
                rgb = (
                    quantized_palette[base + 0],
                    quantized_palette[base + 1],
                    quantized_palette[base + 2],
                )
                pixel_bytes.append(color_to_index[rgb])

    return quantized_rgba, compact_palette_1555, bytes(pixel_bytes)


def find_repo_executable(filename: str) -> Path | None:
    preferred_paths = [
        SCRIPT_DIR / filename,
        REPO_DIR / filename,
        REPO_DIR / "tools" / filename,
        REPO_DIR / "zx0" / filename,
    ]
    for path in preferred_paths:
        if path.is_file():
            return path.resolve()

    matches = sorted(REPO_DIR.rglob(filename))
    for match in matches:
        if match.is_file():
            return match.resolve()
    return None


def get_compressor_path(method: str, override_path: Path | None) -> Path:
    if method not in COMPRESSION_METHODS:
        raise ValueError(f"Unsupported compression method: {method}")
    if override_path is not None:
        return override_path.resolve()

    default_path = COMPRESSION_METHODS[method]["path"]
    if default_path.is_file():
        return default_path.resolve()

    discovered = find_repo_executable(default_path.name)
    if discovered is not None:
        return discovered
    return default_path.resolve()


def compress_data(data: bytes, method: str, override_path: Path | None = None) -> bytes:
    compressor_path = get_compressor_path(method, override_path)
    if not compressor_path.is_file():
        raise FileNotFoundError(
            f"{method.upper()} executable not found: {compressor_path}"
        )

    with tempfile.TemporaryDirectory(prefix="tkit2-") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        raw_path = temp_dir / "raw.bin"
        compressed_path = temp_dir / "compressed.bin"
        raw_path.write_bytes(data)

        if method == "zx7":
            command = [str(compressor_path), str(raw_path), str(compressed_path)]
        elif method == "zx0":
            command = [str(compressor_path), "-f", str(raw_path), str(compressed_path)]
        else:
            raise ValueError(f"Unsupported compression method: {method}")

        result = subprocess.run(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace").strip()
            raise RuntimeError(
                f"{method.upper()} compression failed: {stderr or result.returncode}"
            )

        return compressed_path.read_bytes()


def save_outputs(
    card_path: Path,
    masked: Image.Image,
    cropped: Image.Image,
    resized: Image.Image,
    quantized: Image.Image,
    output_dir: Path,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = card_path.stem
    masked.save(output_dir / f"{stem}-masked.png")
    cropped.save(output_dir / f"{stem}-cropped.png")
    resized.save(output_dir / f"{stem}-resized-52.png")
    preview_path = output_dir / f"{stem}-preview-52.png"
    quantized.save(preview_path)
    return preview_path


def process_card(
    card: CardSource,
    source_dir: Path,
    output_dir: Path,
    frame_cache: dict[str, Image.Image],
    palette_colors: int,
    compression_method: str,
    compressor_path: Path | None,
    write_outputs: bool = True,
) -> BuiltCard:
    frame = None

    if card.card_type not in FRAME_FILES:
        raise KeyError(f"Could not determine card type for {card.image_name}.")

    frame_path = source_dir / FRAME_FILES[card.card_type]
    if frame_path.is_file() and card.card_type not in frame_cache:
        frame_cache[card.card_type] = Image.open(frame_path).convert("RGBA")
    if frame_path.is_file():
        frame = frame_cache[card.card_type]

    card_path = source_dir / card.image_name
    raw_card = Image.open(card_path).convert("RGBA")
    frame_detected = has_removable_frame(raw_card, frame)
    if frame_detected:
        masked = mask_frame(raw_card, frame)
        cropped = masked.crop(CROP_BOXES[card.card_type])
    else:
        masked = raw_card.copy()
        cropped = raw_card.copy()
    resized = resize_preview(cropped, TARGET_SIZE)
    quantized, palette_1555, pixel_bytes = quantize_card_image(resized, palette_colors)
    preview_path = (
        save_outputs(card_path, masked, cropped, resized, quantized, output_dir)
        if write_outputs
        else output_dir / f"{card_path.stem}-preview-52.png"
    )

    return BuiltCard(
        source=card,
        type_index=CARD_TYPE_ENUM.index(normalize_enum(card.card_type)),
        element_index=ELEMENT_ENUM.index(normalize_enum(card.element)),
        palette_1555=palette_1555,
        compressed_pixels=compress_data(pixel_bytes, compression_method, compressor_path),
        preview_path=preview_path,
        frame_detected=frame_detected,
    )


def build_contact_sheet(pack: PackSource, built_cards: list[BuiltCard], output_dir: Path, columns: int = 5) -> Path:
    if not built_cards:
        raise ValueError("No previews available for contact sheet generation.")

    label_height = 16
    tile_width, tile_height = TARGET_SIZE
    rows = math.ceil(len(built_cards) / columns)
    sheet = Image.new(
        "RGBA",
        (columns * tile_width, rows * (tile_height + label_height)),
        (24, 24, 24, 255),
    )
    draw = ImageDraw.Draw(sheet)
    label_lookup = {entry.image_name: entry.name for entry in pack.cards}

    for index, built_card in enumerate(built_cards):
        preview = Image.open(built_card.preview_path).convert("RGBA")
        col = index % columns
        row = index // columns
        x = col * tile_width
        y = row * (tile_height + label_height)
        sheet.paste(preview, (x, y), preview)
        card_name = label_lookup.get(built_card.source.image_name, built_card.source.image_name)
        draw.text((x + 1, y + tile_height + 2), card_name[:8], fill=(220, 220, 220, 255))

    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / "contact-sheet.png"
    sheet.save(output_path)
    return output_path


def estimate_local_pack_payload_size(pack: PackSource, built_cards: list[BuiltCard]) -> int:
    if not built_cards:
        raise ValueError("No cards available to size.")

    palette_entry_count = len(built_cards[0].palette_1555)
    if any(len(card.palette_1555) != palette_entry_count for card in built_cards):
        raise ValueError("All cards must share the same palette entry count.")

    header_size = struct.calcsize(HEADER_FORMAT)
    record_size = struct.calcsize(CARD_RECORD_BASE_FORMAT) + (palette_entry_count * 2)
    strings = StringTable()
    strings.add(pack.description)
    for card in built_cards:
        strings.add(card.source.name)
        strings.add(card.source.image_name)
    string_blob = strings.to_bytes()
    image_blob_size = sum(len(card.compressed_pixels) for card in built_cards)
    return header_size + (len(built_cards) * record_size) + len(string_blob) + image_blob_size


def serialize_common_header(
    magic: bytes,
    version: int,
    card_count: int,
    palette_entry_count: int,
    transparent_color: int,
    pack_identifier: str,
    compression_method: str,
    description_offset: int,
    record_table_offset: int,
    string_table_offset: int,
    image_blob_offset: int,
) -> bytes:
    if any(offset > 0xFFFF for offset in (
        description_offset,
        record_table_offset,
        string_table_offset,
        image_blob_offset,
    )):
        raise ValueError("Pack payload exceeds uint16 header offset range.")

    return struct.pack(
        HEADER_FORMAT,
        magic,
        version,
        card_count,
        palette_entry_count,
        transparent_color,
        text_to_bytes(pack_identifier).ljust(9, b"\x00")[:9],
        COMPRESSION_METHODS[compression_method]["code"],
        description_offset,
        record_table_offset,
        string_table_offset,
        image_blob_offset,
    )


def serialize_local_pack_payload(
    magic: bytes,
    version: int,
    pack: PackSource,
    built_cards: list[BuiltCard],
    transparent_color: int,
    compression_method: str,
) -> bytes:
    if not built_cards:
        raise ValueError("No cards available to serialize.")

    palette_entry_count = len(built_cards[0].palette_1555)
    if any(len(card.palette_1555) != palette_entry_count for card in built_cards):
        raise ValueError("All cards must share the same palette entry count.")

    header_size = struct.calcsize(HEADER_FORMAT)
    record_size = struct.calcsize(CARD_RECORD_BASE_FORMAT) + (palette_entry_count * 2)

    strings = StringTable()
    description_rel_offset = strings.add(pack.description)
    name_rel_offsets = [strings.add(card.source.name) for card in built_cards]
    image_name_rel_offsets = [strings.add(card.source.image_name) for card in built_cards]
    string_blob = strings.to_bytes()

    record_table_offset = header_size
    string_table_offset = record_table_offset + (len(built_cards) * record_size)
    image_blob_offset = string_table_offset + len(string_blob)

    image_offset = image_blob_offset
    record_blob = bytearray()
    image_blob = bytearray()

    for index, card in enumerate(built_cards):
        if image_offset > 0xFFFF:
            raise ValueError("Pack payload exceeds uint16 offset range.")
        if len(card.compressed_pixels) > 0xFFFF:
            raise ValueError(f"Compressed image too large for {card.source.name}.")
        record_blob.extend(
            struct.pack(
                CARD_RECORD_BASE_FORMAT,
                card.source.rank,
                card.type_index,
                card.source.up,
                card.source.right,
                card.source.down,
                card.source.left,
                card.element_index,
                0,
                string_table_offset + name_rel_offsets[index],
                string_table_offset + image_name_rel_offsets[index],
                image_offset,
                len(card.compressed_pixels),
            )
        )
        record_blob.extend(struct.pack(f"<{palette_entry_count}H", *card.palette_1555))
        image_blob.extend(card.compressed_pixels)
        image_offset += len(card.compressed_pixels)

    payload_size = image_blob_offset + len(image_blob)
    if payload_size > MAX_PAYLOAD_BYTES:
        raise ValueError(
            f"Pack payload is too large for this format/wrapper: {payload_size} bytes."
        )

    header = serialize_common_header(
        magic,
        version,
        len(built_cards),
        palette_entry_count,
        transparent_color,
        pack.pack_identifier,
        compression_method,
        string_table_offset + description_rel_offset,
        record_table_offset,
        string_table_offset,
        image_blob_offset,
    )

    return header + bytes(record_blob) + string_blob + bytes(image_blob)


def serialize_pack(
    pack: PackSource,
    built_cards: list[BuiltCard],
    transparent_color: int,
    compression_method: str,
) -> bytes:
    return serialize_local_pack_payload(
        SINGLE_FILE_MAGIC,
        SINGLE_FILE_FORMAT_VERSION,
        pack,
        built_cards,
        transparent_color,
        compression_method,
    )


def serialize_shard(
    pack: PackSource,
    built_cards: list[BuiltCard],
    transparent_color: int,
    compression_method: str,
) -> bytes:
    return serialize_local_pack_payload(
        SHARD_MAGIC,
        MULTIFILE_FORMAT_VERSION,
        pack,
        built_cards,
        transparent_color,
        compression_method,
    )


def serialize_manifest(
    pack: PackSource,
    transparent_color: int,
    compression_method: str,
    palette_entry_count: int,
    shard_entries: list[ShardDirectoryEntry],
) -> bytes:
    if not shard_entries:
        raise ValueError("Manifest requires at least one shard entry.")

    header_size = struct.calcsize(HEADER_FORMAT)
    shard_record_size = struct.calcsize(SHARD_DIRECTORY_FORMAT)
    shard_table_offset = header_size
    string_table_offset = shard_table_offset + (len(shard_entries) * shard_record_size)

    strings = StringTable()
    description_rel_offset = strings.add(pack.description)
    string_blob = strings.to_bytes()

    shard_blob = bytearray()
    for entry in shard_entries:
        shard_blob.extend(
            struct.pack(
                SHARD_DIRECTORY_FORMAT,
                text_to_bytes(entry.var_name).ljust(8, b"\x00")[:8],
                entry.first_card_index,
                entry.card_count,
                entry.payload_size,
                0,
            )
        )

    payload_size = string_table_offset + len(string_blob)
    if payload_size > MAX_PAYLOAD_BYTES:
        raise ValueError(
            f"Manifest payload is too large for this format/wrapper: {payload_size} bytes."
        )

    header = serialize_common_header(
        MANIFEST_MAGIC,
        MULTIFILE_FORMAT_VERSION,
        sum(entry.card_count for entry in shard_entries),
        palette_entry_count,
        transparent_color,
        pack.pack_identifier,
        compression_method,
        string_table_offset + description_rel_offset,
        shard_table_offset,
        string_table_offset,
        0,
    )

    return header + bytes(shard_blob) + string_blob


def to_base36(value: int, width: int) -> str:
    if value < 0:
        raise ValueError("Base36 values must be non-negative.")
    alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    result = []
    while value:
        value, digit = divmod(value, 36)
        result.append(alphabet[digit])
    if not result:
        result.append("0")
    encoded = "".join(reversed(result))
    if len(encoded) > width:
        raise ValueError("Value does not fit in requested base36 width.")
    return encoded.rjust(width, "0")


def build_manifest_name(var_name: str) -> str:
    return normalize_var_name(var_name)


def build_shard_names(manifest_name: str, shard_count: int) -> list[str]:
    prefix = manifest_name[:4].ljust(4, "X")
    for marker in SHARD_MARKER_CANDIDATES:
        shard_names = [f"{prefix}{marker}{to_base36(index, 3)}" for index in range(shard_count)]
        if manifest_name not in shard_names and len(set(shard_names)) == len(shard_names):
            return shard_names
    raise ValueError(f"Could not derive unique shard names from manifest name {manifest_name}.")


def partition_pack_into_shards(
    pack: PackSource,
    built_cards: list[BuiltCard],
    manifest_name: str,
    transparent_color: int,
    compression_method: str,
) -> list[BuiltShard]:
    shard_counts: list[int] = []
    start_index = 0

    while start_index < len(built_cards):
        low = 1
        high = len(built_cards) - start_index
        best_count = 0
        best_payload: bytes | None = None
        while low <= high:
            mid = (low + high) // 2
            candidate_cards = built_cards[start_index : start_index + mid]
            try:
                candidate_payload = serialize_shard(
                    pack,
                    candidate_cards,
                    transparent_color,
                    compression_method,
                )
            except ValueError:
                candidate_payload = None
            if candidate_payload is not None and len(candidate_payload) <= MAX_PAYLOAD_BYTES:
                best_count = mid
                best_payload = candidate_payload
                low = mid + 1
            else:
                high = mid - 1

        if best_count == 0 or best_payload is None:
            raise ValueError(
                f"Card {built_cards[start_index].source.name} does not fit in a single shard."
            )

        shard_counts.append(best_count)
        start_index += best_count

    shard_names = build_shard_names(manifest_name, len(shard_counts))
    shards: list[BuiltShard] = []
    start_index = 0
    for shard_index, card_count in enumerate(shard_counts):
        shard_cards = built_cards[start_index : start_index + card_count]
        shards.append(
            BuiltShard(
                var_name=shard_names[shard_index],
                first_card_index=start_index,
                built_cards=shard_cards,
                payload=serialize_shard(pack, shard_cards, transparent_color, compression_method),
            )
        )
        start_index += card_count

    return shards


TI_VAR_APPVAR_TYPE = 0x15
TI_VAR_FLAG_ARCHIVED = 0x80


def export8xv(filepath: Path, filename: str, filedata: bytes) -> Path:
    filename = normalize_var_name(filename)
    data_size = len(filedata) + 2
    if data_size > 0xFFFF:
        raise ValueError(f"Appvar payload exceeds uint16 size limit: {len(filedata)} bytes.")

    dsl = len(filedata) & 0xFF
    dsh = (len(filedata) >> 8) & 0xFF
    filedata = bytes([dsl, dsh]) + filedata

    vsl = len(filedata) & 0xFF
    vsh = (len(filedata) >> 8) & 0xFF
    vh = bytes([0x0D, 0x00, vsl, vsh, TI_VAR_APPVAR_TYPE])
    vh += text_to_bytes(filename).ljust(8, b"\x00")[:8]
    vh += bytes([0x00, TI_VAR_FLAG_ARCHIVED, vsl, vsh])

    varentry = vh + filedata
    varsizel = len(varentry) & 0xFF
    varsizeh = (len(varentry) >> 8) & 0xFF
    checksum = sum(varentry)
    vchkl = checksum & 0xFF
    vchkh = (checksum >> 8) & 0xFF

    header = b"**TI83F*"
    header += bytes([0x1A, 0x0A, 0x00])
    header += text_to_bytes("TriCards tkit2 pack builder").ljust(42, b" ")[:42]
    header += bytes([varsizel, varsizeh])
    header += varentry
    header += bytes([vchkl, vchkh])

    filepath.mkdir(parents=True, exist_ok=True)
    output_path = filepath / f"{filename}.8xv"
    output_path.write_bytes(header)
    return output_path


def inspect_single_card(
    image_name: str,
    pack: PackSource,
    source_dir: Path,
    preview_dir: Path,
    palette_colors: int,
    compression_method: str,
    compressor_path: Path | None,
    override_card_type: str | None,
) -> None:
    entry_map = get_card_entry_map(pack)
    file_name = Path(image_name).name
    card_entry = entry_map.get(file_name)
    if card_entry is None and override_card_type is None:
        raise KeyError(f"Could not determine card type for {file_name}. Use --card-type to override.")

    if card_entry is None:
        card_entry = CardSource(
            rank=0,
            name=file_name,
            card_type=override_card_type,
            up=0,
            right=0,
            down=0,
            left=0,
            element="none",
            image_name=file_name,
        )
    elif override_card_type is not None:
        card_entry = CardSource(
            rank=card_entry.rank,
            name=card_entry.name,
            card_type=override_card_type,
            up=card_entry.up,
            right=card_entry.right,
            down=card_entry.down,
            left=card_entry.left,
            element=card_entry.element,
            image_name=card_entry.image_name,
        )

    frame_cache: dict[str, Image.Image] = {}
    built_card = process_card(
        card_entry,
        source_dir=source_dir,
        output_dir=preview_dir,
        frame_cache=frame_cache,
        palette_colors=palette_colors,
        compression_method=compression_method,
        compressor_path=compressor_path,
    )
    print(f"image: {source_dir / card_entry.image_name}")
    print(f"type: {card_entry.card_type}")
    print(f"frame detected: {built_card.frame_detected}")
    print(f"crop: {CROP_BOXES[card_entry.card_type] if built_card.frame_detected else 'skipped'}")
    print(f"preview: {built_card.preview_path}")
    print(f"palette entries: {len(built_card.palette_1555)}")
    print(f"compression: {compression_method}")
    print(f"compressed image bytes: {len(built_card.compressed_pixels)}")


def build_pack(
    pack: PackSource,
    source_dir: Path,
    preview_dir: Path,
    bin_dir: Path,
    var_name: str,
    palette_colors: int,
    transparent_color: int,
    compression_method: str,
    compressor_path: Path | None,
    output_format: str,
) -> None:
    frame_cache: dict[str, Image.Image] = {}
    built_cards = [
        process_card(
            card,
            source_dir=source_dir,
            output_dir=preview_dir,
            frame_cache=frame_cache,
            palette_colors=palette_colors,
            compression_method=compression_method,
            compressor_path=compressor_path,
        )
        for card in pack.cards
    ]

    contact_sheet = build_contact_sheet(pack, built_cards, preview_dir)
    manifest_name = build_manifest_name(var_name)
    single_payload_bytes = estimate_local_pack_payload_size(pack, built_cards)

    if output_format == "single":
        if single_payload_bytes > MAX_PAYLOAD_BYTES:
            raise ValueError(
                "Single-file output was requested, but the payload exceeds the appvar limit by "
                f"{single_payload_bytes - MAX_PAYLOAD_BYTES} bytes."
            )
        payload = serialize_pack(pack, built_cards, transparent_color, compression_method)
        output_path = export8xv(bin_dir, manifest_name, payload)

        print(f"processed: {len(built_cards)} cards")
        print(f"preview dir: {preview_dir}")
        print(f"contact sheet: {contact_sheet}")
        print("output format: single")
        print(f"payload magic: {SINGLE_FILE_MAGIC.decode(TEXT_ENCODING)}")
        print(f"payload version: {SINGLE_FILE_FORMAT_VERSION}")
        print(f"compression: {compression_method} ({COMPRESSION_METHODS[compression_method]['code']})")
        print(f"palette entries: {palette_colors}")
        print(f"transparent color: 0x{transparent_color:04X}")
        print(f"pack file: {output_path}")
        print(f"payload bytes: {len(payload)}")
        return

    use_multifile = output_format == "multi" or single_payload_bytes > MAX_PAYLOAD_BYTES
    if not use_multifile:
        payload = serialize_pack(pack, built_cards, transparent_color, compression_method)
        output_path = export8xv(bin_dir, manifest_name, payload)

        print(f"processed: {len(built_cards)} cards")
        print(f"preview dir: {preview_dir}")
        print(f"contact sheet: {contact_sheet}")
        print("output format: single")
        print(f"payload magic: {SINGLE_FILE_MAGIC.decode(TEXT_ENCODING)}")
        print(f"payload version: {SINGLE_FILE_FORMAT_VERSION}")
        print(f"compression: {compression_method} ({COMPRESSION_METHODS[compression_method]['code']})")
        print(f"palette entries: {palette_colors}")
        print(f"transparent color: 0x{transparent_color:04X}")
        print(f"pack file: {output_path}")
        print(f"payload bytes: {len(payload)}")
        return

    shards = partition_pack_into_shards(
        pack,
        built_cards,
        manifest_name,
        transparent_color,
        compression_method,
    )
    shard_entries = [
        ShardDirectoryEntry(
            var_name=shard.var_name,
            first_card_index=shard.first_card_index,
            card_count=len(shard.built_cards),
            payload_size=len(shard.payload),
        )
        for shard in shards
    ]
    manifest_payload = serialize_manifest(
        pack,
        transparent_color,
        compression_method,
        palette_colors,
        shard_entries,
    )
    manifest_path = export8xv(bin_dir, manifest_name, manifest_payload)
    shard_paths = [export8xv(bin_dir, shard.var_name, shard.payload) for shard in shards]

    print(f"processed: {len(built_cards)} cards")
    print(f"preview dir: {preview_dir}")
    print(f"contact sheet: {contact_sheet}")
    print("output format: multi")
    print(f"manifest magic: {MANIFEST_MAGIC.decode(TEXT_ENCODING)}")
    print(f"shard magic: {SHARD_MAGIC.decode(TEXT_ENCODING)}")
    print(f"payload version: {MULTIFILE_FORMAT_VERSION}")
    print(f"compression: {compression_method} ({COMPRESSION_METHODS[compression_method]['code']})")
    print(f"palette entries: {palette_colors}")
    print(f"transparent color: 0x{transparent_color:04X}")
    print(f"manifest file: {manifest_path}")
    print(f"manifest payload bytes: {len(manifest_payload)}")
    print(f"shard count: {len(shards)}")
    for shard, shard_path in zip(shards, shard_paths):
        shard_last_card = shard.first_card_index + len(shard.built_cards) - 1
        print(
            "shard file: "
            f"{shard_path} (cards {shard.first_card_index}-{shard_last_card}, payload {len(shard.payload)} bytes)"
        )
    if output_format == "auto" and single_payload_bytes > MAX_PAYLOAD_BYTES:
        print(
            "notice: single-file output exceeded the appvar limit; "
            f"used manifest+shards instead ({single_payload_bytes - MAX_PAYLOAD_BYTES} bytes over)."
        )
    if (
        single_payload_bytes > MAX_PAYLOAD_BYTES
        and (single_payload_bytes - MAX_PAYLOAD_BYTES) <= NEAR_SINGLE_FILE_WARNING_BYTES
    ):
        print(
            "warning: sharding occurred even though the pack almost fit in one appvar "
            f"({single_payload_bytes - MAX_PAYLOAD_BYTES} bytes over the single-file limit)."
        )


def main() -> int:
    args = parse_args()
    source_dir = args.source_dir.resolve()
    preview_dir = args.preview_dir.resolve()
    bin_dir = args.bin_dir.resolve()
    data_file = args.data_file.resolve()

    if not data_file.is_file():
        raise FileNotFoundError(f"Data file not found: {data_file}")

    pack = load_pack_source(data_file)
    validate_pack_source(pack, source_dir, args.palette_colors)
    var_name = normalize_var_name(args.var_name or get_default_var_name(pack.pack_identifier))

    if args.image:
        inspect_single_card(
            image_name=args.image,
            pack=pack,
            source_dir=source_dir,
            preview_dir=preview_dir,
            palette_colors=args.palette_colors,
            compression_method=args.compression,
            compressor_path=args.compressor_path,
            override_card_type=args.card_type,
        )
        return 0

    build_pack(
        pack=pack,
        source_dir=source_dir,
        preview_dir=preview_dir,
        bin_dir=bin_dir,
        var_name=var_name,
        palette_colors=args.palette_colors,
        transparent_color=args.transparent_color,
        compression_method=args.compression,
        compressor_path=args.compressor_path,
        output_format=args.format,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
