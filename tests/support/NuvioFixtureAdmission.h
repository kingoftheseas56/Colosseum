#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QSet>
#include <QString>
#include <utility>

namespace Colosseum::Nuvio {

// The diagnostic package may switch between its exact metadata pins after one
// empty-session proof. Production never supplies pins or enables this gate.
class FixtureAdmission {
public:
    explicit FixtureAdmission(QHash<QString, QByteArray> pins = {})
        : m_pins(std::move(pins)) {}

    void reset() { m_verifiedSession.clear(); m_admitted.clear(); }
    QString verifiedSession() const { return m_verifiedSession; }

    bool verify(const QString &diagnostic, bool reuseVerifiedSession = false) {
        if (reuseVerifiedSession && !m_pins.isEmpty() && sameSafeSession(diagnostic)
            && ownedCount(diagnostic)) return true;
        reset();
        const auto session = field(diagnostic, QStringLiteral("session_instance"));
        if (!safe(diagnostic) || session.isEmpty() || count(diagnostic) != 0) return false;
        m_verifiedSession = session;
        return true;
    }

    bool admit(const QString &diagnostic, const QString &infoHash, int fileIdx,
               const QByteArray &metadata) {
        if (!sameSafeSession(diagnostic) || metadata.isEmpty()) return false;
        // Preserve the original one-fixture gate for configurations without pins.
        if (m_pins.isEmpty()) return count(diagnostic) == 0;
        const auto pin = m_pins.constFind(infoHash);
        if (fileIdx != 0 || pin == m_pins.cend() || !ownedCount(diagnostic)
            || QCryptographicHash::hash(metadata, QCryptographicHash::Sha256).toHex() != pin.value())
            return false;
        m_admitted.insert(infoHash);
        return true;
    }

private:
    static QString field(const QString &diagnostic, const QString &key) {
        for (const auto &part : diagnostic.split(QLatin1Char(';'))) {
            const auto value = part.trimmed();
            if (value.startsWith(key + QLatin1Char('='))) return value.mid(key.size() + 1);
        }
        return {};
    }
    static bool safe(const QString &diagnostic) {
        return field(diagnostic, QStringLiteral("isolation_pass")) == QStringLiteral("true");
    }
    static int count(const QString &diagnostic) {
        bool ok = false;
        const int value = field(diagnostic, QStringLiteral("torrent_count")).toInt(&ok);
        return ok && value >= 0 ? value : -1;
    }
    bool sameSafeSession(const QString &diagnostic) const {
        return !m_verifiedSession.isEmpty() && safe(diagnostic)
            && field(diagnostic, QStringLiteral("session_instance")) == m_verifiedSession;
    }
    bool ownedCount(const QString &diagnostic) const {
        const int value = count(diagnostic);
        return value >= 0 && value <= m_admitted.size();
    }

    QHash<QString, QByteArray> m_pins;
    QString m_verifiedSession;
    QSet<QString> m_admitted;
};

} // namespace Colosseum::Nuvio
