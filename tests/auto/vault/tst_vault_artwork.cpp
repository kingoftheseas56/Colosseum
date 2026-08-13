// tst_vault_artwork — Vault browse-artwork execution plan, Slice 1 + Slice 2 + Slice 3
// part 1 (2026-08-13).
//
// Slice 1 proves VaultThumbnailer, the persistent frame-grab producer: a still is
// written to its keyed cache file from a genuinely decodable fixture; a second request
// for the same (path,size,mtimeMs) key is an honest cache hit — the file is left
// untouched and no second ffmpeg is spawned; a missing or corrupt input yields no file
// and an honest empty/false result, never a hang. Drives the SAME tiny.mp4 fixture
// vault_admission_probe_harness already proves decodes a real frame (dwidth > 0) — no
// new fixture invented here. GUILESS; real ffmpeg (MpvItem::findFfmpeg), QTemporaryDir.
//
// Slice 2 proves VaultPosterFetcher, the canonical poster fetcher + cache: bytes from a
// poster URL land at the identity-id-keyed cache path; a second request for the same id
// is an honest cache hit (no re-download, file untouched); a file:// URL to a
// nonexistent path yields no file and an honest empty result — no hang. NO LIVE NETWORK
// in the gate: a small fixture image is written into the test's own QTemporaryDir and
// its file:// URL stands in for the real https://live.metahub.space poster URL.
//
// Slice 3 part 1 proves VaultArtworkResolver, the pure ladder that composes the two
// producers above: precedence (local ref beats a cached poster beats a cached thumb
// beats the typographic ""), an async kick that later fires artResolved(rowKey) once
// the kicked producer lands its file, and a container row (no video, no cached poster)
// resolving to "" rather than a spurious ref. Real VaultThumbnailer + VaultPosterFetcher
// against a shared QTemporaryDir cache — same fixtures, same GUILESS discipline.

#include "engine/VaultArtworkResolver.h"
#include "engine/VaultPosterFetcher.h"
#include "engine/VaultThumbnailer.h"

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

class tst_vault_artwork : public QObject
{
    Q_OBJECT

private:
    static QString mediaFixture(const QString& name)
    {
        return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/") + name;
    }
    // The exact fixture vault_admission_probe_harness proves ADMITS with a decoded
    // frame (dwidth > 0) via the real libmpv probe — reused here rather than inventing
    // a second "guaranteed decodable" fixture. It is intentionally tiny (1s, 64x64,
    // 8fps), which is exactly why VaultThumbnailer's offset-clamping (never seek past
    // a short clip's own duration) matters and is exercised by every case below.
    static QString tinyMp4() { return mediaFixture(QStringLiteral("tiny.mp4")); }
    static double tinyMp4DurationSec() { return 1.0; }

    // Writes a small fixture "poster" file (arbitrary bytes — content is irrelevant to
    // VaultPosterFetcher, which is a byte-mover, never an image decoder) into `dir` and
    // returns its file:// URL, standing in for a real metahub poster URL with zero
    // network involved.
    static QString writeFixturePoster(const QDir& dir, const QString& name = QStringLiteral("poster.jpg"))
    {
        const QString path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write("fake-poster-bytes-not-a-real-jpeg");
        f.close();
        return QUrl::fromLocalFile(path).toString();
    }

private slots:
    void still_is_produced_at_keyed_cache_path();
    void second_request_for_same_key_is_a_cache_hit();
    void missing_input_yields_no_file_and_no_hang();
    void corrupt_input_yields_no_file_and_no_hang();

    void poster_bytes_land_at_id_keyed_cache_path();
    void second_poster_request_for_same_id_is_a_cache_hit();
    void missing_poster_url_yields_no_file_and_no_hang();

    void ladder_precedence_local_then_poster_then_thumb_then_typographic();
    void async_video_row_kicks_thumb_fetch_and_signals_row_ready();
    void container_row_without_video_or_cached_poster_resolves_to_typographic();
};

void tst_vault_artwork::still_is_produced_at_keyed_cache_path()
{
    QVERIFY2(QFileInfo::exists(tinyMp4()), "fixture tiny.mp4 must exist (shared with vault_admission_probe_harness)");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Baseline (falsifiability): before any request, the thumbs cache holds nothing
    // for this fixture — "it now produces one" has something to be measured against.
    QCOMPARE(QDir(tmp.filePath(QStringLiteral("thumbs"))).entryList(QDir::Files).size(), 0);

    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 1;

    QCOMPARE(thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs), QString()); // miss before any grab

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    const QString immediate = thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec());
    QVERIFY2(immediate.isEmpty(), "a fresh grab must not return a path synchronously");

    QVERIFY2(ready.wait(15000), "thumbReady must fire once ffmpeg's grab completes");
    QCOMPARE(ready.count(), 1);
    const QString grabbedPath = ready.at(0).at(1).toString();
    QVERIFY2(QFileInfo::exists(grabbedPath), "the still must land on disk at the emitted path");
    QVERIFY(QFileInfo(grabbedPath).size() > 0);
    QVERIFY2(grabbedPath.startsWith(tmp.filePath(QStringLiteral("thumbs"))),
              "the still must live under the injected cacheDir's thumbs/ subdir");

    QCOMPARE(QDir(tmp.filePath(QStringLiteral("thumbs"))).entryList(QDir::Files).size(), 1);
    QCOMPARE(thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs), grabbedPath); // now a sync hit
}

void tst_vault_artwork::second_request_for_same_key_is_a_cache_hit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 1;

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec()).isEmpty());
    QVERIFY(ready.wait(15000));
    const QString grabbedPath = ready.at(0).at(1).toString();
    QVERIFY(QFileInfo::exists(grabbedPath));
    const QDateTime firstMtime = QFileInfo(grabbedPath).lastModified();

    // Second call for the identical key: must be a synchronous hit that spawns
    // NOTHING — checked immediately, before the event loop even turns, so this
    // assertion can only pass if requestThumb never reached startJob().
    ready.clear();
    const QString hitPath = thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec());
    QCOMPARE(hitPath, grabbedPath);
    QCOMPARE(thumbnailer.inFlightCount(), 0); // <- negative control target: disable the hit
                                               //    short-circuit and this goes red (becomes 1)

    // No fresh signal, and the file itself is untouched (mtime unchanged) — a
    // wrongly-spawned second process would eventually rewrite the file.
    QCOMPARE(ready.count(), 0);
    QVERIFY2(!ready.wait(3000), "a cache hit must never trigger a second thumbReady");
    QCOMPARE(QFileInfo(grabbedPath).lastModified(), firstMtime);
}

void tst_vault_artwork::missing_input_yields_no_file_and_no_hang()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const QString missing = tmp.filePath(QStringLiteral("does-not-exist.mp4"));

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(missing, 123, 456).isEmpty());
    QVERIFY2(!ready.wait(5000), "a nonexistent input must never produce a still");

    QCOMPARE(thumbnailer.cachedThumbPath(missing, 123, 456), QString());
    QCOMPARE(thumbnailer.inFlightCount(), 0); // bounded-timeout cleanup left no zombie job
}

void tst_vault_artwork::corrupt_input_yields_no_file_and_no_hang()
{
    const QString corrupt = mediaFixture(QStringLiteral("not-a-video.mp4")); // junk bytes, real fixture
    QVERIFY2(QFileInfo::exists(corrupt), "fixture not-a-video.mp4 must exist (shared with the admission probe test)");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultThumbnailer thumbnailer(tmp.path());
    const qint64 size = QFileInfo(corrupt).size();

    QSignalSpy ready(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(corrupt, size, 1).isEmpty());
    QVERIFY2(!ready.wait(5000), "corrupt/undecodable bytes must never produce a still");

    QCOMPARE(thumbnailer.cachedThumbPath(corrupt, size, 1), QString());
    QCOMPARE(thumbnailer.inFlightCount(), 0);
}

// ── VaultPosterFetcher (Slice 2) ────────────────────────────────────────────────

void tst_vault_artwork::poster_bytes_land_at_id_keyed_cache_path()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Baseline (falsifiability): before any request, the posters cache holds nothing
    // for this fixture id — "it now produces one" has something to be measured against.
    QCOMPARE(QDir(tmp.filePath(QStringLiteral("posters"))).entryList(QDir::Files).size(), 0);

    const QString posterUrl = writeFixturePoster(QDir(tmp.path()));
    QVERIFY2(!posterUrl.isEmpty(), "failed to stage the fixture poster file");

    VaultPosterFetcher fetcher(tmp.path());
    const QString identityId = QStringLiteral("imdb:tt0111161");

    QCOMPARE(fetcher.cachedPosterPath(identityId), QString()); // miss before any fetch

    QSignalSpy ready(&fetcher, &VaultPosterFetcher::posterReady);
    const QString immediate = fetcher.requestPoster(identityId, posterUrl);
    QVERIFY2(immediate.isEmpty(), "a fresh fetch must not return a path synchronously");

    QVERIFY2(ready.wait(10000), "posterReady must fire once the file:// download completes");
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(0).toString(), identityId);
    const QString fetchedPath = ready.at(0).at(1).toString();
    QVERIFY2(QFileInfo::exists(fetchedPath), "the poster must land on disk at the emitted path");
    QVERIFY(QFileInfo(fetchedPath).size() > 0);
    QVERIFY2(fetchedPath.startsWith(tmp.filePath(QStringLiteral("posters"))),
              "the poster must live under the injected cacheDir's posters/ subdir");

    QCOMPARE(QDir(tmp.filePath(QStringLiteral("posters"))).entryList(QDir::Files).size(), 1);
    QCOMPARE(fetcher.cachedPosterPath(identityId), fetchedPath); // now a sync hit
}

void tst_vault_artwork::second_poster_request_for_same_id_is_a_cache_hit()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString posterUrl = writeFixturePoster(QDir(tmp.path()));
    QVERIFY(!posterUrl.isEmpty());

    VaultPosterFetcher fetcher(tmp.path());
    const QString identityId = QStringLiteral("imdb:tt0068646");

    QSignalSpy ready(&fetcher, &VaultPosterFetcher::posterReady);
    QVERIFY(fetcher.requestPoster(identityId, posterUrl).isEmpty());
    QVERIFY(ready.wait(10000));
    const QString fetchedPath = ready.at(0).at(1).toString();
    QVERIFY(QFileInfo::exists(fetchedPath));
    const QDateTime firstMtime = QFileInfo(fetchedPath).lastModified();

    // Second call for the identical id: must be a synchronous hit that starts NOTHING
    // — checked immediately, before the event loop even turns, so this assertion can
    // only pass if requestPoster never reached the download branch.
    ready.clear();
    const QString hitPath = fetcher.requestPoster(identityId, posterUrl);
    QCOMPARE(hitPath, fetchedPath);
    QCOMPARE(fetcher.inFlightCount(), 0); // <- negative control target: disable the hit
                                           //    short-circuit and this goes red (becomes 1)

    // No fresh signal, and the file itself is untouched (mtime unchanged) — a
    // wrongly-spawned second download would eventually rewrite the file.
    QCOMPARE(ready.count(), 0);
    QVERIFY2(!ready.wait(3000), "a cache hit must never trigger a second posterReady");
    QCOMPARE(QFileInfo(fetchedPath).lastModified(), firstMtime);
}

void tst_vault_artwork::missing_poster_url_yields_no_file_and_no_hang()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    VaultPosterFetcher fetcher(tmp.path());
    const QString identityId = QStringLiteral("imdb:tt9999999");
    const QString missingUrl =
        QUrl::fromLocalFile(tmp.filePath(QStringLiteral("does-not-exist.jpg"))).toString();

    QSignalSpy ready(&fetcher, &VaultPosterFetcher::posterReady);
    QVERIFY(fetcher.requestPoster(identityId, missingUrl).isEmpty());
    QVERIFY2(!ready.wait(5000), "a nonexistent file:// URL must never produce a poster");

    QCOMPARE(fetcher.cachedPosterPath(identityId), QString());
    QCOMPARE(fetcher.inFlightCount(), 0); // no zombie job left behind
    QCOMPARE(QDir(tmp.filePath(QStringLiteral("posters"))).entryList(QDir::Files).size(), 0);
}

// ── VaultArtworkResolver (Slice 3 part 1) ───────────────────────────────────────

// Precedence, all four rungs in one row so each step's "beats" claim is measured
// against the SAME row rather than four separately-seeded ones: rung 2 (local ref)
// beats rung 3 (cached poster) beats rung 4 (cached thumb) beats rung 5 (typographic).
// This is also the negative control's target: inverting the rung 3/4 check order
// makes the second QCOMPARE below (poster-beats-thumb) go red.
void tst_vault_artwork::ladder_precedence_local_then_poster_then_thumb_then_typographic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultThumbnailer thumbnailer(tmp.path());
    VaultPosterFetcher fetcher(tmp.path());
    VaultArtworkResolver resolver(&thumbnailer, &fetcher);

    // Seed a real cached thumb (genuine ffmpeg grab against tiny.mp4).
    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 1;
    QSignalSpy thumbReady(&thumbnailer, &VaultThumbnailer::thumbReady);
    QVERIFY(thumbnailer.requestThumb(tinyMp4(), size, mtimeMs, tinyMp4DurationSec()).isEmpty());
    QVERIFY2(thumbReady.wait(15000), "seeding a cached thumb requires a real ffmpeg grab to finish");
    const QString cachedThumb = thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs);
    QVERIFY(!cachedThumb.isEmpty());

    // Seed a cached poster (file:// fixture, no live network).
    const QString posterUrl = writeFixturePoster(QDir(tmp.path()));
    QVERIFY2(!posterUrl.isEmpty(), "failed to stage the fixture poster file");
    const QString identityId = QStringLiteral("imdb:tt0111161");
    QSignalSpy posterReady(&fetcher, &VaultPosterFetcher::posterReady);
    QVERIFY(fetcher.requestPoster(identityId, posterUrl).isEmpty());
    QVERIFY2(posterReady.wait(10000), "seeding a cached poster requires the file:// fetch to finish");
    const QString cachedPoster = fetcher.cachedPosterPath(identityId);
    QVERIFY(!cachedPoster.isEmpty());

    VaultArtworkResolver::RowFacts facts;
    facts.rowKey = QStringLiteral("row-precedence");
    facts.kind = QStringLiteral("video");
    facts.path = tinyMp4();
    facts.size = size;
    facts.mtimeMs = mtimeMs;
    facts.durationSec = tinyMp4DurationSec();
    facts.identityId = identityId;
    facts.posterUrl = posterUrl;
    facts.localRef = QStringLiteral("file:///already/adopted/local-poster.jpg");

    // Rung 2 beats rung 3 beats rung 4: the local ref wins even though a cached
    // poster AND a cached thumb both also exist for this row.
    QCOMPARE(resolver.resolve(facts), facts.localRef);

    // Remove the local ref: the cached poster (rung 3) now wins over the cached
    // thumb (rung 4).
    facts.localRef.clear();
    QCOMPARE(resolver.resolve(facts), cachedPoster);

    // Remove the identity/poster: the cached thumb (rung 4) wins.
    facts.identityId.clear();
    facts.posterUrl.clear();
    QCOMPARE(resolver.resolve(facts), cachedThumb);

    // Remove the video facts too: nothing left to resolve — typographic fallback.
    facts.kind.clear();
    facts.path.clear();
    QCOMPARE(resolver.resolve(facts), QString());
}

void tst_vault_artwork::async_video_row_kicks_thumb_fetch_and_signals_row_ready()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultThumbnailer thumbnailer(tmp.path());
    VaultPosterFetcher fetcher(tmp.path());
    VaultArtworkResolver resolver(&thumbnailer, &fetcher);

    const qint64 size = QFileInfo(tinyMp4()).size();
    const qint64 mtimeMs = 2; // distinct key from the precedence test's seeded thumb

    VaultArtworkResolver::RowFacts facts;
    facts.rowKey = QStringLiteral("row-async");
    facts.kind = QStringLiteral("video");
    facts.path = tinyMp4();
    facts.size = size;
    facts.mtimeMs = mtimeMs;
    facts.durationSec = tinyMp4DurationSec();

    QCOMPARE(thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs), QString()); // baseline: no cache yet

    QSignalSpy resolved(&resolver, &VaultArtworkResolver::artResolved);
    QCOMPARE(resolver.resolve(facts), QString()); // no cached art yet: "" now, but a grab was kicked

    QVERIFY2(resolved.wait(15000), "artResolved must fire once the kicked thumb grab completes");
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.at(0).at(0).toString(), facts.rowKey);

    // A second resolve for the same row now returns the freshly cached thumb.
    const QString second = resolver.resolve(facts);
    QVERIFY(!second.isEmpty());
    QCOMPARE(second, thumbnailer.cachedThumbPath(tinyMp4(), size, mtimeMs));
}

void tst_vault_artwork::container_row_without_video_or_cached_poster_resolves_to_typographic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    VaultThumbnailer thumbnailer(tmp.path());
    VaultPosterFetcher fetcher(tmp.path());
    VaultArtworkResolver resolver(&thumbnailer, &fetcher);

    // A Folder/Show/Season node has no own video and (here) no adopted identity yet
    // either — never a spurious ref, and never a fetch kicked for a row with nothing
    // to fetch.
    VaultArtworkResolver::RowFacts facts;
    facts.rowKey = QStringLiteral("row-folder");
    facts.kind = QStringLiteral("folder");

    QSignalSpy resolved(&resolver, &VaultArtworkResolver::artResolved);
    QCOMPARE(resolver.resolve(facts), QString());
    QVERIFY2(!resolved.wait(2000), "a container row must never spuriously resolve or kick a fetch");
    QCOMPARE(thumbnailer.inFlightCount(), 0);
    QCOMPARE(fetcher.inFlightCount(), 0);
}

QTEST_GUILESS_MAIN(tst_vault_artwork)
#include "tst_vault_artwork.moc"
