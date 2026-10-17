#pragma once
// Deliberately narrow synthetic-profile fixture. Not a production profile API.
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <utility>

class VinylTestProfileController {
public:
    struct Status {
        QString alias;
        bool collectionHealthy = false;
        bool progressHealthy = false;
        bool activityHealthy = false;
        bool pendingWriteAccepted = false;
        bool pendingBeforeSwitch = false;
        bool pendingWriteCompleted = false;
        bool pendingWriteSucceeded = false;
        bool pendingOwnerRetired = false;
    };
    using ActivateCallback = std::function<bool(const QString &alias, bool withPendingWrite)>;
    using StatusCallback = std::function<Status()>;
    static constexpr const char *markerName = ".vinyl075-synthetic-fixture";
    static constexpr const char *markerContents = "vinyl075-synthetic-fixture-v1\n";

    VinylTestProfileController(QString actualAppDataRoot,
                               ActivateCallback activate, StatusCallback status)
        : m_appDataRoot(std::move(actualAppDataRoot)),
          m_activate(std::move(activate)), m_status(std::move(status)) {}

    bool enabled() const {
        if (qEnvironmentVariable("COLOSSEUM_VINYL_TEST_FIXTURES") != "1") return false;
        if (!QRegularExpression(QStringLiteral("^vinyl075-fixture-[A-Za-z0-9_-]{1,48}$"))
                 .match(qEnvironmentVariable("COLOSSEUM_APPDATA_TAG")).hasMatch()) return false;
        const QString root = qEnvironmentVariable("COLOSSEUM_VINYL_TEST_ROOT");
        const QFileInfo info(root);
        const QString temp = QFileInfo(QDir::tempPath()).canonicalFilePath();
        if (root.isEmpty() || root != QDir::cleanPath(root)
            || m_appDataRoot != QDir::cleanPath(m_appDataRoot)
            || !info.isAbsolute() || !info.isDir()
            || !info.fileName().startsWith("vinyl075-fixture-")
            || !noSymlinks(root) || !noSymlinks(m_appDataRoot)) return false;
        const QString canonical = info.canonicalFilePath();
        const QString actual = QFileInfo(m_appDataRoot).canonicalFilePath();
        if (temp.isEmpty() || !canonical.startsWith(temp + '/')
            || actual.isEmpty() || !actual.startsWith(canonical + '/')) return false;
        // Test data alone is insufficient: untagged/config-default QSettings
        // must never reach the operator's normal personal stores.
        for (const char *key : {"HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"}) {
            const QString location = qEnvironmentVariable(key);
            if (location != QDir::cleanPath(location) || !noSymlinks(location)) return false;
            const QString resolved = QFileInfo(location).canonicalFilePath();
            if (!QFileInfo(location).isDir() || !resolved.startsWith(canonical + '/')) return false;
        }
        // Do not follow profile/store symlinks even when the root itself is safe.
        QDirIterator descendants(canonical, QDir::AllEntries | QDir::Hidden | QDir::System
                                 | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int count = 0;
        while (descendants.hasNext()) {
            descendants.next();
            if (++count > 10000 || descendants.fileInfo().isSymLink()) return false;
        }
        const QString marker = QDir(canonical).filePath(markerName);
        const QFileInfo markerInfo(marker);
        if (!markerInfo.isFile() || markerInfo.isSymLink()
            || markerInfo.size() != qint64(sizeof("vinyl075-synthetic-fixture-v1\n") - 1)) return false;
        QFile file(marker);
        return file.open(QIODevice::ReadOnly) && file.readAll() == markerContents;
    }

    // Fixed launch-only scheduling delay; no command or persistent setting.
    int resolutionDelayMs() const {
        return qEnvironmentVariable("COLOSSEUM_VINYL_TEST_DELAY_RESOLVE") == "1"
            && enabled() ? 2000 : 0;
    }

    QJsonObject execute(const QJsonObject &payload) {
        if (!enabled()) return failure("fixture-disabled");
        if (payload.size() != 1 || !payload.value("action").isString())
            return failure("invalid-action");
        const QString action = payload.value("action").toString();
        if (action != "status" && action != "activate-a"
            && action != "activate-b" && action != "activate-local"
            && action != "activate-b-with-pending-write")
            return failure("invalid-action");
        if (!m_activate || !m_status) return failure("fixture-unbound");
        Status before = m_status();
        if (!validAlias(before.alias)) return failure("unrecognized-profile");
        if (action != "status") {
            const QString alias = action == "activate-a" ? "A"
                : (action == "activate-b" || action == "activate-b-with-pending-write") ? "B" : "local";
            const bool pending = action == "activate-b-with-pending-write";
            if (pending && before.alias == "B") return result(before, false, "pending-noop-refused");
            const bool activated = m_activate(alias, pending);
            const Status after = m_status();
            // The real owner can commit A->local before a later local->B failure.
            // Report that observed boundary rather than pretending atomic rollback.
            if (validAlias(after.alias) && after.alias != before.alias) ++m_generation;
            if (!activated) return result(after, false, "activation-failed");
            if (after.alias != alias) return result(after, false, "activation-unconfirmed");
            return result(after, true, "");
        }
        return result(before, true, "");
    }
private:
    static bool validAlias(const QString &s) { return s == "A" || s == "B" || s == "local"; }
    static bool noSymlinks(QString path) {
        if (!QFileInfo(path).isAbsolute()) return false;
        path = QDir::cleanPath(path);
        while (path != QDir::rootPath()) {
            const QFileInfo info(path);
            if (!info.exists() || info.isSymLink()) return false;
            const QString parent = info.absolutePath();
            if (parent == path) return false;
            path = parent;
        }
        return true;
    }
    QJsonObject failure(const QString &category) const {
        return {{"success", false}, {"errorCategory", category}};
    }
    QJsonObject result(const Status &s, bool success, const QString &category) const {
        if (!validAlias(s.alias)) return failure("unrecognized-profile");
        return {{"success", success}, {"errorCategory", category},
                {"profileAlias", s.alias}, {"generation", m_generation},
                {"collectionHealthy", s.collectionHealthy},
                {"progressHealthy", s.progressHealthy},
                {"activityHealthy", s.activityHealthy},
                {"pendingWriteAccepted", s.pendingWriteAccepted},
                {"pendingBeforeSwitch", s.pendingBeforeSwitch},
                {"pendingWriteCompleted", s.pendingWriteCompleted},
                {"pendingWriteSucceeded", s.pendingWriteSucceeded},
                {"pendingOwnerRetired", s.pendingOwnerRetired}};
    }
    QString m_appDataRoot;
    ActivateCallback m_activate;
    StatusCallback m_status;
    int m_generation = 0;
};
