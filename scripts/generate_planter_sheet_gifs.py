#!/usr/bin/env python
"""Build compact animated plant GIFs from planter color sprite sheets.

The source sheets contain exact-size sprites on a black background. This script
detects those exact plant components, turns black transparent, upscales them for
the existing ESP32 sprite pipeline, and writes one animated GIF per plant into
assets/plant_gifs. By default each plant keeps 3 deterministic random color
frames so the ESP32 firmware stays small.

Example:
  python scripts/generate_planter_sheet_gifs.py
"""

from __future__ import annotations

import argparse
import random
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - user-facing dependency guard
    raise SystemExit("Pillow is required. Install with: python -m pip install Pillow") from exc


DEFAULT_SHEETS = [
    Path("assets/GREEN-BLUE-PURP-PLANTER.png"),
    Path("assets/YELL-ORG-RED-PLANTER.png"),
]

COLOR_ROWS = [
    ["green_light", "green_mid", "green_dark", "blue_light", "blue_mid", "blue_dark", "purple_light", "purple_mid", "purple_dark"],
    ["yellow_light", "yellow_mid", "yellow_dark", "orange_light", "orange_mid", "orange_dark", "red_light", "red_mid", "red_dark"],
]


@dataclass(frozen=True)
class PlantSpec:
    key: str
    source_name: str
    width: int
    height: int


PLANTS = [
    PlantSpec("color_vine_pot", "money_tree", 20, 54),
    PlantSpec("color_grass_planter", "bamboo", 28, 62),
    PlantSpec("color_round_pot", "money_plant", 22, 33),
    PlantSpec("color_bonsai_bowl", "juniper_bonsai", 25, 32),
    PlantSpec("color_zigzag_pot", "elm_bonsai", 32, 37),
    PlantSpec("color_hanging_vine", "long_vined_pothos", 34, 65),
    PlantSpec("color_hanging_clump", "short_vined_pothos", 33, 46),
]


def is_foreground(image: Image.Image, x: int, y: int, threshold: int) -> bool:
    r, g, b, a = image.getpixel((x, y))
    return bool(a and (r > threshold or g > threshold or b > threshold))


def transparent_black(image: Image.Image, threshold: int) -> Image.Image:
    frame = image.convert("RGBA")
    pixels = frame.load()
    for y in range(frame.height):
        for x in range(frame.width):
            r, g, b, a = pixels[x, y]
            if a and r <= threshold and g <= threshold and b <= threshold:
                pixels[x, y] = (0, 0, 0, 0)
    return frame


def detect_components(image: Image.Image, *, threshold: int, min_pixels: int) -> list[tuple[int, int, int, int, int]]:
    width, height = image.size
    seen: set[tuple[int, int]] = set()
    components: list[tuple[int, int, int, int, int]] = []

    for y in range(height):
        for x in range(width):
            if (x, y) in seen or not is_foreground(image, x, y, threshold):
                continue

            stack = [(x, y)]
            seen.add((x, y))
            left = right = x
            top = bottom = y
            pixels = 0

            while stack:
                cx, cy = stack.pop()
                pixels += 1
                left = min(left, cx)
                right = max(right, cx)
                top = min(top, cy)
                bottom = max(bottom, cy)

                for nx in (cx - 1, cx, cx + 1):
                    for ny in (cy - 1, cy, cy + 1):
                        if nx < 0 or nx >= width or ny < 0 or ny >= height or (nx, ny) in seen:
                            continue
                        if is_foreground(image, nx, ny, threshold):
                            seen.add((nx, ny))
                            stack.append((nx, ny))

            if pixels >= min_pixels:
                components.append((left, top, right + 1, bottom + 1, pixels))

    return components


def load_sheet_frames(
    sheet_paths: list[Path],
    *,
    black_threshold: int,
    min_pixels: int,
) -> dict[str, list[tuple[str, Image.Image]]]:
    frames_by_key: dict[str, list[tuple[str, Image.Image]]] = {plant.key: [] for plant in PLANTS}
    plants_by_size = {(plant.width, plant.height): plant for plant in PLANTS}

    for sheet_index, sheet_path in enumerate(sheet_paths):
        sheet = Image.open(sheet_path).convert("RGBA")
        color_labels = COLOR_ROWS[sheet_index] if sheet_index < len(COLOR_ROWS) else []
        matches: dict[str, list[tuple[int, int, int, int]]] = {plant.key: [] for plant in PLANTS}

        for left, top, right, bottom, _ in detect_components(sheet, threshold=black_threshold, min_pixels=min_pixels):
            plant = plants_by_size.get((right - left, bottom - top))
            if plant is not None:
                matches[plant.key].append((left, top, right, bottom))

        for plant in PLANTS:
            boxes = sorted(matches[plant.key], key=lambda box: (box[1], box[0]))
            if len(boxes) != len(color_labels):
                raise ValueError(
                    f"{sheet_path} produced {len(boxes)} {plant.source_name} frames; "
                    f"expected {len(color_labels)}"
                )
            for label, box in zip(color_labels, boxes):
                frames_by_key[plant.key].append((label, transparent_black(sheet.crop(box), black_threshold)))

    return frames_by_key


def choose_random_frames(
    frames: list[tuple[str, Image.Image]],
    *,
    count: int,
    rng: random.Random,
) -> list[tuple[str, Image.Image]]:
    if count <= 0 or count >= len(frames):
        return frames
    indexes = sorted(rng.sample(range(len(frames)), count))
    return [frames[index] for index in indexes]


def upscale_frames(frames: list[Image.Image], factor: int) -> list[Image.Image]:
    if factor <= 1:
        return frames
    return [
        frame.resize((frame.width * factor, frame.height * factor), Image.Resampling.NEAREST)
        for frame in frames
    ]


MAGENTA_RGB = (255, 0, 255)


def frame_to_gif_palette(frame: Image.Image) -> Image.Image:
    transparent = Image.new("RGBA", frame.size, MAGENTA_RGB + (0,))
    transparent.alpha_composite(frame)
    return transparent.convert("P", palette=Image.Palette.ADAPTIVE, colors=255)


def magenta_palette_index(frame: Image.Image) -> int:
    # Adaptive quantization doesn't guarantee the placeholder color lands at
    # index 0, so find the palette entry actually closest to it.
    palette = frame.getpalette() or []
    best_index = 0
    best_distance = None
    for index in range(0, len(palette), 3):
        r, g, b = palette[index:index + 3]
        distance = (r - MAGENTA_RGB[0]) ** 2 + (g - MAGENTA_RGB[1]) ** 2 + (b - MAGENTA_RGB[2]) ** 2
        if best_distance is None or distance < best_distance:
            best_distance = distance
            best_index = index // 3
    return best_index


def write_gif(path: Path, frames: list[Image.Image], *, duration_ms: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    palette_frames = [frame_to_gif_palette(frame) for frame in frames]
    transparency_indexes = [magenta_palette_index(frame) for frame in palette_frames]
    for frame, transparency_index in zip(palette_frames, transparency_indexes):
        frame.info["transparency"] = transparency_index
        frame.info["disposal"] = 2
    palette_frames[0].save(
        path,
        save_all=True,
        append_images=palette_frames[1:],
        duration=duration_ms,
        loop=0,
        transparency=transparency_indexes[0],
        disposal=2,
        optimize=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate compact animated plant GIFs from planter color sprite sheets.")
    parser.add_argument("--sheets", type=Path, nargs="+", default=DEFAULT_SHEETS, help="Input sprite sheet PNGs, in animation order.")
    parser.add_argument("--out", type=Path, default=Path("assets/plant_gifs"), help="Output folder for generated GIFs.")
    parser.add_argument("--duration", type=int, default=260, help="Frame duration in milliseconds.")
    parser.add_argument("--frames-per-plant", type=int, default=3, help="Random color frames to keep per plant. Use 0 to keep all.")
    parser.add_argument("--upscale", type=int, default=4, help="Nearest-neighbor upscale applied before writing GIFs.")
    parser.add_argument("--seed", type=int, default=20260706, help="Seed for repeatable random color choices.")
    parser.add_argument("--black-threshold", type=int, default=8, help="RGB values at or below this become transparent.")
    parser.add_argument("--min-pixels", type=int, default=30, help="Ignore tiny disconnected sheet artifacts below this size.")
    args = parser.parse_args()

    missing = [path for path in args.sheets if not path.exists()]
    if missing:
        raise SystemExit("Missing sheet(s): " + ", ".join(str(path) for path in missing))

    frames_by_key = load_sheet_frames(args.sheets, black_threshold=args.black_threshold, min_pixels=args.min_pixels)
    rng = random.Random(args.seed)

    for plant in PLANTS:
        chosen = choose_random_frames(frames_by_key[plant.key], count=args.frames_per_plant, rng=rng)
        labels = [label for label, _ in chosen]
        frames = upscale_frames([frame for _, frame in chosen], args.upscale)
        out_path = args.out / f"{plant.key}.gif"
        write_gif(out_path, frames, duration_ms=args.duration)
        width, height = frames[0].size
        print(
            f"Wrote {out_path}: {width}x{height}, frames={len(frames)}, "
            f"source={plant.source_name}, colors={', '.join(labels)}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
