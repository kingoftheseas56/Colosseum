#pragma once

// SearchHistoryStore -- a tiny, durable MRU list for the three world search surfaces.
// It owns the disk boundary so QML Loader recreation and provider callbacks cannot decide
// whether a user search is remembered.

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>
#include <QDebug>
#include <memory>

class SearchHistoryStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit SearchHistoryStore(QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(QStringLiteral("Brotherhood"),
                                                 QStringLiteral("Colosseum"))) {}

    // The explicit INI constructor is deliberately public for the native persistence harness.
    explicit SearchHistoryStore(const QString &iniPath, QObject *parent = nullptr)
        : QObject(parent),
          m_settings(std::make_unique<QSettings>(iniPath, QSettings::IniFormat)) {}

    int revision() const { return m_revision; }

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

signals:
    void changed(const QString &scope);

private:
    static QString normalizedScope(const QString &scope) {
        const QString normalized = scope.trimmed().toLower();
        return normalized.isEmpty() ? QStringLiteral("default") : normalized;
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
};
