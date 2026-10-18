#!/usr/bin/env python3
"""Generate original geometric test images. Requires Pillow; no network inputs."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
NAMES = ['eminem-warehouse', 'eminem-hood', 'eminem-early', 'eminem-signature',
         'UCfM3zsQsOnfWNUppiycmBuw-hero', 'UCVlWr_LN9y80smEMr0KTBOA-hero']
COLORS = [(35, 101, 140), (145, 68, 94), (55, 135, 99), (158, 108, 35),
          (85, 66, 138), (38, 122, 131)]
for index, (name, color) in enumerate(zip(NAMES, COLORS)):
    image = Image.new('RGB', (480, 270), color)
    draw = ImageDraw.Draw(image)
    # Distinct asymmetric geometry makes rotation and focal cropping observable.
    draw.rectangle((12 + index * 17, 16, 95 + index * 17, 245), fill=(225, 206, 155))
    draw.ellipse((260, 30 + index * 9, 420, 190 + index * 9), fill=(25, 28, 38))
    draw.line((0, 240, 480, 100 + index * 13), fill=(240, 240, 230), width=9)
    draw.text((210, 238), 'GENERATED TEST ART ' + str(index + 1), fill=(255, 255, 255))
    path = ROOT / 'artist-media' / (name + '.jpg')
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format='JPEG', quality=90, subsampling=0, optimize=False)
