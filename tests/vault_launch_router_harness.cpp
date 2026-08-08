// vault_launch_router_harness — Slice 7. Drives LocalLaunch (the launch router)
// and VaultPageStore (the comic-reader adapter) over real fixtures, proving:
//   1. a valid CBZ routes to comic + accepted + carries a vault id;
//   2. a corrupt CBZ is REJECTED as corrupt with NO vault id (no session);
//   3. a valid MP4 routes to video + accepted (real admission probe);
//   4. non-video bytes named .mp4 are REJECTED no-decoder;
//   5. an epub routes to book + accepted (Reader 2 validates at open);
//   6. a .png is unknown + unsupported;
//   7. a missing file is not-found;
//   8. VaultPageStore returns the reader's [{index,archive,entry,group}]
//      descriptors in natural reading order.
//
// House contract: prints VAULT_LAUNCH_ROUTER_OK on success; "FAIL: <msg>" per
// failure + exit(1). Links real libmpv (video validation) + CbzArchive; headless.

#include "engine/LocalLaunch.h"
#include "engine/VaultPageStore.h"

#include <QCoreApplication>
#include <QString>
#include <QVariantMap>

#include <cstdio>
#include <cstdlib>

namespace {
int g_fails = 0;
void check(bool ok, const char* msg)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString tk = QStringLiteral(TANKOBAN_FIXTURES_DIR);
    const QString vx = QStringLiteral(VAULT_FIXTURES_DIR);

    using Family = LocalLaunch::Family;
    using Reject = LocalLaunch::Reject;

    const auto comic = LocalLaunch::route(tk + QStringLiteral("/tiny-volume.cbz"));
    check(comic.family == Family::Comic && comic.accepted, "CBZ must route comic + accepted");
    check(comic.vaultId.startsWith(QStringLiteral("vault:")), "accepted comic must carry a vault id");

    const auto corrupt = LocalLaunch::route(vx + QStringLiteral("/corrupt/bad.cbz"));
    check(corrupt.family == Family::Comic && !corrupt.accepted && corrupt.reject == Reject::Corrupt,
          "corrupt CBZ must be REJECTED as corrupt");
    check(corrupt.vaultId.isEmpty(), "rejected file must carry NO vault id (no session)");

    const auto video = LocalLaunch::route(vx + QStringLiteral("/media/tiny.mp4"));
    check(video.family == Family::Video && video.accepted, "valid MP4 must route video + accepted");

    const auto notVideo = LocalLaunch::route(vx + QStringLiteral("/media/not-a-video.mp4"));
    check(notVideo.family == Family::Video && !notVideo.accepted && notVideo.reject == Reject::NoDecoder,
          "non-video .mp4 must be REJECTED no-decoder");

    const auto book = LocalLaunch::route(vx + QStringLiteral("/mixed-root/Dune/Dune.epub"));
    check(book.family == Family::Book && book.accepted, "epub must route book + accepted");

    const auto unknown = LocalLaunch::route(vx + QStringLiteral("/media/thumb.png"));
    check(unknown.family == Family::Unknown && !unknown.accepted && unknown.reject == Reject::Unsupported,
          "png must be unknown + unsupported");

    const auto missing = LocalLaunch::route(vx + QStringLiteral("/does-not-exist.mp4"));
    check(missing.reject == Reject::NotFound, "missing file must be not-found");

    VaultPageStore store;
    const QVariantList pages = store.localPages(tk + QStringLiteral("/tiny-volume.cbz"));
    check(pages.size() == 3, "page store must list 3 pages");
    if (pages.size() == 3) {
        const QVariantMap p0 = pages.at(0).toMap();
        check(p0.value(QStringLiteral("index")).toInt() == 0, "page 0 index == 0");
        check(p0.value(QStringLiteral("archive")).toString().endsWith(QStringLiteral("tiny-volume.cbz")),
              "page 0 archive is the CBZ path");
        check(p0.value(QStringLiteral("entry")).toString() == QStringLiteral("001.png"),
              "page 0 entry is natural-first (001.png)");
        check(p0.value(QStringLiteral("group")).toInt() == 0, "page 0 group == 0");
    }

    if (g_fails == 0) {
        std::printf("VAULT_LAUNCH_ROUTER_OK\n");
        return 0;
    }
    std::fprintf(stderr, "VAULT_LAUNCH_ROUTER FAILED (%d checks)\n", g_fails);
    return 1;
}
