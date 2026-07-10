// comic_dls_parse_harness.cpp — parseDlsLinks contract: signed links only,
// DOWNLOAD NOW leads, ad-gate excluded, pixeldrain DROPPED (blocked host,
// spec 2026-07-10). Verdict rides the exit code.
#include "engine/ComicDlsParse.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    // 1) signed DOWNLOAD NOW leads; bare ad-gate /dls/<token>/ (no ":sig") excluded
    const QByteArray post1 = R"(
        <a href="https://getcomics.org/dls/abc123/">bare ad gate</a>
        <a title="DOWNLOAD NOW" class="aio-red" href="https://getcomics.org/dls/payload:sig==">DOWNLOAD NOW</a>
        <a title="MAIN SERVER" href="https://getcomics.org/dls/payload2:sig2==">Mirror</a>
    )";
    const QStringList r1 = parseDlsLinks(post1);
    require(r1.size() == 2, "signed links kept, ad-gate excluded");
    require(r1.first().contains("payload:sig=="), "DOWNLOAD NOW leads");

    // 2) pixeldrain label in the anchor TEXT -> dropped entirely (not just deprioritized)
    const QByteArray post2 = R"(
        <a title="DOWNLOAD NOW" href="https://getcomics.org/dls/main:sig==">DOWNLOAD NOW</a>
        <a href="https://getcomics.org/dls/pd:sig2==">PIXELDRAIN</a>
    )";
    const QStringList r2 = parseDlsLinks(post2);
    require(r2.size() == 1, "pixeldrain-labeled anchor dropped");
    require(r2.first().contains("main:sig=="), "main link survives");

    // 3) pixeldrain in the anchor ATTRIBUTES -> dropped too
    const QByteArray post3 = R"(
        <a title="PixelDrain mirror" href="https://getcomics.org/dls/pd2:sig3==">Mirror</a>
        <a title="MAIN SERVER" href="https://getcomics.org/dls/ok:sig4==">Mirror</a>
    )";
    const QStringList r3 = parseDlsLinks(post3);
    require(r3.size() == 1 && r3.first().contains("ok:sig4=="), "attr-labeled pixeldrain dropped");

    // 4) duplicates collapse
    const QByteArray post4 = R"(
        <a href="https://getcomics.org/dls/same:sig==">A</a>
        <a href="https://getcomics.org/dls/same:sig==">B</a>
    )";
    require(parseDlsLinks(post4).size() == 1, "duplicate hrefs collapse");

    std::cout << "comic_dls_parse_harness PASS\n";
    return 0;
}
