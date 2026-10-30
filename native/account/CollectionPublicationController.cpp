#include "CollectionPublicationController.h"
#include "../NamedCollectionsStore.h"
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
constexpr qint64 MaxEnvelope = 2 * 1024 * 1024;
constexpr qint64 MaxCover = 256 * 1024;
}
CollectionPublicationController::CollectionPublicationController(AccountClient *client, const QUrl &url, QObject *parent)
    : QObject(parent), m_client(client), m_serviceUrl(url) {
    connect(client, &AccountClient::completed, this, &CollectionPublicationController::complete);
}
void CollectionPublicationController::activate(NamedCollectionsStore *store, bool signedIn) {
    if (m_store != store) {
        deactivate();
        m_store = store;
    }
    if (m_signedIn != signedIn) {
        ++m_generation;
        m_request = 0;
        m_publications.clear();
        m_signedIn = signedIn;
    }
    emit changed();
}
void CollectionPublicationController::deactivate() {
    ++m_generation;
    m_request = 0;
    m_store = nullptr;
    m_signedIn = false;
    m_publications.clear();
    m_received.clear();
    m_operationKey.clear();
    m_retryOperationKey.clear();
    m_error.clear();
    m_message.clear();
    emit changed();
}
bool CollectionPublicationController::fail(const QString &message) {
    m_error = message;
    m_message.clear();
    emit changed();
    return false;
}
bool CollectionPublicationController::ready(bool accountRequired) {
    if (busy()) return false;
    if (!m_store || !m_store->healthy()) return fail(tr("Collections are unavailable while your profile is changing."));
    if (accountRequired && (!m_signedIn || !m_client || m_client->accessToken().isEmpty()))
        return fail(tr("Sign in to a Colosseum account to publish or manage links. Gift drafts stay on this device."));
    m_requestCollectionId.clear();
    m_requestShareId.clear();
    m_operationKey.clear();
    m_error.clear(); m_message.clear(); m_requestGeneration = m_generation;
    return true;
}
bool CollectionPublicationController::validShareId(const QString &id) {
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{43}$"));
    return re.match(id).hasMatch();
}
QString CollectionPublicationController::operationId(const QString &action, const QJsonObject &body) {
    // Only a digest of public content is retained. Never save a passphrase,
    // session, local source reference or request body in this retry journal.
    if (!m_store) return {};
    QJsonObject safe = body;
    safe.remove(QStringLiteral("passphrase"));
    safe.remove(QStringLiteral("operation_id"));
    const QString key = QString::fromLatin1(QCryptographicHash::hash(action.toUtf8()
        + QJsonDocument(safe).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    const QString path = m_store->filePath() + QStringLiteral(".publication-operations.json");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) { fail(tr("Another publication is being saved. Try again.")); return {}; }
    QJsonObject operations;
    QFile input(path);
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) {
            fail(tr("The publication retry journal could not be read.")); return {};
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(input.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            fail(tr("The publication retry journal is damaged. Nothing was sent.")); return {};
        }
        operations = doc.object();
    }
    m_operationKey = key;
    if (operations.contains(key)) return operations.value(key).toString();
    if (operations.size() >= 4096) {
        fail(tr("The publication retry journal is full. Nothing was sent.")); return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    operations.insert(key, id);
    const QByteArray bytes = QJsonDocument(operations).toJson(QJsonDocument::Compact);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        fail(tr("Could not save the publication request safely. Nothing was sent.")); return {};
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return id;
}
bool CollectionPublicationController::publish(const QString &id, bool gift, const QString &passphrase) {
    if (!ready(true)) return false;
    const auto snapshot = m_store->shareSnapshot(id);
    if (snapshot.isEmpty()) return fail(tr("This collection contains a local-only or unsupported reference and cannot be shared safely."));
    QJsonObject body{{"kind", gift ? "gift" : "collection"}, {"snapshot", QJsonObject::fromVariantMap(snapshot)}, {"passphrase", passphrase}};
    const QString operation = operationId(QStringLiteral("publish:") + id, body);
    if (operation.isEmpty()) return false;
    body.insert("operation_id", operation);
    m_requestCollectionId = id;
    m_request = m_client->publishCollection(body);
    emit changed(); return true;
}
bool CollectionPublicationController::publishChanges(const QString &shareId, int revision, const QString &id, const QString &passphrase) {
    if (!ready(true)) return false;
    if (!validShareId(shareId) || revision < 1) return fail(tr("Invalid publication identity."));
    if (sourceFor(shareId) != id)
        return fail(tr("This link is not connected to the selected local collection. Publish a new link instead."));
    const auto snapshot = m_store->shareSnapshot(id);
    if (snapshot.isEmpty()) return fail(tr("This collection contains a local-only or unsupported reference and cannot be shared safely."));
    QJsonObject body{{"expected_revision", revision}, {"snapshot", QJsonObject::fromVariantMap(snapshot)}, {"passphrase", passphrase}};
    const QString operation = operationId(QStringLiteral("update:") + shareId, body);
    if (operation.isEmpty()) return false;
    body.insert("operation_id", operation);
    m_requestCollectionId = id;
    m_requestShareId = shareId;
    m_request = m_client->updateCollectionPublication(shareId, body);
    emit changed(); return true;
}
bool CollectionPublicationController::refresh() {
    if (!ready(true)) return false;
    m_request = m_client->listCollectionPublications(); emit changed(); return true;
}
bool CollectionPublicationController::revoke(const QString &shareId, int revision) {
    if (!ready(true)) return false;
    if (!validShareId(shareId) || revision < 1) return fail(tr("Invalid publication identity."));
    QJsonObject body{{"expected_revision", revision}};
    const QString operation = operationId(QStringLiteral("revoke:") + shareId, body);
    if (operation.isEmpty()) return false;
    body.insert("operation_id", operation);
    m_requestShareId = shareId;
    m_request = m_client->revokeCollectionPublication(shareId, body); emit changed(); return true;
}
QString CollectionPublicationController::linkFor(const QString &id) const {
    if (!validShareId(id) || m_serviceUrl.isEmpty()) return {};
    return m_serviceUrl.resolved(QUrl(QStringLiteral("/collections/s/") + id)).toString();
}
bool CollectionPublicationController::copyLink(const QString &id) {
    const QString link = linkFor(id);
    if (link.isEmpty() || !QGuiApplication::clipboard()) return fail(tr("Could not copy this link."));
    QGuiApplication::clipboard()->setText(link);
    m_message = tr("Link copied."); m_error.clear(); emit changed(); return true;
}
bool CollectionPublicationController::openLink(const QString &value, const QString &passphrase) {
    if (!ready(false)) return false;
    m_received.clear();
    const QUrl url(value.trimmed());
    // Do not fetch arbitrary URLs or forward secrets to another host. The
    // account service origin is the only native receiving boundary.
    if (!url.isValid() || url.scheme() != m_serviceUrl.scheme() || url.host().compare(m_serviceUrl.host(), Qt::CaseInsensitive)
        || url.port(-1) != m_serviceUrl.port(-1) || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
        return fail(tr("Use a collection link from the configured Colosseum service."));
    const QString prefix = QStringLiteral("/collections/s/");
    if (!url.path().startsWith(prefix)) return fail(tr("This is not a collection share link."));
    const QString id = url.path().mid(prefix.size());
    if (!validShareId(id)) return fail(tr("This collection link is invalid."));
    m_received.clear();
    m_requestShareId = id;
    m_request = m_client->openSharedCollection(id, passphrase); emit changed(); return true;
}
bool CollectionPublicationController::acceptReceived(const QJsonObject &body) {
    const QString id = body.value("share_id").toString();
    const QString kind = body.value("kind").toString();
    const int revision = body.value("revision").toInt();
    const QVariantMap snapshot = body.value("snapshot").toObject().toVariantMap();
    QString error;
    if (!validShareId(id) || (kind != QLatin1String("gift") && kind != QLatin1String("collection"))
        || !body.value("revision").isDouble() || body.value("revision").toDouble() != revision
        || revision < 1 || (kind == QLatin1String("gift") && revision != 1)
        || (body.contains("revoked") && !body.value("revoked").isBool()) || body.value("revoked").toBool() || !NamedCollectionsStore::validateShareSnapshot(snapshot, &error))
        return fail(error.isEmpty() ? tr("This edition is invalid or unavailable.") : error);
    m_received = {{"share_id", id}, {"kind", kind}, {"revision", revision}, {"snapshot", snapshot}};
    return true;
}
bool CollectionPublicationController::openEditionFile(const QUrl &url) {
    if (!ready(false)) return false;
    m_received.clear();
    if (!url.isLocalFile()) return fail(tr("Choose a local edition file."));
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxEnvelope) return fail(tr("The edition file cannot be read or is too large."));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.read(MaxEnvelope + 1), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return fail(tr("The edition file is not valid JSON."));
    m_received.clear();
    if (!acceptReceived(doc.object())) return false;
    m_message = tr("Edition opened from a file. Its sender and current revocation status cannot be verified offline.");
    emit changed(); return true;
}
bool CollectionPublicationController::keepReceivedGift() {
    if (!ready(false)) return false;
    if (m_received.value("kind").toString() != QLatin1String("gift")) return fail(tr("Only gift editions can be kept as gifts."));
    const auto result = m_store->keepGift(m_received.value("share_id").toString(), m_received.value("revision").toInt(), m_received.value("snapshot").toMap());
    if (!result.value("ok").toBool()) return fail(result.value("error").toString());
    m_message = tr("Gift kept on this device, including its presentation, for offline viewing.");
    emit changed(); emit kept(result.value("collectionId").toString()); return true;
}
bool CollectionPublicationController::makeOwnVersion(const QString &id, const QString &name) {
    if (!ready(false)) return false;
    const QString operation = operationId(QStringLiteral("copy:") + id, {{"name", name}});
    if (operation.isEmpty()) return false;
    const auto result = m_store->makeOwnVersion(id, name, operation);
    if (!result.value("ok").toBool()) return fail(result.value("error").toString());
    m_message = tr("Your independent version is ready. The original gift has not changed.");
    emit changed(); emit copied(result.value("collectionId").toString()); return true;
}
bool CollectionPublicationController::savePresentation(const QString &id, const QVariantMap &presentation, int revision) {
    if (!ready(false)) return false;
    if (!m_store->savePresentation(id, presentation, revision)) return fail(m_store->error());
    m_message = tr("Gift draft saved on this device. No shared link was changed."); emit changed(); return true;
}
QString CollectionPublicationController::loadCover(const QUrl &url) {
    if (!url.isLocalFile()) { fail(tr("Choose a local PNG or JPEG cover.")); return {}; }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxCover) { fail(tr("Choose a PNG or JPEG no larger than 256 KiB.")); return {}; }
    QByteArray bytes = file.read(MaxCover + 1);
    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QByteArray format = reader.format().toLower();
    const QSize size = reader.size();
    if ((format != "png" && format != "jpeg" && format != "jpg") || size.isEmpty()
        || size.width() > 2048 || size.height() > 2048) {
        fail(tr("Choose a valid PNG or JPEG cover, at most 2048 × 2048 pixels.")); return {};
    }
    const QImage decoded = reader.read();
    if (decoded.isNull()) { fail(tr("This cover could not be decoded.")); return {}; }
    // Copy pixels into a new image, stripping EXIF, comments and location
    // metadata before the user explicitly publishes this presentation.
    QImage clean(decoded.size(), QImage::Format_ARGB32);
    clean.fill(Qt::transparent);
    { QPainter painter(&clean); painter.drawImage(0, 0, decoded); }
    QByteArray sanitized;
    QBuffer output(&sanitized); output.open(QIODevice::WriteOnly);
    if (!clean.save(&output, format == "png" ? "PNG" : "JPEG") || sanitized.size() > MaxCover) {
        fail(tr("The sanitized cover is too large. Choose a smaller image.")); return {};
    }
    m_error.clear(); emit changed();
    return QStringLiteral("data:image/") + (format == "png" ? QStringLiteral("png") : QStringLiteral("jpeg"))
        + QStringLiteral(";base64,") + QString::fromLatin1(sanitized.toBase64());
}
void CollectionPublicationController::clearReceived() { m_received.clear(); emit changed(); }
void CollectionPublicationController::complete(quint64 id, AccountOperation operation, quint64 tokenGeneration, const AccountTransportReply &reply) {
    if (id != m_request || m_requestGeneration != m_generation) return;
    m_request = 0;
    if (operation != AccountOperation::CollectionOpen && tokenGeneration != m_client->accessTokenGeneration()) {
        fail(tr("Your account session changed. Refresh your publications before trying again.")); return;
    }
    if (reply.networkError || reply.statusCode < 200 || reply.statusCode >= 300) {
        if (!m_operationKey.isEmpty()) m_retryOperationKey = m_operationKey;
        if (reply.errorCode == QLatin1String("idempotency_conflict")) {
            fail(tr("This retry differs from the earlier request. Re-enter the original passphrase, or check Shared links before deliberately starting a new publication request."));
        } else fail(reply.errorMessage.isEmpty() ? tr("The request did not finish. Retry the same action safely; no success has been assumed.") : reply.errorMessage.left(1000));
        return;
    }
    if (operation == AccountOperation::CollectionOpen) {
        if (reply.body.value("share_id").toString() != m_requestShareId) { fail(tr("The service returned a different collection link.")); return; }
        if (acceptReceived(reply.body)) { m_message = tr("Edition opened."); emit changed(); }
    } else if (operation == AccountOperation::CollectionList) {
        const auto rows = reply.body.value("publications").toArray();
        if (rows.size() > 200) { fail(tr("The publication list is too large.")); return; }
        QVariantList clean;
        for (const auto &value : rows) {
            const auto row = value.toObject();
            if (!validShareId(row.value("share_id").toString()) || row.value("name").toString().size() > 100) {
                fail(tr("The publication list is invalid.")); return;
            }
            clean.append(QVariantMap{{"share_id", row.value("share_id").toString()}, {"kind", row.value("kind").toString()},
                {"revision", row.value("revision").toInt()}, {"revoked", row.value("revoked").toBool()},
                {"protected", row.value("protected").toBool()}, {"name", row.value("name").toString()},
                {"source_collection_id", sourceFor(row.value("share_id").toString())}});
        }
        m_publications = clean; emit changed();
    } else {
        const auto row = reply.body;
        const QString shareId = row.value("share_id").toString();
        const QString kind = row.value("kind").toString();
        QString validationError;
        if (!validShareId(shareId) || (!m_requestShareId.isEmpty() && shareId != m_requestShareId)
            || !row.value("revision").isDouble() || row.value("revision").toDouble() != row.value("revision").toInt()
            || row.value("revision").toInt() < 1
            || (kind != QLatin1String("gift") && kind != QLatin1String("collection"))
            || (operation == AccountOperation::CollectionRevoke && !row.value("revoked").toBool())
            || (operation != AccountOperation::CollectionRevoke &&
                (row.value("revoked").toBool() || !NamedCollectionsStore::validateShareSnapshot(row.value("snapshot").toObject().toVariantMap(), &validationError)))) {
            fail(tr("The service returned an invalid publication receipt. Refresh before retrying.")); return;
        }
        if (operation != AccountOperation::CollectionRevoke && !saveSource(shareId, m_requestCollectionId)) {
            fail(tr("Publication exists on the service, but its local receipt could not be saved. Refresh Shared links; do not assume a failed publication.")); return;
        }
        QVariantMap clean{{"source_collection_id", sourceFor(shareId)}, {"share_id", shareId}, {"kind", kind},
            {"revision", row.value("revision").toInt()}, {"name", row.value("snapshot").toObject().value("name").toString()},
            {"revoked", row.value("revoked").toBool()}, {"protected", row.value("protected").toBool()}};
        for (int i = m_publications.size() - 1; i >= 0; --i)
            if (m_publications[i].toMap().value("share_id").toString() == shareId) m_publications.removeAt(i);
        m_publications.prepend(clean);
        if (!finishOperation()) {
            fail(tr("The publication completed, but the local retry receipt could not be updated. Refresh Shared links before publishing again.")); return;
        }
        m_message = operation == AccountOperation::CollectionRevoke ? tr("Link revoked. Previously kept copies cannot be recalled.")
            : tr("Publication saved. Use Shared links to copy its link.");
        emit changed();
    }
}

QString CollectionPublicationController::sourceFor(const QString &id) const {
    if (!m_store) return {};
    QFile file(m_store->filePath() + QStringLiteral(".publication-sources.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(id).toString();
}
bool CollectionPublicationController::saveSource(const QString &id, const QString &collectionId) {
    if (!m_store || collectionId.isEmpty()) return false;
    const QString path = m_store->filePath() + QStringLiteral(".publication-sources.json");
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return false;
    QJsonObject sources;
    QFile input(path);
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) return false;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(input.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
        sources = doc.object();
    }
    if (sources.size() >= 4096 && !sources.contains(id)) return false;
    sources.insert(id, collectionId);
    const auto bytes = QJsonDocument(sources).toJson(QJsonDocument::Compact);
    QSaveFile output(path);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

bool CollectionPublicationController::finishOperation() {
    if (!m_store || m_operationKey.isEmpty()) return false;
    const QString path = m_store->filePath() + QStringLiteral(".publication-operations.json");
    QLockFile lock(path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    auto operations = document.object();
    operations.remove(m_operationKey);
    const auto bytes = QJsonDocument(operations).toJson(QJsonDocument::Compact);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return false;
    if (m_retryOperationKey == m_operationKey) m_retryOperationKey.clear();
    m_operationKey.clear();
    return true;
}

bool CollectionPublicationController::startNewPublicationRequest() {
    if (!retryResetAvailable() || !m_store) return false;
    m_operationKey = m_retryOperationKey;
    if (!finishOperation()) return fail(tr("The pending request could not be reset safely."));
    m_error.clear();
    m_message = tr("A new publication request can now be started. Any earlier shared link still exists until you revoke it.");
    emit changed();
    return true;
}
