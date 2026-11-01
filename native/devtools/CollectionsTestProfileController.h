#pragma once

// Build- and launch-gated profile fixture for the assembled Collections checks.
// It exposes three fixed synthetic identities, never arbitrary account IDs,
// store paths, QML methods, reference insertion, or media/player operations.
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <functional>
#include <utility>

class CollectionsTestProfileController final {
public:
    using Activate = std::function<bool(const QString &)>;
    using Status = std::function<QJsonObject()>;
    static constexpr const char *markerName = ".collections-synthetic-fixture";
    static constexpr const char *markerContents = "collections-synthetic-fixture-v1\n";

    CollectionsTestProfileController(QString appDataRoot, Activate activate, Status status)
        : m_root(std::move(appDataRoot)), m_activate(std::move(activate)),
          m_status(std::move(status)) {}

    bool enabled() const {
        const QString tag = qEnvironmentVariable("COLOSSEUM_APPDATA_TAG");
        if (qEnvironmentVariable("COLOSSEUM_COLLECTIONS_TEST_FIXTURES") != QLatin1String("1")
            || !QRegularExpression(QStringLiteral("^collections-fixture-[A-Za-z0-9_-]{1,48}$"))
                    .match(tag).hasMatch()) return false;
        const QFileInfo root(m_root);
        if (!root.isAbsolute() || !root.isDir() || root.isSymLink()
            || root.fileName() != QStringLiteral("Colosseum-dltest-") + tag
            || QDir::cleanPath(m_root) != m_root) return false;
        QString current = m_root;
        for (;;) {
            const QFileInfo node(current);
            if (!node.exists() || node.isSymLink()) return false;
            const QString parent = node.absolutePath();
            if (parent == current) break;
            current = parent;
        }
        QDirIterator descendants(m_root, QDir::AllEntries | QDir::Hidden | QDir::System
            | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        int count = 0;
        while (descendants.hasNext()) {
            descendants.next();
            if (++count > 10000 || descendants.fileInfo().isSymLink()) return false;
        }
        const QFileInfo marker(QDir(m_root).filePath(QString::fromLatin1(markerName)));
        if (!marker.isFile() || marker.isSymLink()
            || marker.size() != qint64(sizeof("collections-synthetic-fixture-v1\n") - 1))
            return false;
        QFile file(marker.absoluteFilePath());
        return file.open(QIODevice::ReadOnly) && file.readAll() == markerContents;
    }

    QJsonObject execute(const QJsonObject &request) {
        if (!enabled()) return failure(QStringLiteral("fixture-disabled"));
        if (request.size() != 1 || !request.value("action").isString())
            return failure(QStringLiteral("invalid-action"));
        const QString action = request.value("action").toString();
        if (action != QLatin1String("status") && action != QLatin1String("activate-local")
            && action != QLatin1String("activate-a") && action != QLatin1String("activate-b"))
            return failure(QStringLiteral("invalid-action"));
        if (!m_activate || !m_status) return failure(QStringLiteral("fixture-unbound"));
        QJsonObject state = m_status();
        if (!validAlias(state.value("profileAlias").toString()))
            return failure(QStringLiteral("unrecognized-profile"));
        if (action != QLatin1String("status")) {
            const QString alias = action == QLatin1String("activate-a") ? QStringLiteral("A")
                : action == QLatin1String("activate-b") ? QStringLiteral("B") : QStringLiteral("local");
            if (!m_activate(alias)) return failure(QStringLiteral("activation-failed"));
            state = m_status();
            if (state.value("profileAlias").toString() != alias)
                return failure(QStringLiteral("activation-unconfirmed"));
        }
        state.insert("success", true);
        return state;
    }

private:
    static bool validAlias(const QString &alias) {
        return alias == QLatin1String("local") || alias == QLatin1String("A") || alias == QLatin1String("B");
    }
    static QJsonObject failure(const QString &error) {
        return {{"success", false}, {"errorCategory", error}};
    }
    QString m_root;
    Activate m_activate;
    Status m_status;
};
