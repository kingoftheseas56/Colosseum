#pragma once

// SearchHistoryStore -- a tiny, durable MRU list for the three world search surfaces.
// It owns the disk boundary so QML Loader recreation and provider callbacks cannot decide
// whether a user search is remembered.

#include <QObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QStringList>
#include <QVariant>
#include <QDebug>
#include <memory>

namespace {
// Isolation gate (2026-08-14 fix). See ProgressStore.h's progressStoreTaggedIniPath() for the
// full writeup: SearchHistoryStore hardcoded the QSettings two-arg registry constructor
// (org="Brotherhood", app="Colosseum"), so a tagged/isolated Lanista test session read AND
// wrote the real user's search history regardless of COLOSSEUM_APPDATA_TAG. Named per-store
// (searchHistoryStore...) because this header, ProgressStore.h, and CollectionStore.h are all
// #included into main.cpp, and two identically-named functions in unnamed namespaces would
// collide in that one translation unit. Untagged: returns an empty string, meaning "use the
// registry" — the daily app is byte-for-byte unaffected.
inline QString searchHistoryStoreTaggedIniPath(const QString &storeFileName) {
    if (!qEnvironmentVariableIsSet("COLOSSEUM_APPDATA_TAG"))
        return QString();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + storeFileName;
}
}

class SearchHistoryStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool rememberEnabled READ rememberEnabled WRITE setRememberEnabled NOTIFY rememberEnabledChanged)

public:
    explicit SearchHistoryStore(QObject *parent = nullptr)
        : QObject(parent),
          m_settings(
              searchHistoryStoreTaggedIniPath(QStringLiteral("search-history-store.ini")).isEmpty()
                  ? std::make_unique<QSettings>(QStringLiteral("Brotherhood"),
                                                QStringLiteral("Colosseum"))
                  : std::make_unique<QSettings>(
                        searchHistoryStoreTaggedIniPath(QStringLiteral("search-history-store.ini")),
                        QSettings::IniFormat)) {
        m_rememberEnabled = m_settings->value(policyKey(), true).toBool();
    }

    // The explicit INI constructor is deliberately public for the native persistence harness.
    explicit SearchHistoryStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {
        m_rememberEnabled = m_settings->value(policyKey(), true).toBool();
    }

    int revision() const { return m_revision; }
    bool rememberEnabled() const { return m_rememberEnabled; }

    Q_INVOKABLE void setRememberEnabled(bool enabled) {
        if (m_rememberEnabled == enabled)
            return;
        m_rememberEnabled = enabled;
        m_settings->setValue(policyKey(), enabled);
        m_settings->sync();
        if (m_settings->status() != QSettings::NoError)
            qWarning() << "Could not persist search-history privacy policy";
        emit rememberEnabledChanged();
    }

    Q_INVOKABLE QStringList list(const QString &scope) const {
        const QString key = storageKey(scope);
        const QVariant value = m_settings->value(key);
        if (!value.isValid())
            return {};
        if (value.metaType().id() == QMetaType::QString)
            return cleanEntries({value.toString()}); // INI serializes a one-item QStringList as one string.
        if (value.metaType().id() != QMetaType::QStringList) {
            qWarning() << "Ignoring malformed search history for" << normalizedScope(scope);
            return {};
        }
        return cleanEntries(value.toStringList());
    }

    Q_INVOKABLE QStringList record(const QString &scope, const QString &query) {
        const QString clean = query.trimmed();
        if (clean.size() < 2)
            return list(scope);
        if (!m_rememberEnabled)
            return list(scope);

        QStringList next = list(scope);
        const QString folded = clean.toCaseFolded();
        for (auto it = next.begin(); it != next.end();) {
            if (it->toCaseFolded() == folded)
                it = next.erase(it);
            else
                ++it;
        }
        next.prepend(clean);
        while (next.size() > 6)
            next.removeLast();
        write(scope, next);
        return next;
    }

    Q_INVOKABLE QStringList remove(const QString &scope, const QString &query) {
        const QString folded = query.trimmed().toCaseFolded();
        if (folded.isEmpty())
            return list(scope);

        QStringList next = list(scope);
        for (auto it = next.begin(); it != next.end();) {
            if (it->toCaseFolded() == folded)
                it = next.erase(it);
            else
                ++it;
        }
        write(scope, next);
        return next;
    }

    Q_INVOKABLE void clear(const QString &scope) {
        m_settings->remove(storageKey(scope));
        synchronize(scope);
        bump(scope);
    }

    // Account Centre "Clear search history" (E2, CPP-PORT-CONTRACT.md-adjacent roadmap
    // §9): a product-level aggregate clear over an EXPLICIT, caller-supplied scope list —
    // never a scan of whatever keys happen to exist in the backing QSettings group. The
    // caller (AccountCenter.qml) names exactly the scopes the confirmation copy promises
    // ("biblio", "tankoban", "theatre" — the real scopes recorded via record()/list() as of
    // 2026-08-19), so this aggregate can never silently widen to cover a future/unexpected
    // scope. Reuses clear(scope)'s own remove+sync+bump per scope — same isolation-gated
    // m_settings, same per-scope changed(scope) signal so any open SearchSurface reloads.
    Q_INVOKABLE void clearAllScopes(const QStringList &scopes) {
        for (const QString &scope : scopes)
            clear(scope);
    }

signals:
    void changed(const QString &scope);
    void rememberEnabledChanged();

private:
    static QString normalizedScope(const QString &scope) {
        const QString normalized = scope.trimmed().toLower();
        return normalized.isEmpty() ? QStringLiteral("default") : normalized;
    }

    static QString policyKey() {
        return QStringLiteral("searchHistory/rememberEnabled");
    }

    static QString storageKey(const QString &scope) {
        return QStringLiteral("searchHistory/") + normalizedScope(scope);
    }

    static QStringList cleanEntries(const QStringList &raw) {
        QStringList clean;
        for (const QString &entry : raw) {
            const QString trimmed = entry.trimmed();
            if (trimmed.size() < 2)
                continue;
            const QString folded = trimmed.toCaseFolded();
            bool duplicate = false;
            for (const QString &kept : clean) {
                if (kept.toCaseFolded() == folded) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                clean.append(trimmed);
            if (clean.size() == 6)
                break;
        }
        return clean;
    }

    void write(const QString &scope, const QStringList &entries) {
        m_settings->setValue(storageKey(scope), entries);
        synchronize(scope);
        bump(scope);
    }

    void synchronize(const QString &scope) {
        m_settings->sync();
        if (m_settings->status() != QSettings::NoError)
            qWarning() << "Could not persist search history for" << normalizedScope(scope);
    }

    void bump(const QString &scope) {
        ++m_revision;
        emit changed(normalizedScope(scope));
    }

    std::unique_ptr<QSettings> m_settings;
    int m_revision = 0;
    bool m_rememberEnabled = true;
};
