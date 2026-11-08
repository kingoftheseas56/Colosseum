#!/usr/bin/env python3
"""Search AudioBookBay for every book in Colosseum's book-centric universes.

Book-centric = the universe's PRIMARY label includes "books" (Dune, Harry Potter,
Dexter, Slow Horses, ...), or it has no PRIMARY entry and every chip is books
(Cosmere). A universe that merely *holds* novels (One Piece, Naruto, James Bond,
Jurassic Park, Avatar) is skipped.

Books are read from qml/Universes.js (the curation source) through Node, in the same
way scripts/export_universe_content.cjs reads it. Search and parsing follow qml/AbbApi.js:
lowercase query (capitals redirect to the homepage), the exact `<div class="post">`
block (which skips honeypot decoys), and the shared-word relevance gate.

Usage:
  python scripts/universe_audiobooks.py --list            # show the books, no network
  python scripts/universe_audiobooks.py                   # search all, write JSON
  python scripts/universe_audiobooks.py --universe Dune --details
Output defaults to output/universe-audiobooks.json. Each book is written as soon as it
is searched, and books already in the file are skipped, so an interrupted run resumes.
"""
import argparse
import html
import json
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABB = "https://audiobookbay.lu"  # mirrors move on takedowns; keep in step with qml/AbbApi.js
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36")

# Universes whose `novels` are bare titles get their author from here.
AUTHORS = {
    "Harry Potter": "J.K. Rowling",
    "Lord of the Rings": "J.R.R. Tolkien",
    "A Song of Ice and Fire": "George R.R. Martin",
    "Dune": "Frank Herbert",
    "The Witcher": "Andrzej Sapkowski",
    "Sherlock Holmes": "Arthur Conan Doyle",
    "Percy Jackson": "Rick Riordan",
}

STOP = {"the", "and", "for", "with", "book", "series", "audiobook", "unabridged",
        "novel", "volume"}

EXTRACT_JS = r"""
const fs=require('fs'),vm=require('vm');
const c={ContentApi:{register(){},document(){return {}},asset(v){return v},resolveAssets(v){return v}}};
vm.createContext(c);
vm.runInContext(fs.readFileSync(process.argv[1],'utf8').replace(/^\.(?:pragma|import).*$/gm,''),c);
process.stdout.write(JSON.stringify({universes:c.universes.concat(c.archive),PRIMARY:c.PRIMARY}));
"""


def load_catalogue():
    out = subprocess.run(["node", "-e", EXTRACT_JS, str(ROOT / "qml" / "Universes.js")],
                         capture_output=True, text=True, encoding="utf-8", check=True)
    return json.loads(out.stdout)


def is_book_centric(universe, primary):
    labels = primary.get(universe["name"])
    if labels:
        return "books" in labels
    chips = universe.get("chips") or []
    return bool(chips) and all(c.get("ic") == "books" for c in chips)


def books_of(universe):
    """Yield (group, title, author, query) for every book the universe page lists."""
    name = universe["name"]
    for entry in universe.get("novels") or []:
        if isinstance(entry, dict):
            title = entry.get("t") or entry.get("title") or ""
            author = entry.get("author") or AUTHORS.get(name, "")
        else:
            title, author = str(entry), AUTHORS.get(name, "")
        yield "Novels", title, author, None
    for shelf in universe.get("shelves") or []:
        if shelf.get("kind") != "book":
            continue
        for run in shelf.get("runs") or []:
            for work in run.get("works") or []:
                yield (shelf.get("title") or "Books", work.get("t", ""),
                       work.get("author", ""), None)
    for series in universe.get("cosmereSeries") or []:
        for book in series.get("books") or []:
            query = book.get("query", "")
            title = re.sub(r"\s+Brandon Sanderson$", "", query)
            yield series.get("name", "Cosmere"), title, "Brandon Sanderson", query


def search_term(title, author):
    if author and author.split()[-1].lower() in title.lower():
        author = ""  # the title already names the author (e.g. "... Newt Scamander")
    return " ".join(x for x in (title, author) if x).strip().lower()


def fetch(url, tries=3):
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30) as resp:
                final = resp.geturl()
                return resp.read().decode("utf-8", "replace"), final
        except Exception as exc:  # network errors: back off, then give up
            if attempt == tries - 1:
                raise
            print(f"    retry {attempt + 1}: {exc}", file=sys.stderr)
            time.sleep(3 * (attempt + 1))


def clean(s):
    return html.unescape(s or "").strip()


def parse_search(text, base):
    starts = [m.start() for m in re.finditer(r'<div class="post">', text)]
    rows = []
    for i, start in enumerate(starts):
        block = text[start:starts[i + 1] if i + 1 < len(starts) else len(text)]
        tm = re.search(r'<h2><a href="([^"]+)"[^>]*>([^<]+)</a></h2>', block)
        if not tm:
            continue
        url = tm.group(1).strip()
        url = url if url.startswith("http") else base + url
        sm = re.search(r"/abss/([^/]+)/?", url)
        if not sm:
            continue
        full = clean(tm.group(2))
        title, author = full, ""
        dash = full.rfind(" - ")
        if 0 < dash < len(full) - 3:
            after = full[dash + 3:].strip()
            if len(after) <= 60 and "/" not in after and "[" not in after and not after[:1].isdigit():
                title, author = full[:dash].strip(), after

        def grab(pattern):
            m = re.search(pattern, block, re.I)
            return m

        lang = grab(r"Language:\s*([^<]+?)(?:<span|<br)")
        fmt = grab(r"Format:\s*<span[^>]*>([^<]+)</span>")
        size = grab(r"File Size:\s*<span[^>]*>([^<]+)</span>\s*([GMK]?Bs?)")
        posted = grab(r"Posted:\s*([^<]+?)<br")
        rows.append({
            "title": title, "author": author, "slug": sm.group(1), "url": url,
            "language": clean(lang.group(1)) if lang else "",
            "format": fmt.group(1).strip() if fmt else "",
            "size": f"{size.group(1).strip()} {size.group(2)}" if size else "",
            "posted": posted.group(1).strip() if posted else "",
        })
    return rows


def words(s):
    return [w for w in re.findall(r"[a-z0-9]{3,}", s.lower())]


def score(row, title, author):
    """Share of the book's title words found in the row; author match breaks ties."""
    want = [w for w in words(title) if w not in STOP] or words(title)
    hay = (row["title"] + " " + row["author"]).lower()
    if not want:
        return 0.0
    s = sum(w in hay for w in want) / len(want)
    if author and author.split()[-1].lower() in hay:
        s += 0.25
    return round(s, 2)


def search(base, title, author, query=None):
    term = (query or "").lower() or search_term(title, author)
    tried = []
    for q in dict.fromkeys([term, title.lower()]):  # retry with the bare title on a miss
        tried.append(q)
        text, final = fetch(base + "/?s=" + urllib.parse.quote(q))
        if urllib.parse.urlparse(final).query == "":
            continue  # redirected to the homepage: its posts are not results
        rows = [r for r in parse_search(text, base) if score(r, title, "") > 0]
        for r in rows:
            r["score"] = score(r, title, author)
        rows.sort(key=lambda r: -r["score"])
        if rows:
            return rows, tried
    return [], tried


def details(base, slug):
    text, _ = fetch(f"{base}/abss/{slug}/")
    m = re.search(r"<td>Info Hash:</td>\s*<td[^>]*>\s*([0-9a-fA-F]{40})\s*</td>", text, re.I)
    return m.group(1).lower() if m else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="print the books and exit (no network)")
    ap.add_argument("--universe", action="append", help="limit to these universes (repeatable)")
    ap.add_argument("--top", type=int, default=5, help="results kept per book (default 5)")
    ap.add_argument("--details", action="store_true", help="also fetch the best match's info hash")
    ap.add_argument("--delay", type=float, default=2.0, help="seconds between requests (default 2)")
    ap.add_argument("--base", default=ABB, help="AudioBookBay mirror URL")
    ap.add_argument("--out", default=str(ROOT / "output" / "universe-audiobooks.json"))
    args = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")

    cat = load_catalogue()
    picked = [u for u in cat["universes"] if is_book_centric(u, cat["PRIMARY"])]
    if args.universe:
        want = {n.lower() for n in args.universe}
        picked = [u for u in picked if u["name"].lower() in want]

    plan = [(u["name"], g, t, a, q) for u in picked for g, t, a, q in books_of(u) if t]
    print(f"{len(picked)} book-centric universes, {len(plan)} books: "
          + ", ".join(u["name"] for u in picked), file=sys.stderr)
    if args.list:
        for name, group, title, author, _ in plan:
            print(f"{name} | {group} | {title} | {author}")
        return

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    done = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {"books": []}
    seen = {(b["universe"], b["title"]) for b in done["books"]}
    base = args.base.rstrip("/")

    for i, (name, group, title, author, query) in enumerate(plan, 1):
        if (name, title) in seen:
            continue
        print(f"[{i}/{len(plan)}] {name}: {title}", file=sys.stderr)
        try:
            rows, tried = search(base, title, author, query)
            error = ""
        except Exception as exc:
            rows, tried, error = [], [], str(exc)
        rows = rows[: args.top]
        if args.details and rows:
            time.sleep(args.delay)
            try:
                rows[0]["infoHash"] = details(base, rows[0]["slug"])
            except Exception as exc:
                rows[0]["infoHashError"] = str(exc)
        done["books"].append({
            "universe": name, "group": group, "title": title, "author": author,
            "queries": tried, "found": bool(rows), "results": rows,
            **({"error": error} if error else {}),
        })
        done["source"] = base
        out.write_text(json.dumps(done, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        time.sleep(args.delay)

    books = done["books"]
    found = sum(b["found"] for b in books)
    print(f"done: {found}/{len(books)} books have results -> {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
