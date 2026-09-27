"""Eyes-only PNG heatmap. Pillow is optional; failure never affects parity."""
import sys
from PIL import Image, ImageChops, ImageEnhance, ImageStat

qml, web, out = sys.argv[1:4]
a = Image.open(qml).convert('RGB')
b = Image.open(web).convert('RGB')
if a.size != b.size:
    raise ValueError(f'image sizes differ: {a.size} / {b.size}')
diff = ImageChops.difference(a, b)
score = sum(ImageStat.Stat(diff).mean) / 3 / 255
ImageEnhance.Contrast(diff).enhance(3).save(out)
print(f'{score:.6f}')
