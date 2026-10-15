#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

// Replaceable provider transport, with independent browse and playback lanes.
// No database, credentials, or persisted stream URLs live here.
class VinylProvider : public QObject {
    Q_OBJECT
public:
    explicit VinylProvider(QString helper, QObject *parent = nullptr)
        : QObject(parent), m_helper(std::move(helper)) {}
    ~VinylProvider() override { cancelAll(); }
    Q_INVOKABLE int request(const QString &lane, const QString &operation, const QString &value) {
        if ((lane != "browse" && lane != "play") ||
            (operation != "search" && operation != "album" && operation != "resolve")) return -1;
        cancel(lane);
        const int id = ++m_serial;
        auto *process = new QProcess(this);
        m_active.insert(lane, process);
        auto *deadline = new QTimer(process);
        deadline->setSingleShot(true);
        auto finish = [this, process, lane, id](const QVariant &data, const QString &error) {
            if (m_active.value(lane) != process) return;
            m_active.remove(lane);
            process->disconnect(this);
            if (process->state() != QProcess::NotRunning) process->kill();
            process->deleteLater();
            emit completed(lane, id, data, error);
        };
        connect(deadline, &QTimer::timeout, this, [finish] { finish({}, "YouTube took too long. Try again."); });
        connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) finish({}, "Vinyl's Python helper could not start.");
        });
        connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
        connect(process, &QProcess::readyReadStandardOutput, this, [process, finish] {
            if (process->bytesAvailable() > 4 * 1024 * 1024) finish({}, "Provider response was too large.");
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [process, finish](int code, QProcess::ExitStatus status) {
            const auto doc = QJsonDocument::fromJson(process->readAllStandardOutput());
            const auto object = doc.object();
            if (code != 0 || status != QProcess::NormalExit || !object.contains("ok"))
                finish({}, "Vinyl's provider helper failed.");
            else if (!object.value("ok").toBool())
                finish({}, object.value("error").toString("YouTube request failed."));
            else finish(object.value("data").toVariant(), {});
        });
        const auto python = qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON", "python");
        process->start(python, {m_helper}); // argv, never a shell command
        process->write(QJsonDocument(QJsonObject{{"operation", operation}, {"value", value}}).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
        deadline->start(operation == "resolve" ? 60000 : 30000);
        return id;
    }
    Q_INVOKABLE void cancel(const QString &lane) {
        auto *process = m_active.take(lane);
        if (!process) return;
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
        process->deleteLater();
    }
    Q_INVOKABLE void cancelAll() { const auto lanes = m_active.keys(); for (const auto &lane : lanes) cancel(lane); }
signals:
    void completed(const QString &lane, int requestId, const QVariant &data, const QString &error);
    void profileLeaving();
    void profileEntered();
    void checkpointRequested();
    void shuttingDown();
private:
    QString m_helper;
    int m_serial = 0;
    QHash<QString, QProcess *> m_active;
};
