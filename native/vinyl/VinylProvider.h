#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QPointer>
#include <QSharedPointer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>
#include <QVariant>
#include <QUrl>
#include <utility>

// Replaceable, request-correlated process transport; independent browse/play lanes.
// The existing completed signal stays stable for QML callers. Errors are categorized
// separately and no untrusted stderr, URL or helper exception becomes a UI message.
class VinylProvider : public QObject {
    Q_OBJECT
    struct Pending {
        int id = -1;
        QString operation;
        QString value;
        QPointer<QProcess> process;
        QByteArray output;
        bool settled = false;
    };
public:
    explicit VinylProvider(QString helper, QObject *parent = nullptr,
                           int browseTimeoutMs = 30000, int playTimeoutMs = 60000)
        : QObject(parent), m_helper(std::move(helper)),
          m_browseTimeoutMs(qMax(1, browseTimeoutMs)), m_playTimeoutMs(qMax(1, playTimeoutMs)) {
        connect(this, &VinylProvider::profileLeaving, this, &VinylProvider::cancelAll);
        connect(this, &VinylProvider::shuttingDown, this, &VinylProvider::cancelAll);
    }
    ~VinylProvider() override {
        cancelAll();
        // Shutdown only: reap killed children before QObject destroys their QProcess.
        for (auto *process : findChildren<QProcess *>()) {
            if (process->state() != QProcess::NotRunning) process->waitForFinished(250);
        }
    }

    Q_INVOKABLE int request(const QString &lane, const QString &operation, const QString &value) {
        if ((lane != "browse" && lane != "play") ||
            (operation != "search" && operation != "album" && operation != "resolve")) return -1;
        cancel(lane);
        const int id = ++m_serial;
        auto state = QSharedPointer<Pending>::create();
        state->id = id;
        state->operation = operation;
        state->value = value;
        auto *process = new QProcess(this);
        state->process = process;
        m_active.insert(lane, state);

        auto *deadline = new QTimer(process);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, this, [this, lane, state] {
            settle(lane, state, {}, "YouTube took too long. Try again.", "timeout");
        });
        connect(process, &QProcess::errorOccurred, this, [this, lane, state](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                settle(lane, state, {}, "Vinyl's Python helper could not start.", "start_failed");
            else
                settle(lane, state, {}, "Vinyl's provider helper failed.", "process_failed");
        });
        connect(process, &QProcess::readyReadStandardError, this, [process] {
            process->readAllStandardError(); // Never log credentials, signed URLs or provider traces.
        });
        connect(process, &QProcess::readyReadStandardOutput, this, [this, lane, state] {
            collectOutput(lane, state);
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this, lane, state](int exitCode, QProcess::ExitStatus status) {
            if (!collectOutput(lane, state)) return;
            if (exitCode != 0 || status != QProcess::NormalExit) {
                settle(lane, state, {}, "Vinyl's provider helper failed.", "process_failed");
                return;
            }
            QJsonParseError parseError;
            const auto doc = QJsonDocument::fromJson(state->output, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                settle(lane, state, {}, "Vinyl's provider reply was invalid.", "malformed_response");
                return;
            }
            const auto reply = doc.object();
            if (!reply.value("ok").isBool()) {
                settle(lane, state, {}, "Vinyl's provider reply was invalid.", "malformed_response");
                return;
            }
            if (!reply.value("ok").toBool()) {
                const auto code = safeCategory(reply.value("errorCode").toString());
                settle(lane, state, {}, safeMessage(code, reply.value("error").toString()), code);
                return;
            }
            const auto data = reply.value("data");
            if (!validPayload(state->operation, data)) {
                settle(lane, state, {}, "Vinyl's provider reply was invalid.", "malformed_response");
            } else if (state->operation == "resolve" &&
                       data.toObject().value("videoId").toString() != state->value) {
                settle(lane, state, {}, "Provider returned a different track.", "identity_mismatch");
            } else {
                settle(lane, state, data.toVariant(), {}, {});
            }
        });
        connect(process, &QProcess::started, this, [process, operation, value] {
            process->write(QJsonDocument(QJsonObject{{"operation", operation}, {"value", value}})
                               .toJson(QJsonDocument::Compact));
            process->closeWriteChannel();
        });
        const auto python = qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON", "python");
        process->start(python, {m_helper}); // argv only; never pass untrusted text to a shell.
        deadline->start(operation == "resolve" ? m_playTimeoutMs : m_browseTimeoutMs);
        return id; // Every completion is queued after request() returns.
    }

    Q_INVOKABLE void cancel(const QString &lane) {
        const auto state = m_active.take(lane);
        if (!state) return;
        state->settled = true;
        retireProcess(state->process.data());
        state->process = nullptr;
        // Cancellation intentionally suppresses completed, as existing QML retires
        // its request ID; queued success/error from the old profile is dropped too.
    }
    Q_INVOKABLE void cancelAll() {
        const auto lanes = m_active.keys();
        for (const auto &lane : lanes) cancel(lane);
    }
signals:
    void completed(const QString &lane, int requestId, const QVariant &data, const QString &error);
    void errorCategorized(const QString &lane, int requestId, const QString &category);
    void profileLeaving();
    void profileEntered();
    void checkpointRequested();
    void shuttingDown();
private:
    static constexpr qsizetype kMaxResponseBytes = 4 * 1024 * 1024;
    static bool validPayload(const QString &operation, const QJsonValue &data) {
        if (operation == "search") {
            if (!data.isArray()) return false;
            for (const auto &entry : data.toArray()) {
                if (!entry.isObject()) return false;
                const auto album = entry.toObject();
                if (!album.value("id").isString() ||
                    !album.value("browseId").isString() || album.value("browseId").toString().isEmpty() ||
                    !album.value("title").isString() || !album.value("artists").isArray())
                    return false;
            }
            return true;
        }
        if (!data.isObject()) return false;
        const auto record = data.toObject();
        if (operation == "album") {
            if (!record.value("id").isString() || !record.value("browseId").isString() ||
                !record.value("title").isString() || !record.value("artists").isArray() ||
                !record.value("tracks").isArray()) return false;
            const auto tracks = record.value("tracks").toArray();
            if (tracks.isEmpty()) return false;
            for (const auto &entry : tracks) {
                if (!entry.isObject()) return false;
                const auto track = entry.toObject();
                if (!track.value("id").isString() || !track.value("videoId").isString() ||
                    !track.value("title").isString() || !track.value("artists").isArray() ||
                    !track.value("seconds").isDouble() || !track.value("available").isBool())
                    return false;
            }
            return true;
        }
        if (operation == "resolve") {
            if (!record.value("videoId").isString() || !record.value("url").isString() ||
                !record.value("headers").isObject() || !record.value("qualityLabel").isString())
                return false;
            const QUrl location(record.value("url").toString());
            return location.isValid() && location.scheme() == "https" && !location.host().isEmpty();
        }
        return false;
    }
    void retireProcess(QProcess *process) {
        if (!process) return;
        process->disconnect(this);
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
            return;
        }
        // kill() is nonblocking. Deletion on the next event-loop turn can precede
        // the OS process exit, so only delete a running child once it is reaped.
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) process->deleteLater();
        });
        process->kill();
    }
    bool collectOutput(const QString &lane, const QSharedPointer<Pending> &state) {
        if (state->settled || m_active.value(lane) != state) return false;
        auto *process = state->process.data();
        if (!process) return false;
        const auto bytes = process->readAllStandardOutput();
        if (bytes.size() > kMaxResponseBytes - state->output.size()) {
            settle(lane, state, {}, "Provider response was too large.", "oversized_response");
            return false;
        }
        state->output.append(bytes);
        return true;
    }
    static QString safeCategory(const QString &category) {
        static const QSet<QString> allowed{
            "invalid_request", "dependency_missing", "provider_unavailable",
            "timeout", "incomplete_album", "not_found", "invalid_response",
            "identity_mismatch"
        };
        return allowed.contains(category) ? category : QStringLiteral("provider_unavailable");
    }
    static QString safeMessage(const QString &category, const QString &message) {
        if (category == "invalid_request") {
            if (message == "Enter an album or artist name" || message == "Invalid album identity" ||
                message == "Invalid YouTube video ID" || message == "Unknown operation")
                return message;
            return "Invalid catalogue request.";
        }
        if (category == "dependency_missing") return "Vinyl's catalogue helper is not installed.";
        if (category == "timeout") return "YouTube took too long. Try again.";
        if (category == "incomplete_album") return "Provider returned an incomplete album";
        if (category == "not_found") return "This album has no available track listing";
        if (category == "invalid_response") return "Provider returned an invalid catalogue entry.";
        if (category == "identity_mismatch") return "Provider returned a different track.";
        return "YouTube is unavailable for this request. Try again.";
    }
    void settle(const QString &lane, const QSharedPointer<Pending> &state,
                const QVariant &data, const QString &error, const QString &category) {
        if (state->settled || m_active.value(lane) != state) return;
        state->settled = true;
        retireProcess(state->process.data());
        state->process = nullptr;
        QTimer::singleShot(0, this, [this, lane, state, data, error, category] {
            if (m_active.value(lane) != state) return; // superseded or profile departed
            m_active.remove(lane);
            if (!category.isEmpty()) emit errorCategorized(lane, state->id, category);
            emit completed(lane, state->id, data, error);
        });
    }

    QString m_helper;
    int m_browseTimeoutMs;
    int m_playTimeoutMs;
    int m_serial = 0;
    QHash<QString, QSharedPointer<Pending>> m_active;
};
