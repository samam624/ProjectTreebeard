from __future__ import annotations

import unittest
from pathlib import Path

from PIL import Image

from generate_plant_sprites import alpha_components, choose_poses, fit_sprite_to_target, load_sprite, normalize_frames


def opaque_bbox(image: Image.Image) -> tuple[int, int, int, int] | None:
    return image.getchannel("A").getbbox()


class StableSpriteGeometryTests(unittest.TestCase):
    def test_shared_crop_preserves_motion_and_ignores_distant_speck(self) -> None:
        first = Image.new("RGBA", (24, 30), (0, 0, 0, 0))
        second = Image.new("RGBA", (24, 30), (0, 0, 0, 0))
        for y in range(3, 15):
            for x in range(4, 16):
                first.putpixel((x, y), (30, 160, 70, 255))
                second.putpixel((x + 2, y), (30, 160, 70, 255))
        second.putpixel((23, 29), (120, 70, 30, 255))

        normalized = normalize_frames([first, second], white_threshold=-1, scale=1.0)

        self.assertEqual([frame.size for frame in normalized], [(14, 12), (14, 12)])
        self.assertEqual(opaque_bbox(normalized[0]), (0, 0, 12, 12))
        self.assertEqual(opaque_bbox(normalized[1]), (2, 0, 14, 12))
        self.assertEqual(normalized[1].getpixel((13, 11))[3], 255)

    def test_large_detached_shape_remains_part_of_shared_crop(self) -> None:
        frame = Image.new("RGBA", (20, 20), (0, 0, 0, 0))
        for y in range(2, 12):
            for x in range(2, 12):
                frame.putpixel((x, y), (20, 120, 50, 255))
        for y in range(14, 17):
            for x in range(14, 17):
                frame.putpixel((x, y), (20, 120, 50, 255))

        [normalized] = normalize_frames([frame], white_threshold=-1, scale=1.0)

        self.assertEqual(normalized.size, (15, 15))
        self.assertEqual(opaque_bbox(normalized), (0, 0, 15, 15))

    def test_target_canvas_does_not_recenter_poses_independently(self) -> None:
        first = Image.new("RGBA", (8, 10), (0, 0, 0, 0))
        second = Image.new("RGBA", (8, 10), (0, 0, 0, 0))
        first.putpixel((1, 4), (30, 160, 70, 255))
        second.putpixel((3, 4), (30, 160, 70, 255))

        fitted = fit_sprite_to_target([first, second], target_width=12, target_height=12)

        self.assertEqual(opaque_bbox(fitted[0]), (3, 6, 4, 7))
        self.assertEqual(opaque_bbox(fitted[1]), (5, 6, 6, 7))

    def test_cactus_main_component_stays_on_the_same_baseline(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        sprite = load_sprite(
            repo_root / "assets" / "plant_gifs" / "cactus_cluster.gif",
            scale=0.5,
            max_poses=3,
            white_threshold=245,
            target_width=84,
            target_height=84,
        )

        main_boxes = [alpha_components(frame)[0][1] for frame in sprite.frames]
        self.assertEqual([box[3] for box in main_boxes], [84, 84, 84])
        self.assertEqual([len(alpha_components(frame)) for frame in sprite.frames], [1, 1, 1])
        self.assertEqual(sprite.durations_ms, [200, 200, 200])

    def test_default_zero_pose_limit_preserves_every_unique_frame(self) -> None:
        frames = []
        for x in range(4):
            frame = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
            frame.putpixel((x, 0), (30, 160, 70, 255))
            frames.append(frame)

        poses, sequence = choose_poses(frames, 0)

        self.assertEqual(len(poses), 4)
        self.assertEqual(sequence, [0, 1, 2, 3])


if __name__ == "__main__":
    unittest.main()
