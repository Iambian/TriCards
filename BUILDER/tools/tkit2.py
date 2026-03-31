"""TriCards card pack builder, version 2.

This builder keeps the TI appvar wrapper used by the original toolchain, but
emits a new versioned payload with a different magic string so the legacy
client cannot open it accidentally.

Payload layout, version 2:

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

Image data stores one byte per pixel before compression. Palette entries use
indices 0..palette_entry_count-1, and 255 means transparent. The shared
transparent RGB1555 color is stored once in the file header and is not part of
the per-card palettes. A future client is expected to decompress the stream,
load the record palette into hardware palette slots, remap opaque bytes into the
chosen hardware indices, and preserve 255 as the transparent sentinel or rewrite
it to the client's active transparent index.
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

MAGIC = b"Tri2Pak!"
FORMAT_VERSION = 2
HEADER_FORMAT = "<8sBHBH9sBHHHH"
CARD_RECORD_BASE_FORMAT = "<8B4H"
TEXT_ENCODING = "latin-1"

TARGET_SIZE = (52, 52)
DEFAULT_PALETTE_COLORS = 6
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


def get_default_var_name(pack_identifier: str) -> str:
    cleaned = "".join(ch for ch in pack_identifier.upper() if ch.isalnum())
    return (cleaned or "TRI2PACK")[:8]


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
    if card.card_type not in FRAME_FILES:
        raise KeyError(f"Could not determine card type for {card.image_name}.")

    frame_path = source_dir / FRAME_FILES[card.card_type]
    if not frame_path.is_file():
        raise FileNotFoundError(f"Frame image not found: {frame_path}")
    if card.card_type not in frame_cache:
        frame_cache[card.card_type] = Image.open(frame_path).convert("RGBA")

    card_path = source_dir / card.image_name
    raw_card = Image.open(card_path).convert("RGBA")
    masked = mask_frame(raw_card, frame_cache[card.card_type])
    cropped = masked.crop(CROP_BOXES[card.card_type])
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


def serialize_pack(
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
    if payload_size > 0xFFFF:
        raise ValueError(
            f"Pack payload is too large for this format/wrapper: {payload_size} bytes."
        )

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        FORMAT_VERSION,
        len(built_cards),
        palette_entry_count,
        transparent_color,
        text_to_bytes(pack.pack_identifier).ljust(9, b"\x00")[:9],
        COMPRESSION_METHODS[compression_method]["code"],
        string_table_offset + description_rel_offset,
        record_table_offset,
        string_table_offset,
        image_blob_offset,
    )

    return header + bytes(record_blob) + string_blob + bytes(image_blob)


TI_VAR_APPVAR_TYPE = 0x15
TI_VAR_FLAG_ARCHIVED = 0x80


def export8xv(filepath: Path, filename: str, filedata: bytes) -> Path:
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
    print(f"crop: {CROP_BOXES[card_entry.card_type]}")
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

    payload = serialize_pack(pack, built_cards, transparent_color, compression_method)
    output_path = export8xv(bin_dir, var_name, payload)
    contact_sheet = build_contact_sheet(pack, built_cards, preview_dir)

    print(f"processed: {len(built_cards)} cards")
    print(f"preview dir: {preview_dir}")
    print(f"contact sheet: {contact_sheet}")
    print(f"payload magic: {MAGIC.decode(TEXT_ENCODING)}")
    print(f"payload version: {FORMAT_VERSION}")
    print(f"compression: {compression_method} ({COMPRESSION_METHODS[compression_method]['code']})")
    print(f"palette entries: {palette_colors}")
    print(f"transparent color: 0x{transparent_color:04X}")
    print(f"pack file: {output_path}")
    print(f"payload bytes: {len(payload)}")


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
    var_name = args.var_name or get_default_var_name(pack.pack_identifier)

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
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
