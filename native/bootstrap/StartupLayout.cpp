#include "bootstrap/StartupLayout.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace {

void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

QString manifestValue(const QByteArray& bytes, const QByteArray& key)
{
    const QList<QByteArray> lines = bytes.split('\n');
    const QByteArray prefix = key + '=';
    for (const QByteArray& line : lines) {
        if (line.startsWith(prefix))
            return QString::fromLatin1(line.mid(prefix.size()).trimmed());
    }
    return {};
}

bool validFingerprint(const QString& value)
{
    if (value.size() != 64)
        return false;
    for (const QChar c : value) {
        const ushort u = c.unicode();
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F')))
            return false;
    }
    return true;
}

} // namespace

QString qmlTreeFingerprint(const QString& qmlRoot, QString* error)
{
    if (error)
        error->clear();

    const QDir root(qmlRoot);
    if (!root.exists()) {
        setError(error, QStringLiteral("qml_root_missing: ") + qmlRoot);
        return {};
    }

    QStringList relativeFiles;
    QDirIterator it(root.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        relativeFiles.push_back(root.relativeFilePath(it.filePath()).replace('\\', '/'));
    }
    std::sort(relativeFiles.begin(), relativeFiles.end());

    if (relativeFiles.isEmpty()) {
        setError(error, QStringLiteral("qml_root_empty: ") + root.absolutePath());
        return {};
    }

    QByteArray material;
    for (const QString& relative : relativeFiles) {
        QFile file(root.filePath(relative));
        if (!file.open(QIODevice::ReadOnly)) {
            setError(error, QStringLiteral("qml_file_unreadable: ") + file.fileName());
            return {};
        }
        const QByteArray fileHash =
            QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
        material += relative.toUtf8();
        material += '\n';
        material += fileHash;
        material += '\n';
    }

    return QString::fromLatin1(
        QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

std::optional<StartupLayout> resolveStartupLayout(const QStringList& arguments,
                                                  const QString& applicationDirPath,
                                                  QString* error)
{
    if (error)
        error->clear();

    const bool qmlOverride = arguments.size() > 1
                             && !arguments.at(1).startsWith(QLatin1Char('-'));
    if (qmlOverride) {
        const QString raw = arguments.at(1);
        const QString absolute = QFileInfo(raw).isAbsolute()
                                     ? QFileInfo(raw).absoluteFilePath()
                                     : QFileInfo(QDir::current().absoluteFilePath(raw)).absoluteFilePath();
        const QFileInfo qmlInfo(absolute);
        if (!qmlInfo.isFile()) {
            setError(error, QStringLiteral("qml_override_missing: ") + absolute);
            return std::nullopt;
        }

        StartupLayout layout;
        layout.qmlPath = qmlInfo.absoluteFilePath();
        layout.qmlOverride = true;
        return layout;
    }

    QDir resourceRoot(applicationDirPath);
    const bool ancestorRootResolved = resourceRoot.cdUp() && resourceRoot.cdUp();
    QFileInfo mainQml;
    if (ancestorRootResolved) {
        const QString candidateQmlRoot = resourceRoot.filePath(QStringLiteral("qml"));
        mainQml.setFile(QDir(candidateQmlRoot).filePath(QStringLiteral("Main.qml")));
    }

    // Windows builds traditionally live under repo/native/build-msvc, so walking two
    // levels up finds the source root. Native Linux builds are intentionally out-of-tree
    // (for example ~/build/colosseum-linux), so the source root is not an ancestor of the
    // executable. For that development shape, accept the launch working directory only
    // when it contains qml/Main.qml; the build manifest below still fingerprints the whole
    // tree and fails closed if the runtime QML differs from what this binary was built with.
    if (!mainQml.isFile()) {
        const QDir workingRoot(QDir::currentPath());
        const QFileInfo workingMainQml(
            QDir(workingRoot.filePath(QStringLiteral("qml"))).filePath(QStringLiteral("Main.qml")));
        if (!workingMainQml.isFile()) {
            const QString missingPath = ancestorRootResolved
                ? mainQml.absoluteFilePath()
                : QDir(applicationDirPath).absoluteFilePath(QStringLiteral("../../qml/Main.qml"));
            setError(error, QStringLiteral("qml_entrypoint_missing: ") + missingPath);
            return std::nullopt;
        }
        resourceRoot = workingRoot;
        mainQml = workingMainQml;
    }

    const QString qmlRoot = resourceRoot.filePath(QStringLiteral("qml"));

    const QString manifestPath =
        QDir(applicationDirPath).filePath(QStringLiteral("qml-build.manifest"));
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("qml_build_manifest_missing: ") + manifestPath);
        return std::nullopt;
    }

    const QByteArray manifestBytes = manifest.readAll();
    if (manifestValue(manifestBytes, QByteArrayLiteral("schema")) != QLatin1String("1")) {
        setError(error, QStringLiteral("qml_build_manifest_schema_invalid: ") + manifestPath);
        return std::nullopt;
    }
    const QString expected = manifestValue(manifestBytes, QByteArrayLiteral("qmlTreeSha256"));
    if (!validFingerprint(expected)) {
        setError(error, QStringLiteral("qml_build_manifest_fingerprint_invalid: ") + manifestPath);
        return std::nullopt;
    }

    QString fingerprintError;
    const QString actual = qmlTreeFingerprint(qmlRoot, &fingerprintError);
    if (actual.isEmpty()) {
        setError(error, fingerprintError);
        return std::nullopt;
    }
    if (actual.compare(expected, Qt::CaseInsensitive) != 0) {
        setError(error, QStringLiteral("qml_build_mismatch: expected=") + expected
                            + QStringLiteral(" actual=") + actual);
        return std::nullopt;
    }

    StartupLayout layout;
    layout.qmlPath = mainQml.absoluteFilePath();
    layout.resourceRoot = resourceRoot.absolutePath();
    return layout;
}
