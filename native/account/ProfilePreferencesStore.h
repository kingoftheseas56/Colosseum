#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QObject>
#include <QSettings>
#include <QString>

#include <memory>


class ProfilePreferencesStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        bool showExplicit
        READ showExplicit
        WRITE setShowExplicit
        NOTIFY showExplicitChanged)
    Q_PROPERTY(
        int revision
        READ revision
        NOTIFY changed)

public:
    explicit ProfilePreferencesStore(
        QObject *parent = nullptr);

    explicit ProfilePreferencesStore(
        const QString &iniPath,
        QObject *parent = nullptr);

    bool showExplicit() const;
    bool hasShowExplicitValue() const;
    int revision() const;

    void setShowExplicit(
        bool showExplicit);

    // Native remote-apply seam. Persists and notifies the shell, but does not
    // manufacture a new local sync mutation.
    bool applySyncedShowExplicit(
        bool showExplicit);

    bool clearSyncedShowExplicit();

signals:
    void showExplicitChanged();
    void changed();
    void syncDirty();

private:
    bool commitShowExplicit(
        bool showExplicit,
        bool localMutation);

    void load();

    std::unique_ptr<QSettings> m_settings;
    bool m_showExplicit = false;
    bool m_hasShowExplicitValue = false;
    int m_revision = 0;
};
