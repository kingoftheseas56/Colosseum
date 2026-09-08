#include "../native/bootstrap/StartupLayout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

static void writeRuntimeManifest(const QString& root, const QStringList& files)
{
    QByteArray material;
    QByteArray lines = QByteArrayLiteral("schema=1\n");
    for (const QString& relative : files) {
        QFile file(QDir(root).filePath(relative));
        file.open(QIODevice::ReadOnly);
        const QByteArray hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
        material += relative.toUtf8() + '\n' + hash + '\n';
        lines += QByteArrayLiteral("file=") + relative.toUtf8() + '\t' + hash + '\n';
    }
    const QByteArray bundleHash = QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
    writeFile(QDir(root).filePath(QStringLiteral("runtime-files.manifest")),
              QByteArrayLiteral("schema=1\nbundleSha256=") + bundleHash + '\n' + lines.mid(9));
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

    const QString originalCwd = QDir::currentPath();
    const QString outOfTreeAppDir = QDir(temp.path()).filePath(QStringLiteral("build/colosseum-linux"));
    QDir().mkpath(outOfTreeAppDir);
    writeManifest(QDir(outOfTreeAppDir).filePath(QStringLiteral("qml-build.manifest")), fingerprint);
    CHECK(QDir::setCurrent(repoRoot), "out-of-tree launch working directory selected");
    const auto outOfTree = resolveStartupLayout(
        {QStringLiteral("colosseum")}, outOfTreeAppDir, &error);
    CHECK(outOfTree.has_value(), "out-of-tree Linux launch resolves from source working directory");
    CHECK(outOfTree && outOfTree->qmlPath == QFileInfo(mainQml).absoluteFilePath(),
          "out-of-tree Linux launch uses source-shaped QML tree");
    CHECK(outOfTree && outOfTree->resourceRoot == QFileInfo(repoRoot).absoluteFilePath(),
          "out-of-tree Linux launch retains source-shaped resource root");
    CHECK(QDir::setCurrent(originalCwd), "working directory restored after out-of-tree proof");

    const QString packagedRoot = QDir(temp.path()).filePath(QStringLiteral("packaged-runtime"));
    const QString packagedQml = QDir(packagedRoot).filePath(QStringLiteral("qml"));
    writeFile(QDir(packagedQml).filePath(QStringLiteral("Main.qml")),
              "import QtQuick\nQtObject { property string bundled: \"yes\" }\n");
    writeFile(QDir(packagedRoot).filePath(QStringLiteral("assets/icon.txt")), "icon\n");
    writeFile(QDir(packagedRoot).filePath(QStringLiteral("resources/data.json")), "{}\n");
    const QString packagedFingerprint = qmlTreeFingerprint(packagedQml, &error);
    writeManifest(QDir(packagedRoot).filePath(QStringLiteral("qml-build.manifest")),
                  packagedFingerprint);
    writeRuntimeManifest(packagedRoot,
                         {QStringLiteral("assets/icon.txt"),
                          QStringLiteral("qml/Main.qml"),
                          QStringLiteral("qml-build.manifest"),
                          QStringLiteral("resources/data.json")});
    const QString bundleCache = QDir(temp.path()).filePath(QStringLiteral("runtime-cache"));
    const auto materialized = materializeRuntimeBundle(packagedRoot, bundleCache, &error);
    CHECK(materialized.has_value(), "packaged runtime materializes into writable cache");
    CHECK(materialized && QFileInfo(QDir(*materialized).filePath(QStringLiteral("qml/Main.qml"))).isFile(),
          "materialized runtime contains Main.qml");
    CHECK(materialized && QFileInfo(QDir(*materialized).filePath(QStringLiteral("assets/icon.txt"))).isFile(),
          "materialized runtime preserves sibling assets");
    CHECK(materialized && QFileInfo(QDir(*materialized).filePath(QStringLiteral("resources/data.json"))).isFile(),
          "materialized runtime preserves sibling resources");

    const QString packagedAppDir = QDir(temp.path()).filePath(QStringLiteral("apk/lib/arm64"));
    QDir().mkpath(packagedAppDir);
    CHECK(materialized && QDir::setCurrent(*materialized), "materialized runtime selected as working root");
    const auto packaged = resolveStartupLayout(
        {QStringLiteral("libcolosseum.so")}, packagedAppDir, &error,
        materialized ? QDir(*materialized).filePath(QStringLiteral("qml-build.manifest")) : QString());
    CHECK(packaged.has_value(), "packaged runtime resolves with extracted build manifest");
    CHECK(packaged && packaged->resourceRoot == QFileInfo(*materialized).absoluteFilePath(),
          "packaged runtime becomes the source-shaped resource root");
    CHECK(QDir::setCurrent(originalCwd), "working directory restored after packaged-runtime proof");

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
