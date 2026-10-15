"""Artist V4 QA: interaction tests + layout checks + final screenshots. Writes qa-results.json and screenshots/."""
import json, pathlib, sys
from playwright.sync_api import sync_playwright

ROOT = pathlib.Path(r"C:/Users/Suprabha/Desktop/Preflight-Architect/arcs/62-vinyl/prototypes/62-vinyl-artist-v4-claude")
SHOTS = ROOT / "screenshots"; SHOTS.mkdir(exist_ok=True)
URL = (ROOT / "index.html").as_uri()
results = {"checks": [], "layout": [], "screenshots": []}

def check(name, ok, detail=""):
    results["checks"].append({"check": name, "pass": bool(ok), "detail": detail})
    print(("PASS " if ok else "FAIL ") + name + (" :: " + str(detail) if detail else ""))

LAYOUT_JS = """() => {
  const page = document.getElementById('page');
  const bars = [];
  document.querySelectorAll('*').forEach(el => {
    const cs = getComputedStyle(el);
    const scrollY = /(auto|scroll)/.test(cs.overflowY), scrollX = /(auto|scroll)/.test(cs.overflowX);
    if (!scrollY && !scrollX) return;
    const vbar = el.offsetWidth - el.clientWidth - parseFloat(cs.borderLeftWidth) - parseFloat(cs.borderRightWidth);
    const hbar = el.offsetHeight - el.clientHeight - parseFloat(cs.borderTopWidth) - parseFloat(cs.borderBottomWidth);
    if (vbar > 0 || hbar > 0) bars.push((el.id || el.className) + ' v' + vbar + ' h' + hbar);
  });
  const scrollers = Array.from(document.querySelectorAll('*')).filter(el => {
    const cs = getComputedStyle(el); return /(auto|scroll)/.test(cs.overflowY) && el.scrollHeight > el.clientHeight + 1;
  }).map(el => el.id || el.className);
  const pr = page.getBoundingClientRect();
  const first = document.querySelector('.song');
  const fr = first ? first.getBoundingClientRect() : null;
  // overflow: anything sticking out of the viewport horizontally that is not inside a rail
  const out = [];
  document.querySelectorAll('#inner *').forEach(el => {
    if (el.closest('.rail')) return;
    const r = el.getBoundingClientRect();
    if (r.width && (r.right > innerWidth + 1 || r.left < -1)) out.push(el.className || el.tagName);
  });
  return {docScrollW: document.documentElement.scrollWidth, docClientW: document.documentElement.clientWidth,
          pageScrollW: page.scrollWidth, pageClientW: page.clientWidth, visibleScrollbars: bars,
          verticalScrollers: scrollers, firstSongTop: fr && Math.round(fr.top), firstSongBottom: fr && Math.round(fr.bottom),
          pageBottom: Math.round(pr.bottom), outOfViewport: out.slice(0, 8),
          popularInFirstViewport: !!fr && fr.bottom <= pr.bottom};
}"""

def open_page(p, w, h, slug):
    pg = p.new_page(viewport={"width": w, "height": h})
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.on("console", lambda m: errs.append(m.text) if m.type == "error" else None)
    pg.goto(URL + "#" + slug); pg.wait_for_load_state("networkidle"); pg.evaluate("document.fonts.ready")
    pg.wait_for_timeout(300)
    return pg, errs

def instant(pg):
    pg.add_style_tag(content="#page,.rail{scroll-behavior:auto!important}")

def scroll_to(pg, sel, offset=8):
    pg.evaluate("""([s, o]) => { const p=document.getElementById('page'); const el=document.querySelector(s);
      p.scrollTop += el.getBoundingClientRect().top - p.getBoundingClientRect().top - o; }""", [sel, offset])
    pg.wait_for_timeout(350)

def shot(pg, name):
    pg.wait_for_timeout(250)
    pg.screenshot(path=str(SHOTS / name)); results["screenshots"].append(name)

ALIGNED_JS = """id => { const r=document.getElementById(id), cs=getComputedStyle(r), left=r.getBoundingClientRect().left+parseFloat(cs.paddingLeft);
  const vis=Array.from(r.querySelectorAll('.item')).map(e=>e.getBoundingClientRect()).filter(b=>b.right>left+1);
  return vis.length ? Math.round(vis[0].left-left) : null; }"""

def focused(pg):
    return pg.evaluate("""() => { const a=document.activeElement; return {cls:a.className, id:a.dataset && (a.dataset.id||a.dataset.video||a.dataset.act||a.dataset.ract||''),
      label:(a.dataset && a.dataset.label) || (a.querySelector && a.querySelector('.song-title') ? a.querySelector('.song-title').textContent : a.textContent.trim().slice(0,40))}; }""")

with sync_playwright() as p:
    br = p.chromium.launch(channel="msedge", headless=True)
    data = None

    # ---------------- layout + screenshots per viewport ----------------
    plan = [
        (1600, 900, "eminem", [("first", None), ("discography", "#discography"), ("compilations", ".shelf:nth-of-type(3)"), ("videos-live", "#videos"), ("similar-end", "#similar")]),
        (1280, 720, "eminem", [("first", None), ("discography", "#discography"), ("videos", "#videos"), ("live-similar", "#live")]),
        (960, 720, "eminem", [("first", None), ("discography", "#discography"), ("videos", "#videos"), ("similar-end", "#similar")]),
        (390, 844, "eminem", [("first", None), ("discography", "#discography"), ("videos", "#videos"), ("similar-end", "#similar")]),
        (1280, 720, "lifafa", [("first", None), ("lower", "#discography")]),
        (1600, 900, "lifafa", [("first", None)]),
        (1600, 900, "easywanderlings", [("first", None), ("lower", "#discography"), ("videos", "#videos")]),
        (1280, 720, "easywanderlings", [("first", None)]),
        (390, 844, "easywanderlings", [("first", None), ("lower", "#discography")]),
        (390, 844, "lifafa", [("first", None)]),
    ]
    for w, h, slug, stops in plan:
        pg, errs = open_page(br, w, h, slug)
        instant(pg)
        if data is None:
            data = pg.evaluate("window.VINYL_ARTIST_DATA")
        lay = pg.evaluate(LAYOUT_JS)
        lay.update({"viewport": f"{w}x{h}", "artist": slug, "consoleErrors": errs})
        results["layout"].append(lay)
        check(f"{slug} {w}x{h}: no horizontal page overflow", lay["docScrollW"] == lay["docClientW"] and lay["pageScrollW"] == lay["pageClientW"] and not lay["outOfViewport"], lay["outOfViewport"])
        check(f"{slug} {w}x{h}: no visible scrollbars", not lay["visibleScrollbars"], lay["visibleScrollbars"])
        check(f"{slug} {w}x{h}: one vertical scroller", lay["verticalScrollers"] in ([], ["page"]), lay["verticalScrollers"])
        check(f"{slug} {w}x{h}: Popular Songs row 1 inside first viewport", lay["popularInFirstViewport"], f"row1 bottom {lay['firstSongBottom']} <= page bottom {lay['pageBottom']}")
        check(f"{slug} {w}x{h}: no console errors", not errs, errs)
        for label, sel in stops:
            if sel: scroll_to(pg, sel)
            shot(pg, f"{slug}-{w}x{h}-{label}.png")
        pg.close()

    # ---------------- keyboard journey (1280x720, Eminem) ----------------
    pg, errs = open_page(br, 1280, 720, "eminem")
    pg.keyboard.press("Tab")
    check("Tab from page lands on Play", focused(pg)["id"] == "artist-play", focused(pg))
    pg.keyboard.press("ArrowRight"); pg.keyboard.press("ArrowRight")
    check("Right/Left moves between hero actions", focused(pg)["id"] == "save", focused(pg))
    pg.keyboard.press("ArrowDown")
    check("Down from hero enters Popular Songs row 1", focused(pg)["label"] == data["artists"]["eminem"]["popular"][0]["title"], focused(pg))
    for _ in range(4): pg.keyboard.press("ArrowDown")
    check("Down walks rows to row 5", focused(pg)["label"] == data["artists"]["eminem"]["popular"][4]["title"], focused(pg))
    pg.keyboard.press("ArrowDown")
    check("Down after row 5 reaches Show more", "more-songs" in focused(pg)["cls"], focused(pg))
    pg.keyboard.press("Enter")
    vis = pg.evaluate("Array.from(document.querySelectorAll('.song')).filter(s=>!s.hidden).length")
    check("Show more extends the same single column to 10", vis == 10 and pg.evaluate("getComputedStyle(document.querySelector('.songs')).display") == "block", vis)
    pg.keyboard.press("ArrowUp")
    check("Up from Show more returns to row 10", focused(pg)["label"] == data["artists"]["eminem"]["popular"][9]["title"], focused(pg))
    pg.keyboard.press("ArrowUp"); pg.keyboard.press("ArrowUp")
    pg.keyboard.press("ArrowRight")
    check("Right on a song row reaches its Queue action", focused(pg)["cls"] == "ract" and focused(pg)["id"] == "queue", focused(pg))
    pg.wait_for_timeout(300); shot(pg, "eminem-1280x720-focus-song-actions.png")
    pg.keyboard.press("Enter")
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Queue action fires for the exact song", last["kind"] == "queue" and last["id"] == data["artists"]["eminem"]["popular"][7]["videoId"], last)
    pg.keyboard.press("ArrowLeft"); pg.keyboard.press("Enter")
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Enter on a row requests that exact song (no fake playback)", last["kind"] == "song" and last["id"] == data["artists"]["eminem"]["popular"][7]["videoId"]
          and pg.evaluate("document.querySelector('.now-copy b').textContent") == "Nothing playing", last)
    pg.keyboard.press("ArrowDown"); pg.keyboard.press("ArrowDown"); pg.keyboard.press("ArrowDown")
    check("Down past row 10 reaches Show less", "more-songs" in focused(pg)["cls"], focused(pg))
    pg.keyboard.press("Enter")
    vis = pg.evaluate("Array.from(document.querySelectorAll('.song')).filter(s=>!s.hidden).length")
    check("Show less returns the list to five rows", vis == 5, vis)
    pg.keyboard.press("ArrowDown")
    f = focused(pg)
    albums = data["artists"]["eminem"]["shelves"][0]["items"]
    check("Down from songs enters Albums shelf item 1", f["id"] == albums[0]["id"], f)
    rail_x0 = pg.evaluate("document.getElementById('rail-albums').scrollLeft")
    for _ in range(8): pg.keyboard.press("ArrowRight")
    pg.wait_for_timeout(700)
    f = focused(pg)
    rail_x1 = pg.evaluate("document.getElementById('rail-albums').scrollLeft")
    inview = pg.evaluate("""() => { const a=document.activeElement.getBoundingClientRect(), r=document.getElementById('rail-albums').getBoundingClientRect(); return a.left>=r.left && a.right<=r.right; }""")
    check("Right x8 moves to album 9 and the shelf reveals it", f["id"] == albums[8]["id"] and rail_x1 > rail_x0 and inview, {"focused": f["label"], "scrollLeft": [rail_x0, rail_x1], "inView": inview})
    off = pg.evaluate(ALIGNED_JS, "rail-albums")
    check("Shelf at rest starts on a whole cover (no cut-off cover on the left)", off is not None and off >= -1, off)
    shot(pg, "eminem-1280x720-focus-album-shelf.png")
    pg.keyboard.press("End"); pg.wait_for_timeout(600)
    check("End jumps to last album", focused(pg)["id"] == albums[-1]["id"], focused(pg))
    off = pg.evaluate(ALIGNED_JS, "rail-albums")
    check("At the end of the shelf the first visible cover is still whole", off is not None and off >= -1, off)
    pg.keyboard.press("Home"); pg.wait_for_timeout(600)
    pg.keyboard.press("Enter")
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Enter on a cover opens that exact release (native album owner)", last["kind"] == "album" and last["id"] == albums[0]["id"], last)
    nshelves = pg.evaluate("document.querySelectorAll('.shelf').length")
    titles = pg.evaluate("Array.from(document.querySelectorAll('.shelf h3')).map(h=>h.textContent)")
    check("Small categories merge into one More releases shelf", titles == ["Albums", "Singles", "More releases"], titles)
    for _ in range(nshelves): pg.keyboard.press("ArrowDown")
    pg.wait_for_timeout(600)
    f = focused(pg)
    vids = data["artists"]["eminem"]["videos"]
    check("Down through shelves reaches the Videos filmstrip", f["id"] == vids[0]["id"], f)
    for _ in range(5): pg.keyboard.press("ArrowRight")
    pg.wait_for_timeout(700)
    shot(pg, "eminem-1280x720-focus-video.png")
    pg.keyboard.press("Enter")
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Enter on video 6 opens exactly that video ID", last["kind"] == "video" and last["id"] == vids[5]["id"], last)
    shot(pg, "eminem-1280x720-route-video.png")
    # stationary filmstrip: nothing moves when idle
    before = pg.evaluate("""() => ({x: document.getElementById('rail-videos').scrollLeft, f: Array.from(document.querySelectorAll('#rail-videos .frame')).slice(0,8).map(e=>Math.round(e.getBoundingClientRect().left)), order: Array.from(document.querySelectorAll('#rail-videos .frame')).map(e=>e.dataset.id).join()})""")
    pg.wait_for_timeout(4000)
    after = pg.evaluate("""() => ({x: document.getElementById('rail-videos').scrollLeft, f: Array.from(document.querySelectorAll('#rail-videos .frame')).slice(0,8).map(e=>Math.round(e.getBoundingClientRect().left)), order: Array.from(document.querySelectorAll('#rail-videos .frame')).map(e=>e.dataset.id).join()})""")
    anims = pg.evaluate("document.getAnimations().length")
    check("Filmstrip is stationary for 4 s idle (no drift, no reorder, no running animations)", before == after and anims == 0, {"scrollLeft": [before["x"], after["x"]], "animations": anims})
    check("Video rail order equals provider order", after["order"] == ",".join(v["id"] for v in vids), "")
    pg.keyboard.press("ArrowDown"); pg.wait_for_timeout(500)
    live = data["artists"]["eminem"]["live"]
    check("Down from Videos reaches Live performances", focused(pg)["id"] == live[0]["id"], focused(pg))
    pg.keyboard.press("ArrowDown"); pg.wait_for_timeout(500)
    rel = data["artists"]["eminem"]["related"]
    pg.keyboard.press("ArrowRight"); pg.wait_for_timeout(400)
    check("Similar artists rail: Right selects artist 2", focused(pg)["id"] == rel[1]["id"], focused(pg))
    shot(pg, "eminem-1280x720-focus-similar.png")
    pg.keyboard.press("Enter")
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Enter opens that exact artist", last["kind"] == "artist" and last["id"] == rel[1]["id"], last)
    pg.keyboard.press("ArrowUp"); pg.wait_for_timeout(400)
    check("Up returns to the remembered live item", focused(pg)["id"] == live[0]["id"], focused(pg))
    pg.keyboard.press("Escape")
    st0 = pg.evaluate("document.getElementById('page').scrollTop")
    pg.keyboard.press("PageUp"); pg.wait_for_timeout(700)
    st1 = pg.evaluate("document.getElementById('page').scrollTop")
    pg.keyboard.press("ArrowUp"); pg.wait_for_timeout(500)
    st2 = pg.evaluate("document.getElementById('page').scrollTop")
    check("Escape returns to page; PageUp and Up scroll the page", st1 < st0 and st2 < st1, [st0, st1, st2])
    pg.keyboard.press("Home"); pg.wait_for_timeout(900)
    check("Home scrolls to top", pg.evaluate("document.getElementById('page').scrollTop") == 0, "")
    pg.close()

    # ---------------- pointer: wheel, arrows, drag, click ----------------
    pg, errs = open_page(br, 1600, 900, "eminem")
    pg.mouse.move(800, 500); pg.mouse.wheel(0, 900); pg.wait_for_timeout(900)
    check("Mouse wheel scrolls the page", pg.evaluate("document.getElementById('page').scrollTop") > 300, pg.evaluate("document.getElementById('page').scrollTop"))
    instant(pg)
    scroll_to(pg, "#videos", 20)
    check("No header arrow buttons; chevrons are not keyboard stops", pg.evaluate("document.querySelectorAll('.nav,.head-tools').length") == 0
          and pg.evaluate("Array.from(document.querySelectorAll('.chev')).every(c=>c.tabIndex===-1)"), "")
    st = pg.evaluate("({l:document.querySelector('.chev.left[data-for=rail-videos]').classList.contains('shown'), r:document.querySelector('.chev.right[data-for=rail-videos]').classList.contains('shown')})")
    check("At rest only the right edge chevron shows (more to the right only)", st == {"l": False, "r": True}, st)
    x0 = pg.evaluate("document.getElementById('rail-videos').scrollLeft")
    pg.click('.chev.right[data-for="rail-videos"]'); pg.wait_for_timeout(800)
    x1 = pg.evaluate("document.getElementById('rail-videos').scrollLeft")
    off = pg.evaluate(ALIGNED_JS, "rail-videos")
    check("Edge chevron pages the filmstrip and lands on a whole frame", x1 > x0 and off is not None and off >= -1, [x0, x1, off])
    check("After paging, the left chevron appears", pg.evaluate("document.querySelector('.chev.left[data-for=rail-videos]').classList.contains('shown')"), "")
    sim_size = pg.evaluate("Math.round(document.querySelector('.label').getBoundingClientRect().width)")
    check("Similar-artist portrait enlarged inside the same widget", sim_size == 64 and pg.evaluate("Math.round(document.querySelector('.badge').getBoundingClientRect().width)") == 132, sim_size)
    n_routes = pg.evaluate("window.__routes.length")
    box = pg.locator("#rail-videos .frame").nth(6).bounding_box()
    pg.mouse.move(box["x"] + 60, box["y"] + 60); pg.mouse.down(); pg.mouse.move(box["x"] - 200, box["y"] + 62, steps=8);     pg.mouse.up()
    pg.wait_for_timeout(700)
    x2 = pg.evaluate("document.getElementById('rail-videos').scrollLeft")
    check("Mouse drag scrolls the filmstrip without opening a video, then rests on a whole frame", x2 > x1 and pg.evaluate("window.__routes.length") == n_routes and pg.evaluate(ALIGNED_JS, "rail-videos") >= -1, [x1, x2])
    pg.mouse.wheel(300, 0); pg.wait_for_timeout(600)
    check("Horizontal wheel/trackpad scrolls the filmstrip", pg.evaluate("document.getElementById('rail-videos').scrollLeft") > x2, "")
    target = pg.locator("#rail-videos .frame").nth(9)
    tid = target.get_attribute("data-id")
    target.scroll_into_view_if_needed(); target.click(); pg.wait_for_timeout(200)
    last = pg.evaluate("window.__routes[window.__routes.length-1]")
    check("Click on a frame opens exactly that video", last["id"] == tid and last["kind"] == "video", last)
    pg.close()

    # ---------------- atmosphere continuity: no bands ----------------
    from PIL import Image
    for slug in ("eminem", "easywanderlings"):
        pg, errs = open_page(br, 1600, 900, slug)
        instant(pg)
        pg.add_style_tag(content="#inner,.topbar,.nowbar{visibility:hidden!important}")
        if slug == "easywanderlings":   # the wallpaper's own horizon is content, not a band: measure only our layers over it
            pg.add_style_tag(content="#wash{background-image:none!important;background-color:#4a4f5a!important}")
        worst = 0; per = []
        for frac in (0, .25, .5, .75, 1):
            pg.evaluate("f => { const p=document.getElementById('page'); p.scrollTop=(p.scrollHeight-p.clientHeight)*f; }", frac)
            pg.wait_for_timeout(300)
            path = str(SHOTS.parent / "_atmos.png"); pg.screenshot(path=path)
            im = Image.open(path).convert("L"); W, H = im.size; px = im.load()
            xs = range(0, W, 8)
            rows = [sum(px[x, y] for x in xs) / len(xs) for y in range(H)]
            jump = max(abs(rows[y + 1] - rows[y]) for y in range(H - 1))
            per.append(round(jump, 2)); worst = max(worst, jump)
        pathlib.Path(SHOTS.parent / "_atmos.png").unlink()
        limit = 2.0   # 8-bit gradient quantisation alone gives steps up to ~1.5; a band is a larger, local jump
        check(f"{slug}: background has no horizontal bands (max row-to-row luminance step < {limit} of 255 at 5 scroll positions" + (", overlays over a flat stand-in for the wallpaper" if slug != "eminem" else ", full stack incl. portrait") + ")", worst < limit, per)
        pg.close()

    # ---------------- states: wallpaper fallback, sparse ----------------
    pg, errs = open_page(br, 1280, 720, "easywanderlings")
    ATM = "({mode: document.body.classList.contains('wallpaper-mode'), img: getComputedStyle(document.getElementById('wash')).backgroundImage, figure: !document.getElementById('figure').hidden})"
    bd = pg.evaluate(ATM)
    check("No eligible artist image -> current Colosseum wallpaper, no album-cover blur", bd["mode"] and "current-home-pick.jpg" in bd["img"] and not bd["figure"], bd)
    pg.goto(URL + "#eminem"); pg.wait_for_timeout(400)
    bd = pg.evaluate(ATM)
    check("Eligible artist image -> blurred artist backdrop + blended portrait", not bd["mode"] and "eminem/hero.jpg" in bd["img"] and bd["figure"], bd)
    pg.goto(URL + "#lifafa"); pg.wait_for_timeout(400)
    s = pg.evaluate("({sections: Array.from(document.querySelectorAll('section.block')).map(s=>s.id), shelves: Array.from(document.querySelectorAll('.shelf h3')).map(h=>h.textContent)})")
    check("Sparse artist renders only sections with data", s["sections"] == ["popular", "discography", "videos", "similar"] and s["shelves"] == ["Albums", "Singles"], s)
    pg.close()
    br.close()

(ROOT / "qa-results.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
fails = [c for c in results["checks"] if not c["pass"]]
print(f"\n{len(results['checks']) - len(fails)}/{len(results['checks'])} checks passed; {len(results['screenshots'])} screenshots")
