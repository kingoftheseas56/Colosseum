#!/usr/bin/env python3
"""Build candidate/index.html (combined runnable entry) and candidate/compare.html (before/after)."""
import csv, json, os, html
H = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))); C = os.path.join(H, 'candidate')
rows = list(csv.DictReader(open(os.path.join(C, 'COVERAGE.csv'), encoding='utf-8')))
pairs = json.load(open(os.path.join(C, 'evidence/before-after/pairs.json')))['pairs']
e = html.escape
CSS = """:root{--bg:#0d0f15;--panel:#151923;--ink:#f3f2ee;--dim:#b5bac6;--gold:#f0c44a;--edge:#2c3342}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 "Segoe UI",Inter,system-ui,sans-serif}main{max-width:1180px;margin:auto;padding:28px 16px 60px}h1{font-size:27px;margin:0 0 6px}h2{font-size:19px;margin:30px 0 8px;border-bottom:1px solid var(--edge);padding-bottom:6px}a{color:var(--gold)}p{color:var(--dim);max-width:900px}table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:6px 8px;border-bottom:1px solid var(--edge);text-align:left;vertical-align:top}th{color:var(--dim);font-weight:600}.chg{color:#e7d9a8}.tag{display:inline-block;font-size:11px;border:1px solid var(--edge);border-radius:4px;padding:0 5px;margin-left:4px;color:var(--dim)}.note{background:var(--panel);border:1px solid #6d5a25;border-radius:8px;padding:12px 16px;color:var(--ink)}.wrap{overflow-x:auto}.pair{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin:8px 0 26px}.pair img{width:100%;border:1px solid var(--edge);display:block}.pair h3{font-size:13px;color:var(--dim);margin:0 0 4px;font-weight:600}@media(max-width:700px){.pair{grid-template-columns:1fr}}"""
B = {'01-sources-manga': 'Batch 1 · Sources and manga (9)', '02-detail-pages': 'Batch 2 · Detail pages (8)', '03-browsing-libraries': 'Batch 3 · Browsing and libraries (16)',
     '04-accounts-configuration': 'Batch 4 · Accounts and configuration (13)', '05-tracker-utilities': 'Batch 5 · Tracker and utilities (7)'}
out = [f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Android Mock Candidate</title><style>{CSS}</style></head><body><main>',
 '<h1>Colosseum Android phone mocks · combined candidate</h1>',
 '<p>All five batches in one runnable tree: 53 required pages, Sol’s three verified corrections integrated, Batch 1 rebuilt to Hemanth’s selected manga direction with the real TitlePageBar, and Batch 4 independently qualified. Each page opens with its own viewport, inset and text-scale controls outside the Colosseum frame.</p>',
 '<p class="note">Local HTML mocks only. States are <b>browser-tested in headless Linux Chromium</b> (fresh evidence below), not native-qualified: no Qt, Android device, Windows font or live-service behaviour is claimed. Segoe UI binaries are intentionally absent; a labelled Linux fallback renders instead, so text metrics are unverified. Run <code>python3 -m http.server 8000</code> from the handoff folder and open <code>/candidate/index.html</code>. For Batch 5 wallpaper art, first run <code>python3 candidate/verify/fetch-kde-wallpapers.py</code>.</p>',
 '<p><a href="REPORT.md">Final report</a> · <a href="COVERAGE.csv">Per-page coverage (53)</a> · <a href="QML-PORTING.md">QML porting notes</a> · <a href="compare.html">Before/after comparisons</a> · <a href="evidence/batch1/FLOW-REEVALUATION.md">Cover-flow re-evaluation</a> · <a href="../baseline/review-batches/RECEIVING-STATUS.md">Receiving status (input)</a> · <a href="../index.html">Frozen baseline entry</a></p>']
for b, title in B.items():
    idx = {'01-sources-manga': 'review-batches/01-sources-manga/finished-mocks/index.html', '02-detail-pages': 'review-batches/02-detail-pages/finished-mocks/index.html', '03-browsing-libraries': 'review-batches/03-browsing-libraries/finished-mocks/index.html', '04-accounts-configuration': 'review-batches/04-accounts-configuration/finished-mocks/index.html', '05-tracker-utilities': 'review-batches/05-tracker-utilities/finished-mocks/index.html'}[b]
    out.append(f'<h2>{e(title)}</h2><p><a href="{idx}">Batch index (comparisons, prior evidence)</a></p><div class="wrap"><table><tr><th>Page</th><th>Candidate state</th><th>Fresh checks</th><th>Open choice</th></tr>')
    for r in [r for r in rows if r['batch'] == b]:
        href = r['candidate_path'].replace('candidate/', '', 1)
        alt = ''
        if r['page'] in ('manga-series', 'manga-chapters'): alt = f' <a class="tag" href="{href}?route=reading-room">older reading room</a>'
        if r['page'] in ('film-detail', 'series-detail'): alt = f' <a class="tag" href="{href}?renderer=legacy">legacy</a> <a class="tag" href="{href}?renderer=rebuilt">rebuilt</a>'
        chg = r['change']; cls = ' class="chg"' if not chg.startswith('Carried') else ''
        out.append(f'<tr><td><a href="{href}">{e(r["page"])}</a>{alt}</td><td{cls}>{e(chg)}</td><td>smoke {e(r["smoke_9_cases"])} · {e(r["focused_checks"])}</td><td>{e(r["open_product_choice"])}</td></tr>')
    out.append('</table></div>')
out.append('<h2>Decisions still open for Hemanth</h2><ul><li>Cover-flow allocation: A (source fit; fails on phones) or B (page scroll; costs 102–559 px of scrolling) — <a href="review-batches/01-sources-manga/finished-mocks/05-volume-flow/index.html">compare</a>.</li><li>Keep the COLOSSEUM · TANKOBAN wordmark in the title bar (retained now; TitlePageBar has none) and the disabled Language box in Tankoban Mode (preview) vs hiding it (rebuilt source).</li><li>Theatre legacy vs rebuilt title page (separate from manga; one shared Main.qml flag today).</li><li>B2-018 Universe tabs placed below Back (recommended fit) vs original placement.</li><li>B4-009 account navigation joining the scroll while the keyboard is visible.</li></ul>')
out.append('</main></body></html>')
open(os.path.join(C, 'index.html'), 'w', encoding='utf-8').write('\n'.join(out))
cmp = [f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Before After</title><style>{CSS}</style></head><body><main><h1>Before / after</h1><p>Same URL, viewport and Chromium build; before = frozen <code>baseline/</code>, after = <code>candidate/</code>. <a href="index.html">Back to the candidate</a>. Hashes: <a href="evidence/before-after/pairs.json">pairs.json</a>.</p>']
for p in pairs:
    cmp.append(f'<h2>{e(p["note"])}</h2><p>{e(p["page"])} · {p["viewport"][0]}×{p["viewport"][1]}</p><div class="pair"><div><h3>Before</h3><img loading="lazy" src="evidence/before-after/{p["before"]["file"]}" alt="before"></div><div><h3>After</h3><img loading="lazy" src="evidence/before-after/{p["after"]["file"]}" alt="after"></div></div>')
flow = json.load(open(os.path.join(C, 'evidence/batch1/focused-candidate.json')))['flowAllocation']
cmp.append('<h2>Cover-flow allocation under the selected header (A fit vs B scroll)</h2><p>See <a href="evidence/batch1/FLOW-REEVALUATION.md">the measured re-evaluation</a>.</p>')
for vp in ['phone-portrait', 'phone-landscape', 'small-landscape', 'desktop']:
    a = next(f for f in flow if f['mode'] == 'A-fit' and f['vp'] == vp); b = next(f for f in flow if f['mode'] == 'B-scroll' and f['vp'] == vp)
    bimg = b.get('captureScrolled') or b['capture']
    cmp.append(f'<p>{vp} {a["w"]}×{a["h"]}: A book {a["bookHeight"]}px (raw {a["unclampedBookHeight"]}) {"fits" if a["viable"] else "does not fit"}; B book {b["bookHeight"]}px, scroll {b["scrollToSeeFocusedAndActions"]}px</p><div class="pair"><div><h3>A · source fit</h3><img loading="lazy" src="evidence/batch1/{a["capture"]["file"]}" alt=""></div><div><h3>B · page scroll{" (scrolled to the flow)" if b.get("captureScrolled") else ""}</h3><img loading="lazy" src="evidence/batch1/{bimg["file"]}" alt=""></div></div>')
cmp.append('</main></body></html>')
open(os.path.join(C, 'compare.html'), 'w', encoding='utf-8').write('\n'.join(cmp))
print('index + compare written')
