from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
PAGE_DIR = ROOT / "tests/lanista_capture/seeds/tankoban-reader/manga/journey-manga-series-v1/ch-1"
PAGE_DIR.mkdir(parents=True, exist_ok=True)

try:
    title_font = ImageFont.truetype(r"C:\Windows\Fonts\georgiab.ttf", 72)
    body_font = ImageFont.truetype(r"C:\Windows\Fonts\segoeui.ttf", 34)
    small_font = ImageFont.truetype(r"C:\Windows\Fonts\segoeui.ttf", 28)
except OSError:
    title_font = body_font = small_font = ImageFont.load_default()

def panel(draw, box, shade, heading, copy):
    draw.rounded_rectangle(box, radius=26, fill=shade, outline=(35, 35, 39), width=7)
    x1, y1, x2, y2 = box
    draw.text((x1 + 38, y1 + 34), heading, font=body_font, fill=(20, 20, 23))
    draw.multiline_text((x1 + 38, y1 + 100), copy, font=small_font,
                        fill=(48, 48, 52), spacing=12)
def make_page(index):
    img = Image.new("RGB", (1200, 1800), (238, 235, 226))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, 1200, 150), fill=(18, 18, 21))
    d.text((54, 36), "JOURNEY MANGA", font=title_font, fill=(244, 240, 230))
    d.text((930, 64), f"PAGE {index + 1}", font=small_font, fill=(195, 190, 180))
    if index == 0:
        panel(d, (55, 205, 1145, 760), (206, 202, 191), "CHAPTER 1", "The station lights come on.\nA train is already waiting.")
        panel(d, (55, 810, 555, 1680), (222, 218, 208), "PLATFORM", "No announcements.\nNo crowd.\nJust one open door.")
        panel(d, (605, 810, 1145, 1680), (190, 187, 178), "DEPARTURE", "The clock hits midnight.\nThe doors close.")
    elif index == 1:
        panel(d, (55, 205, 560, 835), (218, 214, 204), "CAR 07", "Empty seats run\nto the far end.")
        panel(d, (610, 205, 1145, 835), (198, 195, 186), "WINDOW", "City lights turn\ninto long white lines.")
        panel(d, (55, 885, 1145, 1680), (228, 224, 214), "NEXT STOP", "The map above the door\nshows a station that isn't there.")
    else:
        panel(d, (55, 205, 1145, 700), (200, 197, 188), "THE TUNNEL", "The train slows.\nOutside, there is only black.")
        panel(d, (55, 750, 560, 1680), (226, 222, 212), "DOOR", "A chime sounds.\nThe lock clicks.")
        panel(d, (610, 750, 1145, 1680), (211, 207, 197), "ARRIVAL", "Someone is standing\non the platform.")
    path = PAGE_DIR / f"page_{index:03d}.png"
    img.save(path, optimize=True)
    return path.stat().st_size
if __name__ == "__main__":
    sizes = [make_page(i) for i in range(3)]
    index_path = ROOT / "tests/lanista_capture/seeds/tankoban-reader/manga/index.json"
    import json
    payload = json.loads(index_path.read_text(encoding="utf-8"))
    payload["entries"]["journey-manga-ch-1"]["bytes"] = sum(sizes)
    index_path.write_text(json.dumps(payload, indent=4) + "\n", encoding="utf-8")
    print("generated manga pages:", sizes, "total", sum(sizes))
