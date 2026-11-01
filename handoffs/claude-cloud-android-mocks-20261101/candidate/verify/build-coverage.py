#!/usr/bin/env python3
"""Build candidate/COVERAGE.csv (53 required pages) from the fresh evidence JSON files."""
import csv, json, os, collections
H = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
E = lambda p: json.load(open(os.path.join(H, 'candidate/evidence', p), encoding='utf-8'))
pages = json.load(open(os.path.join(H, 'candidate/verify/pages.json')))
smoke = E('smoke-candidate.json')['results']
sm = collections.defaultdict(lambda: [0, 0])
for r in smoke:
    if r['variant']: continue
    ok = not (r['errors'] or r['failed'] or r['external'] or r['brokenImages'] or r['horizontalOverflow'] or not r['frameAtRequestedSize'] or r['androidWindowActionsReachable'])
    sm[r['page']][0] += ok; sm[r['page']][1] += 1
b1 = E('batch1/focused-candidate.json'); b1c = collections.defaultdict(lambda: [0, 0])
for c in b1['results']:
    k = c['page'].split('?')[0]; b1c[k][0] += c['pass']; b1c[k][1] += 1
b2 = E('batch2/focused-candidate.json'); b3 = E('batch3/focused-candidate.json'); b5 = E('batch5/focused-candidate.json')
b4 = E('batch4/qualification-candidate.json'); b4c = collections.defaultdict(lambda: [0, 0])
for c in b4['results']: b4c[c['page']][0] += c['pass']; b4c[c['page']][1] += 1
b4p = collections.defaultdict(list)
for p in b4['probeResults']: b4p[p['page']].append(p['id'] + (' pass' if p['pass'] else ' FAIL'))
flow = b1['flowAllocation']
def flowtxt(mode):
    rs = [f for f in flow if f['mode'] == mode]
    return f"{sum(f['viable'] for f in rs)}/{len(rs)} viewports viable (190px floor, 1.10 focus kept)"
CHANGE = {
 'manga-series': 'CHANGED: rebuilt route is main; real TitlePageBar (Back pill, Search, desktop-only system menu); wordmark/progress/More retained',
 'manga-chapters': 'CHANGED: rebuilt route is main; real TitlePageBar; chapter list/navigation unchanged',
 'volume-flow-original-fit': 'REBUILT on the selected header: allocation A (source fit, page fixed)',
 'volume-flow-minimal-alternative': 'REBUILT on the selected header: allocation B (page scroll) - proposal',
 'film-detail': 'INTEGRATED Sol SOL-B2-001 (Android window gating) - both Theatre renderers kept',
 'series-detail': 'INTEGRATED Sol SOL-B2-001 (Android window gating) - both Theatre renderers kept',
 'search': 'INTEGRATED Sol Search candidate 5f3adf98; CHANGED window controls desktop-only',
 'genre-index': 'Shares 20-search-browse script (Sol candidate); CHANGED tablet window controls removed',
 'genre-results': 'Shares 20-search-browse script (Sol candidate); CHANGED tablet window controls removed',
 'continue-see-all': 'Shares 20-search-browse script (Sol candidate); CHANGED tablet window controls removed',
 'biblio-explore': 'Shares 20-search-browse script (Sol candidate); window controls desktop-only',
 'biblio-home': 'CHANGED: Apple Books icon restored from fixed pin (was 404)',
 'tracker-connections': 'INTEGRATED Sol tracker.js 866b0560 (exact opener / nested dialog focus)',
 'tracker-dossier': 'INTEGRATED Sol tracker.js 866b0560',
 'tracker-preferences': 'INTEGRATED Sol tracker.js 866b0560',
 'wallpaper-search': 'Inherited KDE art gap: restorable byte-exact via verify/fetch-kde-wallpapers.py (git-ignored)',
 'account-flyout': 'CHANGED: desktop TopBar underlay window controls desktop-only (tablet leak fixed)',
}
for n in ['account-centre', 'account-profile', 'account-security', 'account-recovery', 'account-privacy', 'account-devices', 'account-your-colosseum']:
    CHANGE[n] = 'CHANGED: window controls desktop-only (Android tablet leak fixed)'
CHOICE = {
 'manga-series': 'Language box in Tankoban Mode (preview: disabled box; rebuilt source hides it); wordmark placement; flow allocation A vs B',
 'manga-chapters': 'Wordmark placement (TitlePageBar has none)',
 'volume-flow-original-fit': 'Flow allocation A vs B (Hemanth)', 'volume-flow-minimal-alternative': 'Flow allocation A vs B (Hemanth)',
 'film-detail': 'Theatre legacy vs rebuilt route (Hemanth)', 'series-detail': 'Theatre legacy vs rebuilt route (Hemanth)',
 'saga-universe': 'B2-018 Universe tabs below Back (recommended fit) vs original', 'shelves-universe': 'B2-018 Universe tabs below Back (recommended fit) vs original',
 'account-profile': 'B4-009 keyboard: original nav joins scroll while IME visible?', 'account-security': 'B4-009 keyboard', 'account-recovery': 'B4-009 keyboard', 'account-privacy': 'B4-009 keyboard', 'account-centre': 'B4-009 keyboard',
}
GAP = {'01-sources-manga': 'Disclosed stand-in art (no exact manga jackets) and illustrative source fixtures', '02-detail-pages': 'Disclosed art/native gaps per batch REVIEW', '03-browsing-libraries': 'Optional poster bleed not selected; disclosed art gaps',
       '04-accounts-configuration': 'Local fixtures only; no auth/service; IME is illustrative', '05-tracker-utilities': 'Disclosed art/native gaps per batch REVIEW'}
rows = []
for p in pages:
    n, b = p['page'], p['batch']
    if b == '01-sources-manga' and n in ('manga-series', 'manga-chapters'): foc = f"focused-b1 {b1c[n][0]}/{b1c[n][1]} viewport cases (TitlePageBar, Android omission, desktop menu, popups, flow)"
    elif n == 'volume-flow-original-fit': foc = 'flow geometry A: ' + flowtxt('A-fit')
    elif n == 'volume-flow-minimal-alternative': foc = 'flow geometry B: ' + flowtxt('B-scroll')
    elif n in ('film-detail', 'series-detail'): foc = f"focused-b2 {sum(r['pass'] for r in b2['results'] if r['kind']==n.split('-')[0])}/{sum(1 for r in b2['results'] if r['kind']==n.split('-')[0])}"
    elif n == 'search': foc = f"focused-b3 {b3['passed']}/{b3['cases']} on one revision"
    elif n.startswith('tracker-'): foc = f"focused-b5 {b5['passed']}/{b5['cases']} (tracker.js shared)"
    elif b == '04-accounts-configuration': foc = f"qualify-b4 {b4c[n][0]}/{b4c[n][1]}" + (('; probes ' + ', '.join(b4p[n])) if b4p[n] else '')
    elif n in ('film-sources', 'comic-sources'):
        si = E('batch1/sources-interactions-candidate.json'); k = 'Provider' if n == 'film-sources' else 'Weak-match'
        rs = [r for r in si['results'] if r['name'].startswith(k)]; foc = f"focused-b1-sources {sum(r['pass'] for r in rs)}/{len(rs)} (Codex receiving checks re-run)"
    else: foc = 'load/size/overflow/window smoke only (unchanged page)'
    s = sm[n]
    rows.append({'batch': b, 'page': n, 'candidate_path': 'candidate/' + p['path'], 'change': CHANGE.get(n, 'Carried unchanged from finished mock'),
                 'smoke_9_cases': f"{s[0]}/{s[1]}", 'focused_checks': foc, 'android_window_actions_reachable': '0 (smoke)',
                 'state': 'browser-tested (Linux Chromium); not native-qualified', 'disclosed_gaps': GAP[b] + '; Segoe fallback (metrics unverified)',
                 'open_product_choice': CHOICE.get(n, '')})
with open(os.path.join(H, 'candidate/COVERAGE.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
print(len(rows), 'rows;', sum(r['smoke_9_cases'] == '9/9' for r in rows), 'with 9/9 smoke')
