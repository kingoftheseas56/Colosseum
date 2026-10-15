// ComicPackLabels.h — volume label parser for multi-volume pack demux
//
// A "pack" is one downloaded archive that turns out to contain N nested comic
// archives (e.g. the live Chew v1–v8 + Extras GetComics post: one ZIP whose top
// folder holds 12 complete .cbr/.cbz files, one per volume). The demux (see
// docs/superpowers/specs/2026-08-06-comics-multivolume-pack-demux-design.md)
// ingests each nested file as its own library entry under a shared seriesId;
// this parser maps each nested filename to the three things the shelf/reader
// need: a display label, a main-vs-extra role, and a deterministic order.
//
// This is a PURE function over a QString (the nested file's path relative to
// the pack's extract tree — exact bytes as extracted). No file IO, no Qt app
// context, no mutable state: it is safe to call from any thread and cheap to
// table-test directly (the harness links this header to exercise the dozen
// real Chew names as a literal table, which is why the parser lives in a
// header rather than ComicDownloader.cpp's anonymous namespace).
//
// Contract (the plan's "Label parser" + "Volume labels and roles"):
//   - v(\d+) anywhere in the name, zero-pad normalized → "Vol. N", role main,
//     order N. ("v1" and "v05" both resolve to their integer.)
//   - A "Bonus" token AND a v(\d+) match → role extra, label "Vol. N — Bonus",
//     order N (the bonus sorts with its volume's neighborhood, after mains).
//   - "Script Book" (case-insensitive token) → role extra, label "Script Book",
//     order after every parsed main (natural-sort sentinel, see kAfterMains).
//   - Any other unmatched named special (no v(\d+), no recognised token) →
//     role extra, label from the cleaned filename stem, order after mains.
//   - Unparseable (empty / all-noise) → role main, order kAfterMains, so it is
//     ALWAYS readable and ordered after the parsed mains by natural sort —
//     never hidden, never lost.
//   - Non-ASCII in source names (e.g. the real Chew `´` in "Taster´s") MUST
//     round-trip safely. The parser operates on QString (UTF-16) end to end;
//     it never re-encodes, so any Unicode the filesystem handed us survives.
//
// Serialization: role/order persist on the index Entry as packRole/packOrder
// (optional fields; absent = ordinary single issue, all legacy rows unchanged).
// They are the ONLY inputs the QML shelf needs to build the mains-only crossing
// chain and the Extras group — so the parser's output shape is a stable contract
// the reader chain depends on. See Slice 4's packVolumes() read API.
#pragma once
#include <QString>

namespace MangaTankoban {

struct PackLabel {
    // Display label shown on the shelf / reader header. For a main volume this
    // is "Vol. N"; for a bonus this is "Vol. N — Bonus"; for a named special
    // this is the cleaned filename stem (e.g. "Script Book"). Never empty for a
    // parseable input; for an unparseable input it is the (cleaned) stem or a
    // fallback literal so the row is never label-less.
    QString label;

    // "" / "main" / "extra". Serialized as packRole on the Entry; absent (empty)
    // means an ordinary single issue (pre-demux row), which the read API treats
    // as "not a pack volume" and excludes from both the mains and extras lists.
    QString role;

    // Deterministic order. Mains: the parsed volume number (1, 2, … 8). Extras:
    // the volume they attach to (a Bonus for v2 sorts at 2) so an Extras group
    // listed by order reads v1-Bonus, v2-Bonus, v3-Bonus naturally. Named
    // specials and unparseables use kAfterMains so natural-sort places them
    // after every parsed main. -1 is the Entry default (ordinary issue).
    int order = -1;

    static constexpr int kAfterMains = 1000000;  // natural-sort sentinel: after every realistic main
};

// Parse a nested archive's path (relative to the pack extract tree, or just the
// filename) into {label, role, order}. Pure; thread-safe; allocation-free apart
// from the returned QStrings. Declared inline so the harness can link it from
// the header alone without a .cpp.
inline PackLabel parsePackLabel(const QString& relPath)
{
    PackLabel out;
    // The stem: drop any directory prefix, drop the extension. We keep the rest
    // verbatim (including non-ASCII) so the label is faithful to the source name.
    QString stem = relPath;
    const int slash = stem.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) stem = stem.mid(slash + 1);
    const int dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) stem = stem.left(dot);   // dot==0 would be a hidden-file corner; leave it
    // Light cosmetic cleanup: collapse runs of whitespace, trim. We do NOT strip
    // parenthesised metadata like "(2012) (Digital) (Kingpin-Empire)" here — the
    // label is allowed to carry the source's flavour, and the shelf shows the
    // parsed "Vol. N" form anyway for mains. Named specials keep their full stem
    // because that IS the label.
    for (QChar& c : stem) {
        if (c == QChar('_') || c == QChar('.')) c = QChar(' ');
    }
    QString cleaned;
    cleaned.reserve(stem.size());
    bool lastSpace = true;   // collapse runs + trim leading
    for (const QChar c : stem) {
        if (c.isSpace()) {
            if (!lastSpace) cleaned.append(QChar(' '));
            lastSpace = true;
        } else {
            cleaned.append(c);
            lastSpace = false;
        }
    }
    cleaned = cleaned.trimmed();

    // Hunt for the volume number: first v(\d+) token, case-insensitive. "v05" and
    // "v1" both resolve to their integer. A token boundary (start, space, or one
    // of [(-]) must precede the 'v' so "Marvel" or "Draws" don't false-match.
    int volNumber = -1;
    const int n = cleaned.size();
    for (int i = 0; i < n; ++i) {
        if (cleaned.at(i).toLower() != QChar('v')) continue;
        // Boundary before v?
        if (i > 0) {
            const QChar prev = cleaned.at(i - 1);
            const bool boundary = prev.isSpace() || prev == QChar('(') || prev == QChar('-')
                                  || prev == QChar('[') || prev == QChar('_');
            if (!boundary) continue;
        }
        // Digits after v?
        int j = i + 1;
        while (j < n && cleaned.at(j).isDigit()) ++j;
        if (j == i + 1) continue;   // 'v' not followed by a digit
        bool ok = false;
        const int parsed = cleaned.mid(i + 1, j - i - 1).toInt(&ok);
        if (ok && parsed > 0) {
            volNumber = parsed;
            break;   // first v(\d+) wins
        }
    }

    // "Bonus" token, case-insensitive, on a token boundary.
    bool isBonus = false;
    {
        const QString lower = cleaned.toLower();
        int at = 0;
        while ((at = lower.indexOf(QStringLiteral("bonus"), at)) >= 0) {
            const bool boundaryBefore = (at == 0)
                || [&] {
                    const QChar prev = cleaned.at(at - 1);
                    return prev.isSpace() || prev == QChar('(') || prev == QChar('-')
                           || prev == QChar('[') || prev == QChar('_');
                }();
            const int after = at + 5;
            const bool boundaryAfter = (after >= lower.size())
                || [&] {
                    const QChar nxt = cleaned.at(after);
                    return !(nxt.isLetterOrNumber());
                }();
            if (boundaryBefore && boundaryAfter) { isBonus = true; break; }
            ++at;
        }
    }

    // "Script Book" token, case-insensitive (whole-phrase, token-bounded).
    const bool isScriptBook = [&] {
        const QString lower = cleaned.toLower();
        const int at = lower.indexOf(QStringLiteral("script book"));
        if (at < 0) return false;
        const bool boundaryBefore = (at == 0)
            || [&] {
                const QChar prev = cleaned.at(at - 1);
                return prev.isSpace() || prev == QChar('(') || prev == QChar('-')
                       || prev == QChar('[') || prev == QChar('_');
            }();
        const int after = at + 11;   // strlen("script book")
        const bool boundaryAfter = (after >= lower.size())
            || !cleaned.at(after).isLetterOrNumber();
        return boundaryBefore && boundaryAfter;
    }();

    if (volNumber > 0) {
        out.order = volNumber;
        if (isBonus) {
            out.role = QStringLiteral("extra");
            out.label = QStringLiteral("Vol. %1 \u2014 Bonus").arg(volNumber);
            // U+2014 EM DASH (the plan's "Vol. N — Bonus") as a Unicode
            // escape, independent of compiler source/execution encodings.
        } else {
            out.role = QStringLiteral("main");
            out.label = QStringLiteral("Vol. %1").arg(volNumber);
        }
        return out;
    }

    // No volume number. Two honest readings of the plan/spec exist for a
    // volume-less nested file:
    //  - the plan: "Script Book and unmatched named specials → extra".
    //  - spec §4:  "Unparseable volume filename → main, ordered after the
    //    parsed mains — always readable, never hidden."
    // Reconciliation: a POSITIVELY RECOGNISED special (Script Book) is an extra
    // with that label. Anything ELSE without a volume number is treated as
    // unparseable → MAIN, ordered after parsed mains. This is the safer default
    // (spec §4's "never hidden"): mis-classifying a real volume as an extra
    // would bury a readable book behind the Extras label, while classifying a
    // genuine extra as a main merely lists it at the tail of the main run —
    // readable, never lost. The only positively-recognised special today is
    // Script Book; extend this block as new known specials appear.
    if (isScriptBook) {
        out.role = QStringLiteral("extra");
        out.order = PackLabel::kAfterMains;
        out.label = QStringLiteral("Script Book");
        return out;
    }
    // Unparseable (no v#, no recognised special token): a main so it's always
    // readable, ordered after parsed mains by natural sort (never hidden).
    out.role = QStringLiteral("main");
    out.order = PackLabel::kAfterMains;
    out.label = cleaned.isEmpty() ? QStringLiteral("Volume") : cleaned;
    return out;
}

} // namespace MangaTankoban
