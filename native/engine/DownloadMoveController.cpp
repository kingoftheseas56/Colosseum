#include "DownloadMoveController.h"
#include "DownloadMediaPaths.h"

#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>

#include <utility>

namespace {
struct MoveResult {
    DownloadLocation location;
    QString error;
};
}

DownloadMoveController::DownloadMoveController(DownloadLocation location, Inventory inventory,
                                             BusyReason busyReason, ApplyMove applyMove,
                                             PlanGuard planGuard, QObject *parent)
    : QObject(parent)
    , m_location(std::move(location))
    , m_inventory(std::move(inventory))
    , m_busyReason(std::move(busyReason))
    , m_applyMove(std::move(applyMove))
    , m_planGuard(std::move(planGuard))
    , m_available(DownloadMediaPaths::available(m_location.root(), m_location.marker()))
{
    if (pending()) {
        m_status = tr("A confirmed move is waiting to resume.");
        // The root and aliases may have been committed just before shutdown.
        // Refresh readers and index consumers before they can open the new paths.
        if (m_location.committed())
            m_applyMove(m_location.pending());
    } else if (!m_available) {
        m_status = tr("Waiting for the download drive.");
    }
    auto *monitor = new QTimer(this);
    connect(monitor, &QTimer::timeout, this, [this] {
        const bool available = DownloadMediaPaths::available(m_location.root(), m_location.marker());
        if (available == m_available)
            return;
        m_available = available;
        if (!pending())
            m_status = available ? QString() : tr("Waiting for the download drive.");
        emit changed();
        emit locationAvailableChanged(available);
    });
    monitor->start(2000);
}

QString DownloadMoveController::moveBlocker(const DownloadLocation::Plan &plan) const
{
    if (m_planGuard) {
        const QString reason = m_planGuard(plan);
        if (!reason.isEmpty())
            return reason;
    }
    return m_busyReason();
}

QString DownloadMoveController::currentPath() const
{
    return QDir::toNativeSeparators(m_location.root());
}

QVariantMap DownloadMoveController::preview() const
{
    return m_preview.destinationRoot.isEmpty() && m_preview.error.isEmpty() ? QVariantMap() : m_preview.preview();
}

void DownloadMoveController::previewLocation(const QUrl &folder)
{
    if (m_working || pending())
        return;
    const QString destination = DownloadMediaPaths::rootForSelection(folder);
    const quint64 generation = ++m_previewGeneration;
    m_preview = {};
    m_preview.currentRoot = m_location.root();
    m_preview.destinationRoot = destination;
    m_preview.error = tr("Checking downloads and available space…");
    emit changed();
    auto *watcher = new QFutureWatcher<DownloadLocation::Plan>(this);
    connect(watcher, &QFutureWatcher<DownloadLocation::Plan>::finished, this, [this, watcher, generation] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewGeneration)
            return;
        if (result.error.isEmpty())
            result.error = moveBlocker(result);
        m_preview = std::move(result);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([location = m_location, destination, items = m_inventory()] {
        return location.plan(destination, items);
    }));
}

void DownloadMoveController::cancelPreview()
{
    ++m_previewGeneration;
    m_preview = {};
    emit changed();
}

void DownloadMoveController::confirmMove()
{
    if (m_working || pending() || m_preview.destinationRoot.isEmpty() || !m_preview.error.isEmpty())
        return;
    auto verified = m_location.plan(m_preview.destinationRoot, m_inventory());
    if (verified.error.isEmpty())
        verified.error = moveBlocker(verified);
    if (!verified.error.isEmpty() || verified.paths != m_preview.paths || verified.bytes != m_preview.bytes
        || verified.items != m_preview.items) {
        m_preview = std::move(verified);
        m_status = tr("The downloads changed. Review the updated preview.");
        emit changed();
        return;
    }
    m_status = m_location.begin(std::move(verified));
    if (!m_status.isEmpty()) {
        emit changed();
        return;
    }
    m_preview = {};
    DownloadMediaPaths::setMoving(true);
    moveFiles();
}

void DownloadMoveController::retryMove()
{
    if (!pending() || m_working)
        return;
    m_status = moveBlocker(m_location.pending());
    if (!m_status.isEmpty()) {
        emit changed();
        return;
    }
    DownloadMediaPaths::setMoving(true);
    if (m_location.committed()) {
        m_applyMove(m_location.pending());
        finishCleanup();
    } else {
        moveFiles();
    }
}

QString DownloadMoveController::resolveLocalPath(const QString &path) const
{
    const QUrl url(path);
    if (url.isLocalFile())
        return QUrl::fromLocalFile(DownloadMediaPaths::resolved(url.toLocalFile())).toString();
    return QDir::isAbsolutePath(path) ? DownloadMediaPaths::resolved(path) : path;
}

bool DownloadMoveController::pathWaitingForDrive(const QString &path) const
{
    const QUrl url(path);
    return DownloadMediaPaths::waitingForDrive(DownloadMediaPaths::resolved(url.isLocalFile() ? url.toLocalFile() : path));
}

void DownloadMoveController::moveFiles()
{
    m_working = true;
    m_status = tr("Copying and verifying downloads…");
    emit changed();
    auto *watcher = new QFutureWatcher<MoveResult>(this);
    connect(watcher, &QFutureWatcher<MoveResult>::finished, this, [this, watcher] {
        MoveResult result = watcher->result();
        watcher->deleteLater();
        m_location = std::move(result.location);
        if (!result.error.isEmpty()) {
            m_working = false;
            m_status = result.error;
            emit changed();
            return;
        }
        DownloadMediaPaths::install(m_location.root(), m_location.marker(), m_location.aliases(), m_location.knownLocations());
        m_applyMove(m_location.pending());
        m_status = m_busyReason();
        if (!m_status.isEmpty()) {
            m_working = false;
            emit changed();
            return;
        }
        finishCleanup();
    });
    watcher->setFuture(QtConcurrent::run([location = m_location]() mutable {
        QString error = location.copy();
        if (error.isEmpty())
            error = location.commit();
        return MoveResult{std::move(location), error};
    }));
}

void DownloadMoveController::finishCleanup()
{
    m_working = true;
    m_status = tr("Finishing the download move…");
    emit changed();
    auto *watcher = new QFutureWatcher<MoveResult>(this);
    connect(watcher, &QFutureWatcher<MoveResult>::finished, this, [this, watcher] {
        MoveResult result = watcher->result();
        watcher->deleteLater();
        m_location = std::move(result.location);
        m_working = false;
        m_status = result.error.isEmpty() ? tr("Download location changed.") : result.error;
        if (!pending())
            DownloadMediaPaths::setMoving(false);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([location = m_location]() mutable {
        const QString error = location.cleanSources();
        return MoveResult{std::move(location), error};
    }));
}
