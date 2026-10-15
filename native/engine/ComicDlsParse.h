// ComicDlsParse.h — the GetComics release-post link parser, extracted from
// ComicDownloader as a free function so the contract is harness-testable
// (Core-only, no Network). Returns signed /dls/ hrefs best-first:
// DOWNLOAD NOW > MAIN SERVER > aio-red > rest. The bare /dls/<token>/ ad-gate
// (no ":sig" payload) is excluded (the TB2 2026-06-05 scar). Anchors labeled
// pixeldrain — in attributes OR inner text — are DROPPED: the host is blocked
// from this ISP (http=000, probed 2026-07-10); a dead host is not a fallback.
#pragma once

#include <QByteArray>
#include <QStringList>

QStringList parseDlsLinks(const QByteArray& html);
