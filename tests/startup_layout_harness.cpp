#include "../native/bootstrap/StartupLayout.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (0)

static void writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(bytes);
}
static void writeManifest(const QString& path, const QString& fingerprint)
{
    writeFile(path,
              QByteArrayLiteral("schema=1\nqmlTreeSha256=")
                  + fingerprint.toLatin1() + QByteArrayLiteral("\n"));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--fingerprint")) {
        QString error;
        const QString fingerprint = qmlTreeFingerprint(QString::fromLocal8Bit(argv[2]), &error);
        if (fingerprint.isEmpty()) {
            std::fprintf(stderr, "%s\n", qUtf8Printable(error));
            return 2;
        }
        std::printf("%s\n", qUtf8Printable(fingerprint));
        return 0;
    }

    QTemporaryDir temp;
    CHECK(temp.isValid(), "temporary root created");

    const QString repoRoot = QDir(temp.path()).filePath(QStringLiteral("repo"));
    const QString appDir = QDir(repoRoot).filePath(QStringLiteral("native/build-msvc"));
    const QString qmlDir = QDir(repoRoot).filePath(QStringLiteral("qml"));
    const QString mainQml = QDir(qmlDir).filePath(QStringLiteral("Main.qml"));
    const QString manifest = QDir(appDir).filePath(QStringLiteral("qml-build.manifest"));
    writeFile(mainQml, "import QtQuick\nQtObject {}\n");
    writeFile(QDir(qmlDir).filePath(QStringLiteral("Catalog.js")), ".pragma library\n");

    QString error;
    const QString fingerprint = qmlTreeFingerprint(qmlDir, &error);
    CHECK(!fingerprint.isEmpty(), "QML tree fingerprint is produced");
    CHECK(error.isEmpty(), "QML fingerprint has no error");
    writeManifest(manifest, fingerprint);

    const auto normal = resolveStartupLayout({QStringLiteral("colosseum.exe")}, appDir, &error);
    CHECK(normal.has_value(), "normal launch resolves against matching manifest");
    CHECK(normal && normal->qmlPath == QFileInfo(mainQml).absoluteFilePath(),
          "normal launch uses source-shaped QML tree");
    CHECK(normal && normal->resourceRoot == QFileInfo(repoRoot).absoluteFilePath(),
          "normal launch retains source-shaped resource root");

    const auto flagged = resolveStartupLayout(
        {QStringLiteral("colosseum.exe"), QStringLiteral("--update-result=success")}, appDir, &error);
    CHECK(flagged.has_value(), "flag-only updater launch resolves");
    CHECK(flagged && !flagged->qmlOverride, "flag-only updater launch is not QML override");

    const QString overridePath = QDir(temp.path()).filePath(QStringLiteral("probe.qml"));
    writeFile(overridePath, "import QtQuick\nQtObject {}\n");
    const auto overridden = resolveStartupLayout(
        {QStringLiteral("colosseum.exe"), overridePath}, appDir, &error);
    CHECK(overridden.has_value(), "explicit QML override resolves");
    CHECK(overridden && overridden->qmlOverride, "explicit QML override is marked");
    CHECK(overridden && overridden->qmlPath == QFileInfo(overridePath).absoluteFilePath(),
          "explicit QML override wins over build manifest");

    writeFile(mainQml, "import QtQuick\nQtObject { property int changed: 1 }\n");
    const auto mismatched = resolveStartupLayout({QStringLiteral("colosseum.exe")}, appDir, &error);
    CHECK(!mismatched.has_value(), "normal launch rejects QML changed after native build");
    CHECK(error.startsWith(QStringLiteral("qml_build_mismatch:")),
          "QML/native mismatch has deterministic error");

    QFile::remove(manifest);
    const auto missing = resolveStartupLayout({QStringLiteral("colosseum.exe")}, appDir, &error);
    CHECK(!missing.has_value(), "normal launch fails closed without build manifest");
    CHECK(error.startsWith(QStringLiteral("qml_build_manifest_missing:")),
          "missing build manifest has deterministic error");

    std::printf(fails ? "FAILS: %d\n" : "startup_layout_harness: ALL PASS\n", fails);
    return fails;
}
