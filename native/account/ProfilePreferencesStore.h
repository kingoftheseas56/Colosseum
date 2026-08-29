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
    Q_PROPERTY(bool rememberSearchHistory READ rememberSearchHistory WRITE setRememberSearchHistory NOTIFY rememberSearchHistoryChanged)
    Q_PROPERTY(bool keepActivityHistory READ keepActivityHistory WRITE setKeepActivityHistory NOTIFY keepActivityHistoryChanged)
    Q_PROPERTY(bool syncActivityHistory READ syncActivityHistory WRITE setSyncActivityHistory NOTIFY syncActivityHistoryChanged)

public:
    explicit ProfilePreferencesStore(
        QObject *parent = nullptr);

    explicit ProfilePreferencesStore(
        const QString &iniPath,
        QObject *parent = nullptr);

    bool showExplicit() const;
    bool hasShowExplicitValue() const;
    int revision() const;
    bool rememberSearchHistory() const;
    bool keepActivityHistory() const;
    bool syncActivityHistory() const;

    void setShowExplicit(
        bool showExplicit);
    Q_INVOKABLE void setRememberSearchHistory(bool enabled);
    Q_INVOKABLE void setKeepActivityHistory(bool enabled);
    Q_INVOKABLE void setSyncActivityHistory(bool enabled);

    // Native remote-apply seam. Persists and notifies the shell, but does not
    // manufacture a new local sync mutation.
    bool applySyncedShowExplicit(
        bool showExplicit);

    bool clearSyncedShowExplicit();

signals:
    void showExplicitChanged();
    void rememberSearchHistoryChanged();
    void keepActivityHistoryChanged();
    void syncActivityHistoryChanged();
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
    bool m_rememberSearchHistory = true;
    bool m_keepActivityHistory = true;
    bool m_syncActivityHistory = true;
    int m_revision = 0;
};
