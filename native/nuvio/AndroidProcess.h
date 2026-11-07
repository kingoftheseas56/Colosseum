#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class QSocketNotifier;
class QTimer;

// The Android transport deliberately has the same narrow interface used by the
// desktop resolver. The Java bridge binds a private QtService, never a thread
// inside the activity process. Each invocation owns a fresh process and pipe.
class NuvioAndroidProcess final : public QObject
{
    Q_OBJECT
public:
    using ProcessError = QProcess::ProcessError;
    using ExitStatus = QProcess::ExitStatus;
    using ProcessState = QProcess::ProcessState;
    static constexpr auto FailedToStart = QProcess::FailedToStart;
    static constexpr auto NotRunning = QProcess::NotRunning;
    static constexpr auto NormalExit = QProcess::NormalExit;
    static constexpr auto SeparateChannels = QProcess::SeparateChannels;

    explicit NuvioAndroidProcess(QObject *parent = nullptr);
    ~NuvioAndroidProcess() override;
    void setProgram(const QString &program) { m_program = program; }
    void setArguments(const QStringList &arguments) { m_arguments = arguments; }
    void setProcessEnvironment(const QProcessEnvironment &) {}
    void setProcessChannelMode(QProcess::ProcessChannelMode) {}
    void start(QIODevice::OpenMode mode);
    void kill();
    ProcessState state() const { return m_state; }
    qint64 processId() const { return m_pid; }
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
    int slotIndex() const { return m_slot; }
    qint64 requestMonotonicMs() const { return m_requestMonotonicMs; }
#endif
    QByteArray readAllStandardOutput();
    QByteArray readAllStandardError() { return {}; }

signals:
    void started();
    void readyReadStandardOutput();
    void readyReadStandardError();
    void errorOccurred(QProcess::ProcessError error);
    void finished(int exitCode, QProcess::ExitStatus status);

private:
    void readPipe();
    void cleanup(bool cancel);
    void failStart();
    QString m_program;
    QStringList m_arguments;
    QByteArray m_output;
    ProcessState m_state = NotRunning;
    int m_fd = -1;
    int m_job = -1;
    int m_slot = -1;
    qint64 m_pid = 0;
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
    qint64 m_requestMonotonicMs = 0;
#endif
    bool m_received = false;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_pidTimer = nullptr;
};
