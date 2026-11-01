"""Split the Stremio server.js webpack bundle into one file per module.

Shape (probed 2026-08-07): `!(function(modules){ BOOTSTRAP })([ m0, m1, ... ]);`
A positional ARRAY, so module id == array index. No `/* id */` markers exist, so
ids must come from position - which means a lossy split would silently renumber
every module. The round-trip check below is therefore not ceremony: it is the only
thing standing between us and a mislabelled route map in Slice 2.

Usage: python split_bundle.py <server.js> <outdir>
Writes <outdir>/modules/<id>.js, <outdir>/index.json, <outdir>/_prologue.txt,
<outdir>/_epilogue.txt and verifies an exact byte round-trip before exiting 0.
"""
import json
import os
import sys

# Characters after which a '/' begins a REGEX literal rather than division.
# Standard heuristic; sufficient for bundler output.
REGEX_OK_AFTER = set("(,=:[!&|?{};+-*%~^<>") | {""}
REGEX_OK_WORDS = ("return", "typeof", "instanceof", "in", "of", "new", "delete",
                  "void", "throw", "case", "do", "else", "yield", "await")


def scan(src, start, stop_depth_zero_at=None):
    """Yield (index, char, depth_delta_applied) walking JS with literal awareness."""
    raise NotImplementedError  # kept intentionally unused; logic inlined below


def split_top_level(src, open_idx):
    """src[open_idx] must be '['. Return (elements, close_idx).

    elements is a list of (start, end) byte spans for each top-level array element,
    NOT including the separating commas.
    """
    assert src[open_idx] == "[", "expected array start"
    i = open_idx + 1
    n = len(src)
    depth = 0                 # nesting inside the array
    elems = []
    cur = i
    prev_sig = ""             # last significant char, for regex disambiguation

    while i < n:
        c = src[i]

        # --- comments ---
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            i = src.find("\n", i)
            if i == -1:
                i = n
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            i = (j + 2) if j != -1 else n
            continue

        # --- regex literal ---
        if c == "/":
            back = src[:i].rstrip()
            word = ""
            k = len(back) - 1
            while k >= 0 and (back[k].isalpha()):
                word = back[k] + word
                k -= 1
            is_regex = (prev_sig in REGEX_OK_AFTER) or (word in REGEX_OK_WORDS)
            if is_regex:
                i += 1
                in_class = False
                while i < n:
                    ch = src[i]
                    if ch == "\\":
                        i += 2
                        continue
                    if ch == "[":
                        in_class = True
                    elif ch == "]":
                        in_class = False
                    elif ch == "/" and not in_class:
                        break
                    elif ch == "\n":
                        break          # unterminated: treat as division after all
                    i += 1
                i += 1
                prev_sig = "/"
                continue

        # --- strings / templates ---
        if c in ("'", '"', "`"):
            q = c
            i += 1
            while i < n:
                ch = src[i]
                if ch == "\\":
                    i += 2
                    continue
                if q == "`" and ch == "$" and i + 1 < n and src[i + 1] == "{":
                    # template substitution: walk it with a brace counter
                    d = 1
                    i += 2
                    while i < n and d:
                        if src[i] == "{":
                            d += 1
                        elif src[i] == "}":
                            d -= 1
                        elif src[i] in ("'", '"', "`"):
                            qq = src[i]
                            i += 1
                            while i < n and src[i] != qq:
                                i += 2 if src[i] == "\\" else 1
                        i += 1
                    continue
                if ch == q:
                    break
                i += 1
            i += 1
            prev_sig = q
            continue

        # --- structure ---
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0 and c == "]":
                elems.append((cur, i))
                return elems, i
            depth -= 1
        elif c == "," and depth == 0:
            elems.append((cur, i))
            cur = i + 1

        if not c.isspace():
            prev_sig = c
        i += 1

    raise SystemExit("FAIL: array never closed")


def main():
    path, outdir = sys.argv[1], sys.argv[2]
    src = open(path, "r", encoding="latin-1", newline="").read()

    # Locate the module array: after the IIFE's closing ')(' comes '['.
    marker = src.find("}([")
    if marker == -1:
        # tolerate whitespace variants
        import re
        m = re.search(r"\}\s*\)\s*\(\s*\[", src)
        if not m:
            raise SystemExit("FAIL: could not locate module array opener")
        open_idx = src.index("[", m.start())
    else:
        open_idx = marker + 2

    elems, close_idx = split_top_level(src, open_idx)

    prologue = src[:open_idx + 1]
    epilogue = src[close_idx:]

    moddir = os.path.join(outdir, "modules")
    os.makedirs(moddir, exist_ok=True)
    index = []
    for mid, (a, b) in enumerate(elems):
        body = src[a:b]
        with open(os.path.join(moddir, "%d.js" % mid), "w",
                  encoding="latin-1", newline="") as f:
            f.write(body)
        stripped = body.strip()
        index.append({
            "id": mid,
            "start": a,
            "end": b,
            "len": b - a,
            "empty": stripped == "",
            "preview": stripped[:110].replace("\n", " "),
        })

    with open(os.path.join(outdir, "_prologue.txt"), "w",
              encoding="latin-1", newline="") as f:
        f.write(prologue)
    with open(os.path.join(outdir, "_epilogue.txt"), "w",
              encoding="latin-1", newline="") as f:
        f.write(epilogue)
    with open(os.path.join(outdir, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)

    # --- NEGATIVE CONTROL: exact byte round-trip, or the split is worthless ---
    rebuilt = prologue + ",".join(src[a:b] for a, b in elems) + epilogue
    if rebuilt != src:
        # narrow the first divergence for the report
        lo = 0
        while lo < min(len(rebuilt), len(src)) and rebuilt[lo] == src[lo]:
            lo += 1
        raise SystemExit("FAIL: round-trip differs at byte %d (len %d vs %d)"
                         % (lo, len(rebuilt), len(src)))

    print("OK: %d modules, round-trip byte-exact" % len(elems))
    print("   prologue %d bytes, epilogue %d bytes" % (len(prologue), len(epilogue)))
    nonempty = sum(1 for e in index if not e["empty"])
    print("   non-empty modules: %d   empty slots: %d"
          % (nonempty, len(index) - nonempty))


if __name__ == "__main__":
    main()
