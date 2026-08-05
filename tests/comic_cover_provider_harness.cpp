// Task 3 (CBZ-in-place plan): image://comiccover, a fully stateless cover
// provider that decodes a scaled thumbnail directly from a CBZ page -- no
// loose file on disk to point at. Proves: a valid (archive, entry) id decodes
// to a non-null, correctly-scaled image; a missing entry / malformed id
// resolves to null without crashing; and the id round-trips through the exact
// base64url encoding buildId() (what downloadedIssues() will call) produces.
#include "engine/ComicCoverProvider.h"
#include "engine/ComicCoverId.h"
#include "engine/CbzArchive.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_failures = 0;
#define CHECK(cond, label)                                     \
    do {                                                        \
        if (!(cond)) {                                          \
            std::fprintf(stderr, "FAIL: %s\n", (label));        \
            ++g_failures;                                       \
        }                                                       \
    } while (0)

// Builds a real CBZ with genuine, decodable PNG page bytes via
// CbzArchive::writeImagesAtomic -- the same miniz-backed writer this
// provider's own readEntry() reads through (cbz_archive_probe_harness's own
// fixtures use the same technique for the same reason). A tar.exe-built zip
// is fine for the extraction-subprocess tests elsewhere in this suite (they
// go through bsdtar, never miniz) but is NOT guaranteed miniz-readable --
// this provider's whole job is decoding pixels via readEntry(), so the
// fixture must be built the way readEntry()'s own writer builds one.
bool makeCoverCbz(const QString& root, const QString& name,
                   const QSize& pageSize, QString* archivePath)
{
    const QString pages = root + QLatin1Char('/') + name + QStringLiteral("-pages");
    if (!QDir().mkpath(pages)) return false;

    QImage page(pageSize, QImage::Format_ARGB32);
    page.fill(qRgb(180, 60, 60));
    if (!page.save(pages + QStringLiteral("/page_000.png"), "PNG")) return false;

    *archivePath = root + QLatin1Char('/') + name + QStringLiteral(".cbz");
    QString error;
    return MangaTankoban::CbzArchive::writeImagesAtomic(
        *archivePath, pages, {QStringLiteral("page_000.png")}, &error);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTemporaryDir temp;
    CHECK(temp.isValid(), "temp fixture dir created");

    QString archivePath;
    CHECK(makeCoverCbz(temp.path(), QStringLiteral("cover"), QSize(800, 1200), &archivePath),
          "fixture CBZ with a real 800x1200 PNG page created");

    Colosseum::ComicCoverProvider provider;

    // ── A valid id decodes to a non-null, correctly scaled image ─────────────
    {
        const QString id = Colosseum::buildComicCoverId(archivePath, QStringLiteral("page_000.png"));
        QSize reportedSize;
        const QImage image = provider.requestImage(id, &reportedSize, QSize(200, 300));
        CHECK(!image.isNull(), "a valid (archive, entry) id decodes to a non-null image");
        // 800x1200 scaled to fit 200x300 keeping aspect ratio -> exactly 200x300
        // (the source is already a 2:3 ratio, same as the request).
        CHECK(image.size() == QSize(200, 300),
              "the image is scaled DOWN to the requested tile size, not served at native 800x1200");
        CHECK(reportedSize == image.size(), "the reported size (out-param) matches the returned image");
    }

    // ── No requested size still gets a bounded default, not a full decode ────
    {
        const QString id = Colosseum::buildComicCoverId(archivePath, QStringLiteral("page_000.png"));
        const QImage image = provider.requestImage(id, nullptr, QSize());
        CHECK(!image.isNull(), "an unsized request still decodes something");
        CHECK(image.width() < 800 && image.height() < 1200,
              "an unsized request is capped to a sane default, not the native 800x1200 page "
              "(a grid tile must never pay for a full-resolution decode)");
    }

    // ── A partial requestedSize (one dimension only) is honored, not dropped ──
    // QML's `sourceSize.width: 296` alone produces QSize(296, 0) -- a naive
    // "both must be positive" check silently ignores this and falls back to
    // the default box, dropping the caller's explicit request.
    {
        const QString id = Colosseum::buildComicCoverId(archivePath, QStringLiteral("page_000.png"));
        const QImage widthOnly = provider.requestImage(id, nullptr, QSize(400, 0));
        // 800x1200 constrained to width=400 (height unconstrained) -> 400x600.
        CHECK(widthOnly.size() == QSize(400, 600),
              "a width-only requestedSize (height<=0) is honored via the width, not dropped to the default box");

        const QImage heightOnly = provider.requestImage(id, nullptr, QSize(0, 400));
        // 800x1200 constrained to height=400 (width unconstrained) -> 267x400 (round down).
        CHECK(heightOnly.height() == 400 && heightOnly.width() < 800,
              "a height-only requestedSize (width<=0) is honored via the height, not dropped to the default box");
    }

    // ── A source smaller than the target box is served unscaled, not upscaled ─
    // Upscaling a thumbnail wastes memory/bandwidth for no visual gain and
    // contradicts this provider's whole point (never pay for more pixels than
    // asked). A source narrower AND shorter than the target box must come
    // back at its own native size.
    {
        QString smallArchive;
        CHECK(makeCoverCbz(temp.path(), QStringLiteral("small-cover"), QSize(100, 150), &smallArchive),
              "fixture CBZ with a real 100x150 PNG page (smaller than the default box) created");
        const QString id = Colosseum::buildComicCoverId(smallArchive, QStringLiteral("page_000.png"));

        const QImage unsized = provider.requestImage(id, nullptr, QSize());
        CHECK(unsized.size() == QSize(100, 150),
              "a source smaller than the DEFAULT box is served at native size, not upscaled to 240x360");

        const QImage requestedLarger = provider.requestImage(id, nullptr, QSize(400, 600));
        CHECK(requestedLarger.size() == QSize(100, 150),
              "a source smaller than an EXPLICITLY requested box is still served at native size, not upscaled");
    }

    // ── A missing entry resolves to null, no crash ────────────────────────────
    {
        const QString id = Colosseum::buildComicCoverId(archivePath, QStringLiteral("no-such-page.png"));
        QSize reportedSize(999, 999);
        const QImage image = provider.requestImage(id, &reportedSize, QSize(200, 300));
        CHECK(image.isNull(), "a missing entry name resolves to a null image");
        CHECK(reportedSize.isEmpty(), "the out-param size is cleared, not left stale, for a null result");
    }

    // ── A missing archive file resolves to null, no crash ─────────────────────
    {
        const QString id = Colosseum::buildComicCoverId(
            temp.path() + QStringLiteral("/does-not-exist.cbz"), QStringLiteral("page_000.png"));
        const QImage image = provider.requestImage(id, nullptr, QSize(200, 300));
        CHECK(image.isNull(), "a missing archive file resolves to a null image");
    }

    // ── Malformed ids never crash, always resolve null ────────────────────────
    {
        const QString malformed[] = {
            QStringLiteral(""),               // empty
            QStringLiteral("not-a-real-id"),  // no slash
            QStringLiteral("///"),            // slashes but garbage segments
        };
        for (const QString& id : malformed) {
            const QImage image = provider.requestImage(id, nullptr, QSize(200, 300));
            CHECK(image.isNull(), qPrintable(QStringLiteral("malformed id \"%1\" resolves null, not a crash").arg(id)));
        }
    }

    // ── The id round-trips through EXACTLY buildId()'s own encoding ──────────
    // Regression guard: if a future edit swaps the segment order, the
    // separator, or the base64 variant, this is the first thing that breaks --
    // a real path with characters base64's standard alphabet would mangle
    // (into '+'/'/' , which would collide with the '/' segment separator).
    {
        const QString trickyArchive = temp.path() + QStringLiteral("/weird name (v2).cbz");
        QFile::copy(archivePath, trickyArchive);
        const QString id = Colosseum::buildComicCoverId(trickyArchive, QStringLiteral("page_000.png"));
        CHECK(!id.contains(QLatin1Char('+')) && !id.contains(QLatin1Char('%')),
              "buildId() output uses a URL-safe alphabet (base64url, not base64/percent-encoding)");
        const QImage image = provider.requestImage(id, nullptr, QSize(200, 300));
        CHECK(!image.isNull(), "an archive path containing spaces and parentheses round-trips correctly");
    }

    if (g_failures == 0) {
        std::puts("COMIC_COVER_PROVIDER_OK");
        return 0;
    }
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
}
