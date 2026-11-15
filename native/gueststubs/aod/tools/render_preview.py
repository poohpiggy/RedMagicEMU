#!/usr/bin/env python3
"""Offline AOD geometry approximation; not Android Canvas or a device fix."""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def positive_dp(dp, density):
    # Android's positive dimension pixel-size rounding.
    return int(dp * density / 160 + 0.5)


def render(background, foreground, highest_y, density, output):
    root = Path(__file__).resolve().parents[4]
    output = output.resolve()
    if output == root or root in output.parents:
        raise ValueError('output must be outside the repository')
    if output.exists():
        raise ValueError('output directory must not exist')
    if density <= 0:
        raise ValueError('density must be positive')
    with Image.open(background) as image:
        bg = image.convert('RGB')
    with Image.open(foreground) as image:
        if image.mode != 'RGBA':
            raise ValueError('foreground must be RGBA')
        fg = image.copy()
    width, height = bg.size
    if fg.size != bg.size or not 0 <= highest_y < height:
        raise ValueError('foreground size or highest point is invalid')
    distance = positive_dp(20, density)
    vertical = positive_dp(136, density)
    horizontal = positive_dp(120, density)
    corner = positive_dp(24, density)
    if not distance or not horizontal or vertical > width or vertical + distance > height:
        raise ValueError('density is incompatible with the input dimensions')

    # A new, exclusively created directory prevents overwriting prior results.
    output.mkdir()
    for name, length, radius in [('vertical', vertical, vertical / 2),
                                 ('horizontal_big', horizontal, horizontal / 2),
                                 ('horizontal_small', horizontal, corner)]:
        scale = length / width
        y = highest_y
        if (height - y) * scale < distance + length:
            y = int(height - (distance + length) / scale)
        dy = ((height - 2 * y) * scale) / 2 - distance - length / 2
        left = (width - length) // 2
        top = (height - length) // 2 - distance
        affine = (1 / scale, 0, (left - width / 2) / scale + width / 2,
                  0, 1 / scale, (top - height / 2 - dy) / scale + height / 2)
        size = (length, length + distance)
        b = bg.transform(size, Image.Transform.AFFINE, affine, resample=Image.Resampling.BILINEAR)
        f = fg.transform(size, Image.Transform.AFFINE, affine, resample=Image.Resampling.BILINEAR)
        q = 4
        mask = Image.new('L', (size[0] * q, size[1] * q), 0)
        draw = ImageDraw.Draw(mask)
        draw.rounded_rectangle((0, distance * q, length * q, (distance + length) * q),
                               radius=radius * q, fill=255)
        bottom_mask = mask.resize(size, Image.Resampling.LANCZOS)
        draw.rectangle((0, 0, length * q, (distance + radius) * q), fill=255)
        top_mask = mask.resize(size, Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', size, (0, 0, 0, 255))
        canvas.paste(b, (0, 0), bottom_mask)
        f.putalpha(ImageChops.multiply(f.getchannel('A'), top_mask))
        canvas.alpha_composite(f)
        canvas.convert('RGB').save(output / (name + '.jpg'), 'JPEG', quality=100, subsampling=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--background', required=True, type=Path)
    parser.add_argument('--foreground', required=True, type=Path)
    parser.add_argument('--highest-y', required=True, type=int)
    parser.add_argument('--density', required=True, type=int, help='target density in dpi')
    parser.add_argument('--output', required=True, type=Path, help='new directory outside the repository')
    args = parser.parse_args()
    try:
        render(args.background, args.foreground, args.highest_y, args.density, args.output)
    except (OSError, ValueError) as error:
        print(f'preview failed: {error}', file=sys.stderr)
        return 1
    print('Approximate offline preview only; not Android Canvas or a device fix.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
