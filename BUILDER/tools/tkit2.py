# Card package builder, version 2.

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw


SCRIPT_DIR = Path(__file__).resolve().parent
BUILDER_DIR = SCRIPT_DIR.parent
DEFAULT_SOURCE_DIR = BUILDER_DIR / "src" / "ff8packorig"
DEFAULT_OUTPUT_DIR = BUILDER_DIR / "obj" / "tkit2"
DEFAULT_DATA_FILE = DEFAULT_SOURCE_DIR / "data.json"
TARGET_SIZE = (52, 52)
QUANTIZE_TO_COLORS = 6
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

FRAME_BRIGHTNESS_THRESHOLD = 24
FRAME_MATCH_THRESHOLD = 36


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Process original TriCards images for inspection."
	)
	parser.add_argument(
		"image",
		nargs="?",
		help="Optional image filename inside the source directory. If omitted, process every card in data.json.",
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
		help="Pack data JSON used to infer card type from image filename.",
	)
	parser.add_argument(
		"--card-type",
		choices=sorted(FRAME_FILES),
		help="Override the inferred card type.",
	)
	parser.add_argument(
		"--output-dir",
		type=Path,
		default=DEFAULT_OUTPUT_DIR,
		help="Directory where preview PNGs will be written.",
	)
	return parser.parse_args()


def load_card_entries(data_file: Path) -> list[dict[str, str]]:
	with data_file.open("r", encoding="latin-1") as handle:
		raw = json.load(handle)

	card_entries = []
	for entry in raw[2:]:
		if len(entry) != 9:
			continue
		card_entries.append(
			{
				"name": str(entry[1]).strip(),
				"type": str(entry[2]).strip(),
				"image": str(entry[8]).strip(),
			}
		)
	return card_entries


def get_card_entry_map(card_entries: list[dict[str, str]]) -> dict[str, dict[str, str]]:
	return {entry["image"]: entry for entry in card_entries}


def get_frame_mask(frame: Image.Image) -> list[bool]:
	pixels = []
	frame_pixels = frame.load()
	for y in range(frame.height):
		for x in range(frame.width):
			red, green, blue, alpha = frame_pixels[x, y]
			is_border = (
				alpha > 0 and max(red, green, blue) >= FRAME_BRIGHTNESS_THRESHOLD
			)
			pixels.append(is_border)
	return pixels


def color_distance(left: tuple[int, int, int], right: tuple[int, int, int]) -> int:
	return max(abs(left[0] - right[0]), abs(left[1] - right[1]), abs(left[2] - right[2]))


def mask_frame(card: Image.Image, frame: Image.Image) -> Image.Image:
	if card.size != frame.size:
		raise ValueError(
			f"Image/frame size mismatch: {card.size} vs {frame.size}."
		)

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


def quantize_preview(image: Image.Image) -> Image.Image:
	image = flatten_alpha(image)
	alpha = image.getchannel("A")
	bbox = alpha.getbbox()
	if bbox is None:
		return image

	opaque_crop = image.crop(bbox).convert("RGB")
	color_count = max(1, min(QUANTIZE_TO_COLORS, 255))
	dither = get_dither_mode()
	palette_source = opaque_crop.quantize(
		colors=color_count,
		kmeans=3,
        method=Image.Quantize.FASTOCTREE,
		dither=dither,
	)
	palette_image = Image.new("P", (1, 1))
	palette_image.putpalette(palette_source.getpalette())

	quantized_rgb = image.convert("RGB").quantize(
		palette=palette_image,
		dither=dither,
	).convert("RGBA")
	quantized_rgb.putalpha(alpha)
	return flatten_alpha(quantized_rgb)


def save_outputs(card_path: Path, card_type: str, masked: Image.Image, output_dir: Path) -> Path:
	crop_box = CROP_BOXES[card_type]
	cropped = masked.crop(crop_box)
	resized = resize_preview(cropped, TARGET_SIZE)
	quantized = quantize_preview(resized)

	output_dir.mkdir(parents=True, exist_ok=True)
	stem = card_path.stem
	masked.save(output_dir / f"{stem}-masked.png")
	cropped.save(output_dir / f"{stem}-cropped.png")
	resized.save(output_dir / f"{stem}-resized-52.png")
	preview_path = output_dir / f"{stem}-preview-52.png"
	quantized.save(preview_path)
	return preview_path


def process_card(
		card_path: Path,
		card_type: str,
		source_dir: Path,
		output_dir: Path,
		frame_cache: dict[str, Image.Image],
	) -> Path:
	if card_type not in FRAME_FILES:
		raise KeyError(
			f"Could not determine card type for {card_path.name}."
		)

	frame_path = source_dir / FRAME_FILES[card_type]
	if not frame_path.is_file():
		raise FileNotFoundError(f"Frame image not found: {frame_path}")

	if card_type not in frame_cache:
		frame_cache[card_type] = Image.open(frame_path).convert("RGBA")

	card = Image.open(card_path).convert("RGBA")
	masked = mask_frame(card, frame_cache[card_type])
	return save_outputs(card_path, card_type, masked, output_dir)


def build_contact_sheet(
		card_entries: list[dict[str, str]],
		preview_paths: list[Path],
		output_dir: Path,
		columns: int = 5,
	) -> Path:
	if not preview_paths:
		raise ValueError("No previews available for contact sheet generation.")

	label_height = 16
	tile_width, tile_height = TARGET_SIZE
	rows = math.ceil(len(preview_paths) / columns)
	sheet = Image.new(
		"RGBA",
		(columns * tile_width, rows * (tile_height + label_height)),
		(24, 24, 24, 255),
	)
	draw = ImageDraw.Draw(sheet)
	label_lookup = {entry["image"]: entry["name"] for entry in card_entries}

	for index, preview_path in enumerate(preview_paths):
		preview = Image.open(preview_path).convert("RGBA")
		col = index % columns
		row = index // columns
		x = col * tile_width
		y = row * (tile_height + label_height)
		sheet.paste(preview, (x, y), preview)
		label = preview_path.stem.replace("-preview-52", "")
		card_name = label_lookup.get(f"{label}.png", label)
		draw.text((x + 1, y + tile_height + 2), card_name[:8], fill=(220, 220, 220, 255))

	output_path = output_dir / "contact-sheet.png"
	sheet.save(output_path)
	return output_path


def main() -> int:
	args = parse_args()
	source_dir = args.source_dir.resolve()
	output_dir = args.output_dir.resolve()
	data_file = args.data_file.resolve()
	if not data_file.is_file():
		raise FileNotFoundError(f"Data file not found: {data_file}")

	card_entries = load_card_entries(data_file)
	card_entry_map = get_card_entry_map(card_entries)
	frame_cache: dict[str, Image.Image] = {}

	if args.image:
		image_name = Path(args.image).name
		card_path = source_dir / image_name
		if not card_path.is_file():
			raise FileNotFoundError(f"Card image not found: {card_path}")
		card_entry = card_entry_map.get(image_name)
		card_type = args.card_type or (card_entry["type"] if card_entry else None)
		if card_type not in FRAME_FILES:
			raise KeyError(
				f"Could not determine card type for {image_name}. Use --card-type to override."
			)
		preview_path = process_card(card_path, card_type, source_dir, output_dir, frame_cache)
		print(f"image: {card_path}")
		print(f"type: {card_type}")
		print(f"crop: {CROP_BOXES[card_type]}")
		print(f"masked: {output_dir / (card_path.stem + '-masked.png')}")
		print(f"cropped: {output_dir / (card_path.stem + '-cropped.png')}")
		print(f"preview: {preview_path}")
		return 0

	preview_paths = []
	for card_entry in card_entries:
		card_path = source_dir / card_entry["image"]
		if not card_path.is_file():
			raise FileNotFoundError(f"Card image not found: {card_path}")
		preview_path = process_card(
			card_path,
			card_entry["type"],
			source_dir,
			output_dir,
			frame_cache,
		)
		preview_paths.append(preview_path)

	contact_sheet = build_contact_sheet(card_entries, preview_paths, output_dir)
	print(f"processed: {len(preview_paths)} cards")
	print(f"output dir: {output_dir}")
	print(f"contact sheet: {contact_sheet}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())








