#!/usr/bin/env python3
"""Verify native window pixels and paused DOM overlays, never metadata alone."""
import json, sys
from pathlib import Path
from PIL import Image, ImageChops, ImageStat
root=Path(sys.argv[1]); report=json.loads((root/'outcomes.json').read_text()); captures={c['name']:c for c in report['captures']}; checks=[]
def check(name,passed,**detail):
    checks.append(dict(name=name,passed=bool(passed),**detail)); print(('PASS' if passed else 'FAIL'),name,detail)
def image(name): return Image.open(root/(name+'.png')).convert('RGB')
def crop(name):
    item=captures[name]; n=item['native']; d=item['dpr']
    return image(name).crop(tuple(map(int,((n['x']+n['width']*.35)*d,(n['y']+n['height']*.24)*d,(n['x']+n['width']*.65)*d,(n['y']+n['height']*.56)*d))))
def variation(name):
    if name not in captures: check(name+' present',False); return
    im=crop(name); s=ImageStat.Stat(im); colors=len(im.quantize(colors=128).getcolors() or [])
    check(name+' contains varied visible video',max(s.stddev)>18 and colors>20,stddev=s.stddev,colors=colors)
def difference(a,b,region=None):
    ia,ib=image(a),image(b)
    if region: ia,ib=ia.crop(region),ib.crop(region)
    return max(ImageStat.Stat(ImageChops.difference(ia,ib)).mean)
def domRegion(name,key):
    c=captures[name]; r=c['html'][key]; scale=c['dpr']*c['native']['zoom']
    return tuple(map(int,(r['x']*scale,r['y']*scale,(r['x']+r['width'])*scale,(r['y']+r['height'])*scale)))
for name in ['h264-before-native','native-only','decoded-frame-a','decoded-frame-b','paused-menu','paused-caption-off','paused-caption-on','resized','scrolled','zoomed','fullscreen','ffv1-before-native','ffv1-native-only','ffv1-decoded','document-reopen-before-native','document-reopen-native-only','document-reopen']:
    variation(name)
if all(n in captures for n in ['decoded-frame-a','decoded-frame-b']):
    delta=ImageStat.Stat(ImageChops.difference(crop('decoded-frame-a'),crop('decoded-frame-b')))
    check('two genuinely different displayed frames',max(delta.mean)>3,meanAbsoluteDifference=delta.mean)
if all(n in captures for n in ['decoded-frame-b','paused-menu','paused-caption-off','paused-caption-on']):
    delta=difference('decoded-frame-b','paused-menu',domRegion('paused-menu','menu'))
    check('paused menu changes actual overlay pixels',delta>5,meanAbsoluteDifference=delta)
    region=domRegion('paused-caption-on','caption')
    delta=difference('paused-caption-off','paused-caption-on',region)
    check('paused caption toggles actual overlay pixels',delta>5,meanAbsoluteDifference=delta)
    restored=difference('paused-menu','paused-caption-on',region)
    check('paused caption restores original pixels',restored<1,meanAbsoluteDifference=restored)
    a=crop('decoded-frame-b')
    for name in ['paused-menu','paused-caption-off','paused-caption-on']:
        delta=max(ImageStat.Stat(ImageChops.difference(a,crop(name))).mean)
        check(name+' leaves unobstructed paused video unchanged',delta<1,meanAbsoluteDifference=delta)
for name in ['resized','scrolled','zoomed','fullscreen']:
    if name not in captures: continue
    c=captures[name]; n=c['native']; r=c['html']['aperture']; z=n['zoom']
    error=max(abs(n[k]-r[k]*z) for k in ['x','y','width','height'])
    check(name+' DOM/native rectangles match',error<2,maxCoordinateError=error)
(root/'pixel-checks.json').write_text(json.dumps({'passed':bool(report['passed']) and all(x['passed'] for x in checks),'nativeAssertionsPassed':bool(report['passed']),'pixelsPassed':all(x['passed'] for x in checks),'checks':checks},indent=2)+'\n')
sys.exit(0 if report['passed'] and all(x['passed'] for x in checks) else 1)
