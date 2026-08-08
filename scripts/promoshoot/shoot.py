# Colosseum promo capture rig — drives the app over the Lanista pipe while ffmpeg
# records the screen. One function per clip; run `python shoot.py hero theatre tanko`.
#
# Prereqs: the capture instance is running (COLOSSEUM_APPDATA_TAG=cwprobe,
# COLOSSEUM_LANISTA_PIPE=LanCWProbe, COLOSSEUM_LANISTA_DRIVE=1), freshly launched so it
# owns the foreground, machine otherwise idle. Registry stores are SHARED with the real
# app: no resume clicks, no wallpaper switching, no settings edits in any choreography.
import json, time, subprocess, sys, os, ctypes

PIPE = '//./pipe/LanCWProbe'
FF = r'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe'
OUT = r'C:\Users\Suprabha\Desktop\Colosseum-clips'

_seq = [0]
def lan(cmd, payload=None):
    _seq[0] += 1
    with open(PIPE, 'r+b', buffering=0) as p:
        req = {'cmd': cmd, 'seq': _seq[0]}
        if payload: req['payload'] = payload
        p.write((json.dumps(req) + '\n').encode())
        r = json.loads(p.readline().decode())
    if r.get('type') == 'error':
        raise RuntimeError(f"{cmd}: {r.get('code')} {r.get('message')}")
    return r

def snapshot():
    return lan('ui-snapshot')['elements']

def board():
    # the world/home page's vertical scroller: the largest root Flickable
    els = [e for e in snapshot() if e['class'] == 'QQuickFlickable']
    return max(els, key=lambda e: e['width'] * e['height'])['handle']

def pill(label_x):
    # world pills live in the top bar around y=87; pick by centerX proximity
    els = [e for e in snapshot() if 'MouseArea' in e['class'] and 60 < e['centerY'] < 115
           and 30 < e['width'] < 160]
    els.sort(key=lambda e: abs(e['centerX'] - label_x))
    return els[0]['handle']

PILL_X = {'tankoban': 759, 'biblio': 886, 'theatre': 1004}

def scroll(handle, dy, n=1, gap=0.55):
    for _ in range(n):
        lan('ui-scroll', {'target': handle, 'dy': dy})
        time.sleep(gap)

def park_cursor():
    # off every hoverable surface (right screen edge, mid-height)
    ctypes.windll.user32.SetCursorPos(1918, 540)

def record(name, seconds, actions):
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, name + '.mp4')
    park_cursor()
    ff = subprocess.Popen([FF, '-y', '-loglevel', 'error',
        '-filter_complex', 'ddagrab=framerate=60:draw_mouse=0,hwdownload,format=bgra',
        '-t', str(seconds), '-c:v', 'libx264', '-crf', '20', '-preset', 'veryfast',
        '-pix_fmt', 'yuv420p', '-movflags', '+faststart', out])
    time.sleep(1.2)          # capture rolling before the first move
    actions()
    ff.wait(timeout=seconds + 60)
    print('wrote', out)
    return out

def to_top(handle):
    scroll(handle, 700, n=12, gap=0.12)
    time.sleep(1.0)

# ── clips ─────────────────────────────────────────────────────────────────────
# Every clip: warm pass first (loads + caches poster art so the take has no pop-in),
# slam back to top, then record.

def clip_hero():
    # Home top → one continuous slow descent through the whole board
    b = board()
    scroll(b, -450, n=10, gap=0.3); to_top(b)      # warm the art
    def go():
        time.sleep(1.6)                      # hold the hero top
        scroll(b, -300, n=5, gap=0.7)        # Continue row → bookshelf
        time.sleep(1.0)
        scroll(b, -300, n=5, gap=0.7)        # Theatre strip
        time.sleep(1.0)
        scroll(b, -300, n=5, gap=0.7)        # Biblio desk
        time.sleep(1.2)
    record('01-hero-home', 19, go)

def clip_theatre():
    # requires the instance booted with COLOSSEUM_OPEN_WORLD=Theatre
    # (the world pills are TapHandlers — invisible to ui-snapshot, unclickable by handle)
    b = board()
    scroll(b, -450, n=12, gap=0.3); to_top(b)      # warm the art
    def go():
        time.sleep(2.6)                      # hold marquee + Continue
        scroll(b, -320, n=4, gap=1.15)       # linger through the showcase shelves
        time.sleep(1.4)
        scroll(b, -320, n=4, gap=0.8)        # ease into the Popular grid
        time.sleep(1.5)
    record('02-theatre-discover', 18, go)

def clip_tanko():
    # requires the instance booted with COLOSSEUM_OPEN_WORLD=Tankoban
    b = board()
    scroll(b, -450, n=12, gap=0.3); to_top(b)      # warm the art
    def go():
        time.sleep(2.4)
        scroll(b, -320, n=4, gap=1.15)       # linger through the Discover shelves
        time.sleep(1.4)
        scroll(b, -320, n=4, gap=0.8)        # ease into the manga wall
        time.sleep(1.5)
    record('05-tankoban-discover', 18, go)

CLIPS = {'hero': clip_hero, 'theatre': clip_theatre, 'tanko': clip_tanko}

if __name__ == '__main__':
    for name in (sys.argv[1:] or ['hero']):
        CLIPS[name]()
