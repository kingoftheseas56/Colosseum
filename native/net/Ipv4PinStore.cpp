#include "net/Ipv4PinStore.h"

#include <QAbstractSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int kSchemaVersion = 1;
constexpr int kMaxAttempts = 4;
constexpr int kRetryDelayMs = 250;
}

Ipv4PinStore::Ipv4PinStore(QString cachePath, Lookup lookup, QObject* parent)
    : QObject(parent),
      m_cachePath(cachePath.isEmpty() ? productionCachePath() : std::move(cachePath)),
      m_lookup(std::move(lookup))
{
    if (!m_lookup) {
        m_lookup = [this](const QString& host, LookupDone done) {
            QHostInfo::lookupHost(host, this, [done = std::move(done)](const QHostInfo& info) mutable {
                QString ipv4;
                for (const QHostAddress& address : info.addresses()) {
                    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                        ipv4 = address.toString();
                        break;
                    }
                }
                done(ipv4);
            });
        };
    }
    load();
}
QString Ipv4PinStore::productionCachePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("bootstrap/ipv4-pins.json"));
}

QString Ipv4PinStore::normalizedHost(const QString& host)
{
    return host.trimmed().toLower();
}

QString Ipv4PinStore::pinForHost(const QString& host) const
{
    return m_pins.value(normalizedHost(host));
}

void Ipv4PinStore::load()
{
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toInt() != kSchemaVersion)
        return;

    const QJsonObject pins = root.value(QStringLiteral("pins")).toObject();
    for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
        const QHostAddress address(it.value().toString());
        if (!address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol)
            m_pins.insert(normalizedHost(it.key()), address.toString());
    }
}
bool Ipv4PinStore::save() const
{
    if (!QDir().mkpath(QFileInfo(m_cachePath).absolutePath()))
        return false;

    QJsonObject pins;
    for (auto it = m_pins.constBegin(); it != m_pins.constEnd(); ++it)
        pins.insert(it.key(), it.value());
    const QJsonObject root{{QStringLiteral("schema"), kSchemaVersion},
                           {QStringLiteral("pins"), pins}};

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(m_cachePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(bytes) != bytes.size())
        return false;
    return file.commit();
}

void Ipv4PinStore::refresh(const QStringList& hosts)
{
    QSet<QString> unique;
    for (const QString& rawHost : hosts) {
        const QString host = normalizedHost(rawHost);
        if (!host.isEmpty())
            unique.insert(host);
    }

    bool started = false;
    for (const QString& host : unique) {
        if (m_inFlight.contains(host))
            continue;
        m_inFlight.insert(host);
        started = true;
        resolveAttempt(host, 1);
    }
    if (!started && m_inFlight.isEmpty())
        QTimer::singleShot(0, this, [this] {
            if (m_refreshFinished)
                m_refreshFinished();
        });
}
void Ipv4PinStore::resolveAttempt(const QString& host, int attempt)
{
    m_lookup(host, [this, host, attempt](QString ipv4) {
        QHostAddress address(ipv4);
        if (!address.isNull() && address.protocol() == QAbstractSocket::IPv4Protocol) {
            const QString normalized = address.toString();
            if (m_pins.value(host) != normalized) {
                m_pins.insert(host, normalized);
                if (!save())
                    qWarning("[net] failed to persist IPv4 pin cache: %s", qUtf8Printable(m_cachePath));
                if (m_pinChanged)
                    m_pinChanged(host, normalized);
            }
            finishHost(host);
            return;
        }

        if (attempt < kMaxAttempts) {
            QTimer::singleShot(kRetryDelayMs, this, [this, host, attempt] {
                resolveAttempt(host, attempt + 1);
            });
            return;
        }

        if (m_pins.contains(host))
            qInfo("[net] IPv4 refresh missed %s; retaining cached %s",
                  qUtf8Printable(host), qUtf8Printable(m_pins.value(host)));
        else
            qWarning("[net] NO IPv4 for %s after asynchronous retries", qUtf8Printable(host));
        finishHost(host);
    });
}

void Ipv4PinStore::finishHost(const QString& host)
{
    m_inFlight.remove(host);
    if (m_inFlight.isEmpty() && m_refreshFinished)
        m_refreshFinished();
}
