#!/usr/bin/env python3
"""Real-window pixel acceptance tests. Never substitute DOM/API checks for pixels."""
import json, sys
from pathlib import Path
from PIL import Image

path = Path(sys.argv[1] if len(sys.argv) > 1 else 'evidence')
checks = []
negative_as_transparent = '--negative-as-transparent' in sys.argv[2:]
def check(name, value, detail):
    checks.append(bool(value))
    print(f"{'PASS' if value else 'FAIL'} {name}: {detail}")

def color_near(a, b, tolerance=3):
    return max(abs(x-y) for x,y in zip(a[:3], b)) <= tolerance

manifest = path / 'snapshots.json'
if not manifest.exists():
    check('real window captures exist', False, str(manifest))
    sys.exit(1)
frames = {f['name']: f for f in json.loads(manifest.read_text())}
first = 'initial-transparent' if 'initial-transparent' in frames else 'opaque'
expected = [first, 'transparent', 'menu', 'caption-off', 'caption-on', 'resized', 'scrolled', 'zoomed']
for name in expected:
    check(name+' capture exists', name in frames and (path/(name+'.png')).exists(), name)
    if name not in frames or not (path/(name+'.png')).exists():
        continue
    frame = frames[name]
    img = Image.open(path/(name+'.png')).convert('RGB')
    scale = img.width / frame['windowWidth']
    zoom = frame['zoom']
    check(name+' exposed OpenGL window', frame['exposed'] and frame['graphicsApi']==3, f"exposed={frame['exposed']}, graphicsApi={frame['graphicsApi']}")
    check(name+' real window image size', img.height == round(frame['windowHeight']*scale), str(img.size))
    rect = frame['dom']['aperture']
    points = []
    # Sample the left half of the opening, avoiding all HTML overlay rectangles.
    for fy in (.137,.223,.319,.413,.487):
        for fx in (.071,.153,.237,.323,.407):
            x = int((rect['x']+fx*rect['width'])*zoom)
            y = int((rect['y']+fy*rect['height'])*zoom)
            if x < 0 or y < 0 or x >= frame['windowWidth'] or y >= frame['windowHeight']:
                continue
            # Skip exact native checker borders where subpixel rasterization can differ.
            if x%40 < 2 or y%40 < 2 or x%40 > 37 or y%40 > 37:
                continue
            actual = img.getpixel((int(x*scale),int(y*scale)))
            native = (25,184,173) if (x//40+y//40)%2 == 0 else (245,186,59)
            points.append((actual,native))
    hits = sum(color_near(a,b) for a,b in points)
    check(name+' underlay pixels', bool(points) and (hits == 0 if name=='opaque' and not negative_as_transparent else hits==len(points)), f'{hits}/{len(points)} match native checkerboard')
    if name=='opaque':
        check('opaque body negative control', all(color_near(a,(24,37,56)) for a,_ in points), 'all aperture samples must be opaque body color')
    # Overlay solids are actual HTML pixels, with an interior corner avoiding glyphs.
    for label, wanted, visible in [('caption',(7,16,29), name!='caption-off'), ('controls',(34,68,102),True), ('menu',(67,48,112),name in ['menu','caption-off','caption-on','resized','scrolled','zoomed'])]:
        box = frame['dom'][label]
        check(name+' '+label+' visibility', box['visible']==visible, str(box['visible']))
        if visible:
            x,y = (box['x']+5)*zoom, (box['y']+5)*zoom
            if 0 <= x < frame['windowWidth'] and 0 <= y < frame['windowHeight']:
                actual = img.getpixel((int(x*scale),int(y*scale)))
                check(name+' '+label+' composed pixels', color_near(actual,wanted), f'{actual} expected {wanted}')
            else:
                check(name+' '+label+' inside viewport',False, str((x,y)))
check('native click opened HTML menu', frames.get('menu',{}).get('dom',{}).get('menuClicks')==1, 'menuClicks must equal 1')
check('native clicks toggled HTML captions twice', frames.get('caption-on',{}).get('dom',{}).get('captionClicks')==2, 'captionClicks must equal 2')
check('resize occurred', frames.get('resized',{}).get('windowWidth')==1000, '1000 logical pixels')
check('scroll occurred', frames.get('scrolled',{}).get('dom',{}).get('scrollY')==60, '60 CSS pixels')
check('zoom occurred', frames.get('zoomed',{}).get('zoom')==1.25, '1.25 WebEngine zoomFactor')
print(f"RESULT: {sum(checks)}/{len(checks)} checks passed")
sys.exit(0 if all(checks) else 1)
