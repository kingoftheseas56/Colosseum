#include "SourceContentStore.h"
#include "CatalogRemote.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QRegularExpression>

namespace {
bool hash(const QString& value) {
    static const QRegularExpression expression(QStringLiteral("^[a-f0-9]{64}$"));
    return expression.match(value).hasMatch();
}
bool key(const QString& value) {
    static const QRegularExpression expression(QStringLiteral("^[a-z][a-z0-9-]{0,100}$"));
    return expression.match(value).hasMatch();
}
}
SourceContentStore::SourceContentStore(QObject* parent)
    : QObject(parent), m_remote(new CatalogRemote(QStringLiteral("content"), this)) {
    m_remote->setResultValidator([this](const QString& method, const QJsonValue& result) {
        return method == QLatin1String("bundle") && result.isObject() && install(result.toObject(), false);
    });
    connect(m_remote, &CatalogRemote::changed, this, &SourceContentStore::synchronize);
    connect(m_remote, &CatalogRemote::failed, this, [this](const QString&, const QString& reason) { setError(reason); });
}
void SourceContentStore::configure(QNetworkAccessManager* nam, const QUrl& endpoint,
                                  const QString& cachePath, const QString& starterPath) {
    m_endpoint = endpoint;
    if (!endpoint.isEmpty() && (!endpoint.isValid() || !endpoint.userInfo().isEmpty()
        || !endpoint.query().isEmpty() || !endpoint.fragment().isEmpty()
        || (endpoint.scheme() != QLatin1String("https")
            && !(endpoint.scheme() == QLatin1String("http")
                && (endpoint.host() == QLatin1String("127.0.0.1") || endpoint.host() == QLatin1String("localhost")))))) m_endpoint = QUrl();
    QFile starter(starterPath);
    if (starter.open(QIODevice::ReadOnly) && starter.size() <= 4 * 1024 * 1024)
        install(QJsonDocument::fromJson(starter.readAll()).object());
    m_remote->configure(nam, m_endpoint, cachePath);
    synchronize();
}
QVariantMap SourceContentStore::document(const QString& name) const {
    return m_documents.value(name).toObject().toVariantMap();
}
QString SourceContentStore::assetUrl(const QString& relative) const {
    if (relative.startsWith(QStringLiteral("https://"))) return relative;
    QString path = relative;
    while (path.startsWith(QStringLiteral("../"))) path.remove(0, 3);
    while (path.startsWith(QStringLiteral("./"))) path.remove(0, 2);
    if (!path.startsWith(QStringLiteral("assets/")) || path.contains(QStringLiteral(".."))
        || path.contains('\\') || path.contains('?') || path.contains('#')) return {};
    if (m_endpoint.isEmpty()) {
        const auto root = QFileInfo(QDir::current().filePath(QStringLiteral("assets"))).canonicalFilePath();
        const QFileInfo local(QDir::current().filePath(path));
        const auto canonical = local.canonicalFilePath();
        if (!root.isEmpty() && local.isFile() && canonical.startsWith(root + QLatin1Char('/')))
            return QUrl::fromLocalFile(canonical).toString();
        return {};
    }
    const auto asset = m_manifest.value(QStringLiteral("assets")).toObject().value(path).toObject();
    const auto target = asset.value(QStringLiteral("path")).toString();
    if (target.isEmpty() || m_endpoint.isEmpty()) return {};
    return m_endpoint.resolved(QUrl(target)).toString();
}
void SourceContentStore::refresh(const QString& name) {
    if (!name.isEmpty() && !key(name)) return;
    // One atomic presentation generation avoids mixing new atlas coordinates
    // with an older image manifest. refresh(key) deliberately refreshes the bundle.
    m_remote->read(QStringLiteral("bundle"), {}, QVariantMap(), true);
}
void SourceContentStore::setError(const QString& error) {
    if (m_error == error) return;
    m_error = error;
    emit lastErrorChanged();
}
bool SourceContentStore::install(const QJsonObject& bundle, bool commit) {
    const auto manifest = bundle.value(QStringLiteral("manifest")).toObject();
    const auto descriptions = manifest.value(QStringLiteral("documents")).toObject();
    const auto texts = bundle.value(QStringLiteral("documents")).toObject();
    if (manifest.value(QStringLiteral("schemaVersion")) != QJsonValue(1)
        || !hash(manifest.value(QStringLiteral("revision")).toString())
        || descriptions.isEmpty() || descriptions.size() > 64 || descriptions.size() != texts.size()) return false;
    const QStringList required = {QStringLiteral("universes-roster"), QStringLiteral("universe-one-piece"),
        QStringLiteral("universe-dcau"), QStringLiteral("universe-star-wars"), QStringLiteral("tribute-dcau"),
        QStringLiteral("tribute-star-wars"), QStringLiteral("tribute-one-piece-east-blue"),
        QStringLiteral("tribute-one-piece-paradise"), QStringLiteral("tribute-one-piece-new-world"),
        QStringLiteral("tribute-one-piece-final-sea")};
    for (const auto& name : required) if (!descriptions.contains(name)) return false;
    QJsonObject documents;
    for (auto it = descriptions.begin(); it != descriptions.end(); ++it) {
        if (!key(it.key()) || !it.value().isObject() || !texts.value(it.key()).isString()) return false;
        const auto bytes = texts.value(it.key()).toString().toUtf8();
        const auto description = it.value().toObject();
        if (bytes.size() > 512 * 1024 || !hash(description.value(QStringLiteral("sha256")).toString())
            || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                != description.value(QStringLiteral("sha256")).toString()) return false;
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(bytes, &error);
        const auto envelope = parsed.object();
        if (error.error != QJsonParseError::NoError || !parsed.isObject()
            || envelope.value(QStringLiteral("schemaVersion")) != QJsonValue(1)
            || !hash(envelope.value(QStringLiteral("revision")).toString())
            || envelope.value(QStringLiteral("revision")) != description.value(QStringLiteral("revision"))
            || !envelope.value(QStringLiteral("data")).isObject()) return false;
        const auto data = envelope.value(QStringLiteral("data")).toObject();
        if (it.key() == QLatin1String("universes-roster")
            && (!data.value(QStringLiteral("universes")).isArray()
                || data.value(QStringLiteral("universes")).toArray().isEmpty()
                || !data.value(QStringLiteral("archive")).isArray()
                || !data.value(QStringLiteral("shelfOverlays")).isObject())) return false;
        if (it.key().startsWith(QLatin1String("universe-"))) {
            const auto universe = data.value(QStringLiteral("universe")).toObject();
            if (universe.value(QStringLiteral("id")).toString().isEmpty()
                || universe.value(QStringLiteral("title")).toString().isEmpty()
                || !universe.value(QStringLiteral("sections")).isArray()
                || universe.value(QStringLiteral("sections")).toArray().isEmpty()) return false;
        }
        if (it.key().startsWith(QLatin1String("tribute-one-piece-"))
            && (!data.value(QStringLiteral("story")).toObject().value(QStringLiteral("arcs")).isArray()
                || data.value(QStringLiteral("story")).toObject().value(QStringLiteral("arcs")).toArray().isEmpty()
                || !data.value(QStringLiteral("atlas")).isObject()
                || !data.value(QStringLiteral("artByArc")).isObject())) return false;
        if (it.key() == QLatin1String("tribute-dcau")
            && (!data.value(QStringLiteral("hubs")).isArray() || data.value(QStringLiteral("hubs")).toArray().isEmpty())) return false;
        if (it.key() == QLatin1String("tribute-star-wars")
            && (!data.value(QStringLiteral("destinations")).isArray() || data.value(QStringLiteral("destinations")).toArray().isEmpty()
                || !data.value(QStringLiteral("skywalkerDestination")).isObject())) return false;
        documents.insert(it.key(), envelope.value(QStringLiteral("data")));
    }
    const auto assets = manifest.value(QStringLiteral("assets")).toObject();
    if (assets.size() > 20000) return false;
    static const QRegularExpression assetPath(QStringLiteral("^/assets/content/[a-f0-9]{64}\\.[a-z0-9]+$"));
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        const auto asset = it.value().toObject();
        const auto size = asset.value(QStringLiteral("bytes")).toDouble(-1);
        if (!it.key().startsWith(QStringLiteral("assets/")) || it.key().contains(QStringLiteral(".."))
            || !assetPath.match(asset.value(QStringLiteral("path")).toString()).hasMatch()
            || !hash(asset.value(QStringLiteral("sha256")).toString())
            || !asset.value(QStringLiteral("path")).toString().contains(asset.value(QStringLiteral("sha256")).toString())
            || size < 0 || size > 25 * 1024 * 1024 || size != qint64(size)) return false;
    }
    if (!commit || (m_manifest == manifest && m_documents == documents)) return true;
    const auto old = m_documents;
    m_manifest = manifest;
    m_documents = documents;
    ++m_generation;
    setError({});
    for (auto it = documents.begin(); it != documents.end(); ++it)
        if (old.value(it.key()) != it.value()) emit documentChanged(it.key());
    emit generationChanged();
    return true;
}
void SourceContentStore::synchronize() {
    const auto result = m_remote->read(QStringLiteral("bundle"), {}, QVariantMap()).toMap();
    if (!result.isEmpty() && !install(QJsonObject::fromVariantMap(result))) setError(QStringLiteral("CONTENT_INVALID"));
}
