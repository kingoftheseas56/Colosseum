#pragma once

#include "DownloadLocation.h"

#include <QObject>
#include <QUrl>

#include <functional>

class DownloadMoveController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY changed)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool working READ working NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)

public:
    using Inventory = std::function<QList<DownloadLocation::Item>()>;
    using BusyReason = std::function<QString()>;
    using ApplyMove = std::function<void(const DownloadLocation::Plan &)>;

    DownloadMoveController(DownloadLocation location, Inventory inventory, BusyReason busyReason,
                           ApplyMove applyMove, QObject *parent = nullptr);

    QString currentPath() const;
    QVariantMap preview() const;
    QString status() const { return m_status; }
    bool working() const { return m_working; }
    bool pending() const { return !m_location.pending().id.isEmpty(); }

    Q_INVOKABLE void previewLocation(const QUrl &folder);
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void confirmMove();
    Q_INVOKABLE void retryMove();
    Q_INVOKABLE QString resolveLocalPath(const QString &path) const;
    Q_INVOKABLE bool pathWaitingForDrive(const QString &path) const;

signals:
    void changed();
    void locationAvailableChanged(bool available);

private:
    void moveFiles();
    void finishCleanup();
    DownloadLocation m_location;
    Inventory m_inventory;
    BusyReason m_busyReason;
    ApplyMove m_applyMove;
    DownloadLocation::Plan m_preview;
    QString m_status;
    bool m_working = false;
    bool m_available = false;
    quint64 m_previewGeneration = 0;
};
