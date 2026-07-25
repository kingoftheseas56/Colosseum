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
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

class AudioPairingStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    // DEFAULT-constructed QSettings on purpose (2026-07-18 fix): it resolves through the
    // application identity (org/app names main.cpp sets to Brotherhood/Colosseum — same
    // hive as before in prod). The old hardcoded names meant TEST processes wrote the
    // USER'S live registry hive no matter what identity they set — the autoattach harness
    // wiped every real book↔audiobook pairing on each suite run. A test that sets its own
    // org/app names now gets its own disposable hive automatically.
    explicit AudioPairingStore(QObject *parent = nullptr)
        : QObject(parent) {
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

    // Every stored pairing, as a list of maps. The alignment service enumerates this on
    // startup to re-summon in-progress read-alongs; each map is the same self-describing
    // record getPairing() returns (carries bookId/updatedAt).
    Q_INVOKABLE QVariantList allPairings() const {
        QVariantList out;
        out.reserve(m_map.size());
        for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
            out.append(it.value().toMap());
        return out;
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
        emit pairingSaved(bookId, rec);
    }

    // Unlink — the Audio tab's "unlink" affordance.
    Q_INVOKABLE void deletePairing(const QString &bookId) {
        if (m_map.remove(bookId)) { save(); bump(); emit pairingDeleted(bookId); }
    }

signals:
    void changed();
    // Fine-grained companions to changed(): a specific pairing was upserted (carries the
    // stamped record) or removed. The alignment service listens to these to (re)schedule
    // or drop a book's read-along without re-reading the whole store.
    void pairingSaved(const QString &bookId, const QVariantMap &pairing);
    void pairingDeleted(const QString &bookId);

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
