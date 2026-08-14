#!/usr/bin/env python
"""Generate ESP32 plant sprite assets from source GIFs.

Source GIFs live in assets/plant_gifs. The generator writes:
  - arduino/MSV2_ESP32_Display_Runtime/plant_sprites.h
  - arduino/MSV2_ESP32_Display_Runtime/PlantSpriteCatalog.h
  - arduino/MSV2_ESP32_Display_Runtime/MobileAppPage.h art dropdown/previews
  - generated/plant_sprites/previews/*.png
  - generated/plant_sprites/mobile_art_catalog_snippet.js

Install dependency:
  python -m pip install Pillow

Example:
  python scripts/generate_plant_sprites.py
  python scripts/generate_plant_sprites.py --scale 0.5 --max-poses 3
  python scripts/generate_plant_sprites.py --target-width 84 --target-height 84
"""

from __future__ import annotations

import argparse
import base64
import re
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageSequence
except ImportError as exc:  # pragma: no cover - user-facing dependency guard
    raise SystemExit("Pillow is required. Install with: python -m pip install Pillow") from exc


TRANSPARENT_RGB565 = 0xF81F
MOBILE_ART_BEGIN = "    // MSV2_ART_CATALOG_BEGIN"
MOBILE_ART_END = "    // MSV2_ART_CATALOG_END"


@dataclass
class Sprite:
    key: str
    label: str
    frames: list[Image.Image]
    sequence: list[int]


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-zA-Z0-9]+", "_", value.strip().lower()).strip("_")
    slug = slug or "plant"
    if slug[0].isdigit():
        slug = "_" + slug
    return slug


def label_from_key(key: str) -> str:
    return key.replace("_", " ").title()


def c_ident(key: str) -> str:
    return re.sub(r"[^A-Z0-9_]", "_", key.upper())


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def trim_transparent(image: Image.Image) -> Image.Image:
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if not bbox:
        return image
    return image.crop(bbox)


def normalize_frame(image: Image.Image, *, white_threshold: int, scale: float) -> Image.Image:
    frame = image.convert("RGBA")
    if white_threshold >= 0:
        pixels = frame.load()
        for y in range(frame.height):
            for x in range(frame.width):
                r, g, b, a = pixels[x, y]
                if a and r >= white_threshold and g >= white_threshold and b >= white_threshold:
                    pixels[x, y] = (255, 0, 255, 0)

    frame = trim_transparent(frame)
    if scale != 1.0:
        width = max(1, round(frame.width * scale))
        height = max(1, round(frame.height * scale))
        frame = frame.resize((width, height), Image.Resampling.NEAREST)
    return frame


def image_signature(image: Image.Image) -> bytes:
    return image.tobytes()


def fit_to_canvas(frames: list[Image.Image], width: int | None = None, height: int | None = None) -> list[Image.Image]:
    width = width or max(frame.width for frame in frames)
    height = height or max(frame.height for frame in frames)
    fitted = []
    for frame in frames:
        canvas = Image.new("RGBA", (width, height), (255, 0, 255, 0))
        canvas.alpha_composite(frame, ((width - frame.width) // 2, height - frame.height))
        fitted.append(canvas)
    return fitted


def fit_sprite_to_target(frames: list[Image.Image], *, target_width: int, target_height: int) -> list[Image.Image]:
    if target_width <= 0 or target_height <= 0:
        return frames

    width = max(frame.width for frame in frames)
    height = max(frame.height for frame in frames)
    scale = min(target_width / width, target_height / height, 1.0)
    fitted = []
    for frame in frames:
        if scale < 1.0:
            frame = frame.resize(
                (max(1, round(frame.width * scale)), max(1, round(frame.height * scale))),
                Image.Resampling.NEAREST,
            )
        fitted.extend(fit_to_canvas([frame], target_width, target_height))
    return fitted


def choose_poses(frames: list[Image.Image], max_poses: int) -> tuple[list[Image.Image], list[int]]:
    unique: list[Image.Image] = []
    unique_by_sig: dict[bytes, int] = {}
    original_sequence: list[int] = []

    for frame in frames:
        sig = image_signature(frame)
        if sig not in unique_by_sig:
            unique_by_sig[sig] = len(unique)
            unique.append(frame)
        original_sequence.append(unique_by_sig[sig])

    if len(unique) <= max_poses:
        return unique, original_sequence

    if max_poses == 1:
        selected_indexes = [(len(unique) - 1) // 2]
        remap = {old: 0 for old in range(len(unique))}
        return [unique[selected_indexes[0]]], [remap[index] for index in original_sequence]

    selected_indexes = sorted({round(i * (len(unique) - 1) / (max_poses - 1)) for i in range(max_poses)})
    selected = [unique[i] for i in selected_indexes]
    remap = {old: min(range(len(selected_indexes)), key=lambda idx: abs(selected_indexes[idx] - old)) for old in range(len(unique))}
    return selected, [remap[index] for index in original_sequence]


def load_sprite(path: Path, *, scale: float, max_poses: int, white_threshold: int, target_width: int, target_height: int) -> Sprite:
    key = slugify(path.stem)
    raw_frames = [
        normalize_frame(frame, white_threshold=white_threshold, scale=scale)
        for frame in ImageSequence.Iterator(Image.open(path))
    ]
    raw_frames = [frame for frame in raw_frames if frame.width and frame.height]
    if not raw_frames:
        raise ValueError(f"{path} did not contain usable frames")

    fitted = fit_sprite_to_target(
        fit_to_canvas(raw_frames),
        target_width=target_width,
        target_height=target_height,
    )
    poses, sequence = choose_poses(fitted, max_poses)
    return Sprite(key=key, label=label_from_key(key), frames=poses, sequence=sequence)


def frame_to_rgb565_values(frame: Image.Image) -> list[int]:
    values = []
    pixels = frame.get_flattened_data() if hasattr(frame, "get_flattened_data") else frame.getdata()
    for r, g, b, a in pixels:
        if a < 128:
            values.append(TRANSPARENT_RGB565)
        else:
            values.append(rgb565(r, g, b))
    return values


def write_values(lines: list[str], values: list[int], *, per_line: int = 12) -> None:
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        lines.append("  " + ", ".join(f"0x{value:04X}" for value in chunk) + ",")


def write_sprite_header(path: Path, sprites: list[Sprite]) -> None:
    lines = [
        "// Auto-generated by scripts/generate_plant_sprites.py.",
        "// Source GIFs: assets/plant_gifs/*.gif",
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "#define PLANT_TRANSPARENT 0xF81F",
        "",
        "struct PlantSprite {",
        "  uint16_t w;",
        "  uint16_t h;",
        "  uint8_t frameCount;",
        "  const uint16_t * const *frames;",
        "  const uint8_t *sequence;",
        "};",
        "",
    ]

    for sprite in sprites:
        for index, frame in enumerate(sprite.frames):
            lines.append(f"const uint16_t {sprite.key}_frame_{index}[] PROGMEM = {{")
            write_values(lines, frame_to_rgb565_values(frame))
            lines.append("};")
            lines.append("")

        lines.append(f"const uint16_t* const {sprite.key}_frames[] PROGMEM = {{")
        for index in range(len(sprite.frames)):
            lines.append(f"  {sprite.key}_frame_{index},")
        lines.append("};")
        lines.append("")

        sequence = ", ".join(str(value) for value in sprite.sequence)
        lines.append(f"const uint8_t {sprite.key}_sequence[] PROGMEM = {{ {sequence} }};")
        lines.append("")

        ident = c_ident(sprite.key)
        width, height = sprite.frames[0].size
        lines.append(
            f"const PlantSprite SPRITE_{ident} = {{ {width}, {height}, "
            f"{len(sprite.sequence)}, {sprite.key}_frames, {sprite.key}_sequence }};"
        )
        lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def write_catalog_header(path: Path, sprites: list[Sprite]) -> None:
    lines = [
        "// Auto-generated by scripts/generate_plant_sprites.py.",
        "#pragma once",
        "#include <string.h>",
        '#include "plant_sprites.h"',
        "",
        "struct PlantSpriteCatalogEntry",
        "{",
        "  const char* key;",
        "  const char* label;",
        "  const PlantSprite* sprite;",
        "};",
        "",
        "const PlantSpriteCatalogEntry PLANT_SPRITE_CATALOG[] = {",
    ]
    for sprite in sprites:
        lines.append(f'  {{ "{sprite.key}", "{sprite.label}", &SPRITE_{c_ident(sprite.key)} }},')
    lines.extend(
        [
            "};",
            "",
            "const size_t PLANT_SPRITE_CATALOG_COUNT = sizeof(PLANT_SPRITE_CATALOG) / sizeof(PLANT_SPRITE_CATALOG[0]);",
            "",
            "const PlantSprite* findPlantSpriteByKey(const char* key)",
            "{",
            "  if (key == nullptr)",
            "  {",
            f"    return &SPRITE_{c_ident(sprites[0].key)};",
            "  }",
            "",
            "  for (size_t i = 0; i < PLANT_SPRITE_CATALOG_COUNT; i++)",
            "  {",
            "    if (strcmp(key, PLANT_SPRITE_CATALOG[i].key) == 0)",
            "    {",
            "      return PLANT_SPRITE_CATALOG[i].sprite;",
            "    }",
            "  }",
            "",
            "  if (strcmp(key, \"sprout\") == 0)",
            "  {",
            "    return findPlantSpriteByKey(\"sprout_pot\");",
            "  }",
            "  if (strcmp(key, \"leafy\") == 0)",
            "  {",
            "    return findPlantSpriteByKey(\"cup_plant\");",
            "  }",
            "  if (strcmp(key, \"tall\") == 0)",
            "  {",
            "    return findPlantSpriteByKey(\"wide_planter\");",
            "  }",
            "",
            "  return nullptr;",
            "}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def build_mobile_art_catalog(sprites: list[Sprite], art_images: dict[str, str], *, indent: str = "") -> str:
    lines = [
        f"{indent}const artOptions = [",
    ]
    for sprite in sprites:
        lines.append(f'{indent}  ["{sprite.key}", "{sprite.label}"],')
    lines.append(f"{indent}];")
    lines.append(f"{indent}const artImages = {{")
    for key, data_url in art_images.items():
        lines.append(f'{indent}  "{key}": "{data_url}",')
    lines.append(f"{indent}}};")
    return "\n".join(lines)


def write_previews_and_web_catalog(path: Path, preview_dir: Path, sprites: list[Sprite]) -> dict[str, str]:
    preview_dir.mkdir(parents=True, exist_ok=True)
    art_images: dict[str, str] = {}
    for sprite in sprites:
        preview_path = preview_dir / f"{sprite.key}.png"
        sprite.frames[0].save(preview_path)
        art_images[sprite.key] = "data:image/png;base64," + base64.b64encode(preview_path.read_bytes()).decode("ascii")

    catalog = build_mobile_art_catalog(sprites, art_images)
    lines = [
        "// Auto-generated by scripts/generate_plant_sprites.py.",
        "// Paste this block into MobileAppPage.h, replacing artOptions and artImages.",
        catalog,
    ]
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")
    return art_images


def update_mobile_app_catalog(path: Path, sprites: list[Sprite], art_images: dict[str, str]) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Mobile app page not found: {path}")

    content = path.read_text(encoding="utf-8")
    start = content.find(MOBILE_ART_BEGIN)
    end = content.find(MOBILE_ART_END)
    if start < 0 or end < 0 or end < start:
        raise ValueError(
            f"{path} is missing {MOBILE_ART_BEGIN.strip()} / {MOBILE_ART_END.strip()} markers"
        )

    end += len(MOBILE_ART_END)
    replacement = (
        MOBILE_ART_BEGIN
        + "\n"
        + build_mobile_art_catalog(sprites, art_images, indent="    ")
        + "\n"
        + MOBILE_ART_END
    )
    path.write_text(content[:start] + replacement + content[end:], encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate MSV2 plant sprite firmware/web assets from GIFs.")
    parser.add_argument("--source", type=Path, default=Path("assets/plant_gifs"), help="Folder containing source GIFs.")
    parser.add_argument("--out-sketch", type=Path, default=Path("arduino/MSV2_ESP32_Display_Runtime"), help="ESP32 display sketch folder.")
    parser.add_argument("--generated", type=Path, default=Path("generated/plant_sprites"), help="Generated preview/snippet output folder.")
    parser.add_argument("--scale", type=float, default=0.5, help="Nearest-neighbor scale applied to GIF frames.")
    parser.add_argument("--target-width", type=int, default=84, help="Transparent output canvas width for every sprite. Use 0 to keep per-sprite widths.")
    parser.add_argument("--target-height", type=int, default=84, help="Transparent output canvas height for every sprite. Use 0 to keep per-sprite heights.")
    parser.add_argument("--max-poses", type=int, default=3, help="Maximum unique bitmap poses per GIF.")
    parser.add_argument("--white-threshold", type=int, default=245, help="Make near-white pixels transparent. Use -1 to disable.")
    parser.add_argument("--skip-mobile-app", action="store_true", help="Do not update MobileAppPage.h art picker.")
    args = parser.parse_args()

    gif_paths = sorted(args.source.glob("*.gif"))
    if not gif_paths:
        raise SystemExit(f"No GIFs found in {args.source}. Add files like fern.gif or monstera.gif.")

    sprites = [
        load_sprite(
            path,
            scale=args.scale,
            max_poses=args.max_poses,
            white_threshold=args.white_threshold,
            target_width=args.target_width,
            target_height=args.target_height,
        )
        for path in gif_paths
    ]

    args.out_sketch.mkdir(parents=True, exist_ok=True)
    args.generated.mkdir(parents=True, exist_ok=True)

    write_sprite_header(args.out_sketch / "plant_sprites.h", sprites)
    write_catalog_header(args.out_sketch / "PlantSpriteCatalog.h", sprites)
    art_images = write_previews_and_web_catalog(
        args.generated / "mobile_art_catalog_snippet.js",
        args.generated / "previews",
        sprites,
    )
    if not args.skip_mobile_app:
        update_mobile_app_catalog(args.out_sketch / "MobileAppPage.h", sprites, art_images)

    print(f"Generated {len(sprites)} sprite(s):")
    for sprite in sprites:
        width, height = sprite.frames[0].size
        print(f"  {sprite.key}: {width}x{height}, poses={len(sprite.frames)}, sequence={len(sprite.sequence)}")
    print(f"Wrote {args.out_sketch / 'plant_sprites.h'}")
    print(f"Wrote {args.out_sketch / 'PlantSpriteCatalog.h'}")
    if not args.skip_mobile_app:
        print(f"Updated {args.out_sketch / 'MobileAppPage.h'}")
    print(f"Wrote {args.generated / 'mobile_art_catalog_snippet.js'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
