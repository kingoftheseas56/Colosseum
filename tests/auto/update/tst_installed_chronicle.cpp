// tst_installed_chronicle — Slice 2 of the installed-release chronicle plan.
// Proves InstalledChronicle's trust boundary: it loads only production/test-key-
// signed bundles and rejects every mutation — bad signature, corrupt manifest,
// version mismatch. The loader is pure QtCore + the existing UpdateTrust path;
// no network, no GUI. GUILESS.
//
// Fixtures live under INSTALLED_CHRONICLE_FIXTURES_DIR (baked at configure time,
// house pattern: COLOSSEUM_UPDATE_FIXTURES_DIR). The valid fixture is signed
// with the test private key (counterpart to kUpdateTestPublicKey, which the
// test build selects via COLOSSEUM_UPDATE_TESTING). Mutation cases build their
// own corrupted copies in a QTemporaryDir.

#include "update/InstalledChronicle.h"
#include "update/UpdateVersion.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Colosseum::Update;

class tst_installed_chronicle : public QObject
{
    Q_OBJECT

private:
    static QString fixtures() { return QStringLiteral(INSTALLED_CHRONICLE_FIXTURES_DIR); }

    static bool writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    }

private slots:
    void validBundleLoadsAndExposesFiveHighlights();
    void mutatedManifestIsRejected();
    void mutatedSignatureIsRejected();
    void versionMismatchIsRejected();
    void missingArtworkDoesNotFailLoad();
};

void tst_installed_chronicle::validBundleLoadsAndExposesFiveHighlights()
{
    const auto version = Version::parseCanonical(QStringLiteral("1.1.0"));
    QVERIFY2(version.has_value(), "1.1.0 parses");

    QString error;
    const auto loaded = InstalledChronicle::load(
        fixtures() + QStringLiteral("/manifest.json"),
        fixtures() + QStringLiteral("/manifest.sig"),
        fixtures() + QStringLiteral("/artwork"),
        *version, &error);

    QVERIFY2(loaded.has_value(), qPrintable(QStringLiteral("valid bundle failed to load: ") + error));
    QCOMPARE(loaded->manifest.version.canonical(), QStringLiteral("1.1.0"));
    QCOMPARE(loaded->manifest.tag, QStringLiteral("v1.1.0"));
    QCOMPARE(loaded->manifest.highlights.size(), 5);
    QCOMPARE(loaded->manifest.artwork.size(), 5);

    // The five reference chapters, in order: Reader, Discover, Biblio, Theatre, The house.
    const QStringList titles = {
        QStringLiteral("Reader"), QStringLiteral("Discover"),
        QStringLiteral("Biblio"), QStringLiteral("Theatre"), QStringLiteral("The house"),
    };
    for (int i = 0; i < titles.size(); ++i)
        QCOMPARE(loaded->manifest.highlights.at(i).title, titles.at(i));

    // Artwork root points at the fixture dir and is absolute.
    QVERIFY2(QDir::isAbsolutePath(loaded->artworkRoot), "artwork root must be absolute");
    QVERIFY2(QDir(loaded->artworkRoot).exists(), "artwork root must exist");

    // Each declared artwork file is present on disk (the runtime SHA256 check
    // in highlightMap depends on this; the loader itself does not hash artwork).
    for (const Artwork& artwork : loaded->manifest.artwork) {
        const QString path = QDir(loaded->artworkRoot).filePath(artwork.assetName);
        QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("missing artwork file: ") + path));
    }
}

void tst_installed_chronicle::mutatedManifestIsRejected()
{
    // Flip one byte in the verified manifest; signature must fail.
    const QString originalPath = fixtures() + QStringLiteral("/manifest.json");
    QFile original(originalPath);
    QVERIFY2(original.open(QIODevice::ReadOnly), "fixture manifest opens");
    QByteArray bytes = original.readAll();
    original.close();
    QVERIFY2(!bytes.isEmpty(), "fixture manifest non-empty");
    bytes[bytes.size() / 2] = static_cast<char>(bytes.at(bytes.size() / 2) ^ 0x01);

    QTemporaryDir temp;
    QVERIFY2(temp.isValid(), "temp dir created");
    const QString mutatedPath = QDir(temp.path()).filePath(QStringLiteral("manifest.json"));
    QVERIFY2(writeFile(mutatedPath, bytes), "mutated manifest written");

    const auto version = Version::parseCanonical(QStringLiteral("1.1.0"));
    QVERIFY2(version.has_value(), "1.1.0 parses");

    QString error;
    const auto loaded = InstalledChronicle::load(
        mutatedPath,
        fixtures() + QStringLiteral("/manifest.sig"),
        fixtures() + QStringLiteral("/artwork"),
        *version, &error);

    QVERIFY2(!loaded.has_value(), "mutated manifest must be rejected");
    QVERIFY2(error.contains(QStringLiteral("signature")),
             qPrintable(QStringLiteral("error should cite signature failure: ") + error));
}

void tst_installed_chronicle::mutatedSignatureIsRejected()
{
    // Flip one byte in the signature; verification must fail.
    const QString originalPath = fixtures() + QStringLiteral("/manifest.sig");
    QFile original(originalPath);
    QVERIFY2(original.open(QIODevice::ReadOnly), "fixture signature opens");
    QByteArray bytes = original.readAll();
    original.close();
    QVERIFY2(bytes.size() >= 2, "fixture signature non-trivial");
    bytes[0] = static_cast<char>(bytes.at(0) == '0' ? '1' : '0');

    QTemporaryDir temp;
    QVERIFY2(temp.isValid(), "temp dir created");
    const QString mutatedPath = QDir(temp.path()).filePath(QStringLiteral("manifest.sig"));
    QVERIFY2(writeFile(mutatedPath, bytes), "mutated signature written");

    const auto version = Version::parseCanonical(QStringLiteral("1.1.0"));
    QVERIFY2(version.has_value(), "1.1.0 parses");

    QString error;
    const auto loaded = InstalledChronicle::load(
        fixtures() + QStringLiteral("/manifest.json"),
        mutatedPath,
        fixtures() + QStringLiteral("/artwork"),
        *version, &error);

    QVERIFY2(!loaded.has_value(), "mutated signature must be rejected");
    QVERIFY2(error.contains(QStringLiteral("signature")),
             qPrintable(QStringLiteral("error should cite signature failure: ") + error));
}

void tst_installed_chronicle::versionMismatchIsRejected()
{
    // The bundle is signed for 1.1.0; ask for 1.2.0. The chronicle must belong
    // to the installed release — a version drift is rejected even though the
    // signature itself is valid.
    const auto wrong = Version::parseCanonical(QStringLiteral("1.2.0"));
    QVERIFY2(wrong.has_value(), "1.2.0 parses");

    QString error;
    const auto loaded = InstalledChronicle::load(
        fixtures() + QStringLiteral("/manifest.json"),
        fixtures() + QStringLiteral("/manifest.sig"),
        fixtures() + QStringLiteral("/artwork"),
        *wrong, &error);

    QVERIFY2(!loaded.has_value(), "version mismatch must be rejected");
    QVERIFY2(error.contains(QStringLiteral("version")),
             qPrintable(QStringLiteral("error should cite version mismatch: ") + error));
}

void tst_installed_chronicle::missingArtworkDoesNotFailLoad()
{
    // The loader does not hash artwork (highlightMap does, at runtime). Point
    // artworkRoot at a non-existent dir: load must still succeed — the manifest
    // is verified and well-formed; the runtime per-chapter fallback handles
    // missing files. This proves the loader's contract is manifest-trust, not
    // artwork-presence.
    const auto version = Version::parseCanonical(QStringLiteral("1.1.0"));
    QVERIFY2(version.has_value(), "1.1.0 parses");

    QString error;
    const auto loaded = InstalledChronicle::load(
        fixtures() + QStringLiteral("/manifest.json"),
        fixtures() + QStringLiteral("/manifest.sig"),
        fixtures() + QStringLiteral("/nonexistent-artwork-dir"),
        *version, &error);

    QVERIFY2(loaded.has_value(), qPrintable(QStringLiteral("missing artwork dir must not fail load: ") + error));
    QCOMPARE(loaded->manifest.highlights.size(), 5);
}

QTEST_GUILESS_MAIN(tst_installed_chronicle)
#include "tst_installed_chronicle.moc"
