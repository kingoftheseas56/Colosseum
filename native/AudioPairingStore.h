#pragma once

// AudioPairing — the read-along memory exposed to QML as `AudioPairing`.
// It is one small thing: a persisted note of "this book is paired to that
// audiobook, and here is how the book's chapters line up with the audiobook's
// chapter files." A book page (or the reader's Audio tab) writes it; opening a
// book reads it back to auto-summon the paired audiobook at the right chapter.
// No network, no scraping — just memory + disk, a bookmark file for pairings.
// Persisted via QSettings (same lightweight-state mechanism as ProgressStore /
// SearchHistoryStore), so it survives a restart.
//
// QML / reader-bridge contract (the only surface):
//   AudioPairing.getPairing(bookId)
//       -> { bookId, audiobookId, mappings: [ { bookChapterHref, bookChapterLabel,
//            abChapterIndex, abChapterTitle } ], updatedAt }   (or {} when unlinked)
//   AudioPairing.savePairing(bookId, pairing)   // upsert; stamps bookId + updatedAt
//   AudioPairing.deletePairing(bookId)          // unlink
//   AudioPairing.revision                       // bump on every change — name it in a
//                                               //   binding to make getPairing()-based
//                                               //   bindings re-evaluate reactively.

#include <QObject>
#include <QSettings>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

class AudioPairingStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit AudioPairingStore(QObject *parent = nullptr)
        : QObject(parent),
          m_settings(QStringLiteral("Brotherhood"), QStringLiteral("Colosseum")) {
        load();
    }

    int revision() const { return m_revision; }

    // The pairing for a book, or {} when nothing is linked. bookId is the reader's
    // stable book identity (id or path) — the same key the Audio tab pairs against.
    Q_INVOKABLE QVariantMap getPairing(const QString &bookId) const {
        if (bookId.isEmpty())
            return {};
        return m_map.value(bookId).toMap();
    }

    // Upsert one pairing. `pairing` is a plain object from the reader/QML; we stamp
    // the authoritative bookId + a fresh updatedAt so the record is self-describing.
    Q_INVOKABLE void savePairing(const QString &bookId, const QVariantMap &pairing) {
        if (bookId.isEmpty())
            return;
        QVariantMap rec = pairing;
        rec.insert(QStringLiteral("bookId"), bookId);
        rec.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
        m_map.insert(bookId, rec);
        save();
        bump();
    }

    // Unlink — the Audio tab's "unlink" affordance.
    Q_INVOKABLE void deletePairing(const QString &bookId) {
        if (m_map.remove(bookId)) { save(); bump(); }
    }

signals:
    void changed();

private:
    void bump() { ++m_revision; emit changed(); }

    void load() {
        m_map.clear();
        const QByteArray blob =
            m_settings.value(QStringLiteral("audiobook/pairings")).toByteArray();
        const QJsonDocument doc = QJsonDocument::fromJson(blob);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
                m_map.insert(it.key(), it.value().toObject().toVariantMap());
        }
    }
    void save() {
        QJsonObject obj;
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            obj.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
        m_settings.setValue(QStringLiteral("audiobook/pairings"),
                            QJsonDocument(obj).toJson(QJsonDocument::Compact));
        m_settings.sync();
    }

    QSettings m_settings;
    QHash<QString, QVariant> m_map;   // bookId → pairing map
    int m_revision = 0;
};
