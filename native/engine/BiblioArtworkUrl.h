#pragma once

#include <QRegularExpression>
#include <QString>

// normalizedAppleArtworkUrl — Apple's mzstatic "thumb" CDN serves cover art at
// .../<file>.<ext>/<W>x<H>bb.<ext2>. Two real problems collapse into one fix here:
//
//  1. Apple's own top-ebooks RSS feed (BiblioProviders::parseAppleRss) ships im:image
//     labels shaped "0x<N>bb.png" — an auto-width placeholder the "bb" (bounding-box)
//     resize style cannot actually produce. Verified live against production, 2026-08-06:
//     requesting that exact URL returns HTTP 400 from Apple's CDN with the body
//     {"errorMessage":"Cannot produce 0x170 image with Resize Style: 'bb'"} — every
//     height variant (55/60/170) fails identically, so this is not a size problem, the
//     whole "0xN" shape is dead on arrival. This is why most Biblio Discover covers
//     rendered as blank placeholders rather than blurry thumbnails.
//  2. Apple's Search API (BiblioProviders::parseAppleSearch, artworkUrl100/60) returns
//     VALID but small (60-100px) two-dimension URLs on the same CDN endpoint, which
//     read blurry once enlarged into a grid card.
//
// Both are the same URL family, so one rewrite fixes both: replace whatever trailing
// size segment is present with a fixed, verified-working WxH. 600x600bb.jpg was chosen
// (not a larger size) because it comfortably covers a 148px logical gallery card even
// at 3x device pixel ratio (~444px) without paying for detail nothing on screen can
// show — confirmed at 92KB per cover vs ~350KB at 1400x1400 for the same asset.
//
// Fail-safe: a URL that doesn't match the expected trailing "/WxHbb.ext" shape (Open
// Library covers, a future Apple CDN change, anything malformed in an unrecognized way)
// is returned completely untouched — this function only ever narrows a known, verified
// pattern, it never guesses at or invents a provider URL shape.
inline QString normalizedAppleArtworkUrl(const QString &url)
{
    static const QRegularExpression sizeSuffix(
        QStringLiteral("/\\d+x\\d+bb\\.(?:jpg|jpeg|png)$"));
    const QRegularExpressionMatch m = sizeSuffix.match(url);
    if (!m.hasMatch())
        return url;
    return url.left(m.capturedStart()) + QStringLiteral("/600x600bb.jpg");
}
