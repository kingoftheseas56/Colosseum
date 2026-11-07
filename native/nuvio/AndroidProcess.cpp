#include "AndroidProcess.h"

#include "NuvioWorkerProtocol.h"

#include <QCoreApplication>
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
#include <QElapsedTimer>
#endif
#include <QJniEnvironment>
#include <QJniObject>
#include <QSocketNotifier>
#include <QTimer>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr auto bridge = "org/colosseum/nuvio/NuvioProcessBridge";
// All resolver and IPC callbacks are on the application's Qt thread.
std::array<bool, 4> workerSlots{};
}

NuvioAndroidProcess::NuvioAndroidProcess(QObject *parent) : QObject(parent) {}

NuvioAndroidProcess::~NuvioAndroidProcess()
{
    cleanup(true);
}

void NuvioAndroidProcess::failStart()
{
    cleanup(true);
    QTimer::singleShot(0, this, [this] { emit errorOccurred(FailedToStart); });
}

void NuvioAndroidProcess::start(QIODevice::OpenMode)
{
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
    QElapsedTimer clock;
    clock.start();
    m_requestMonotonicMs = clock.msecsSinceReference();
#endif
    if (m_state != NotRunning || m_program != QLatin1String("android:private-nuvio-service")) {
        failStart();
        return;
    }
    for (int i = 0; i < int(workerSlots.size()); ++i) {
        if (!workerSlots[i]) {
            workerSlots[i] = true;
            m_slot = i;
            break;
        }
    }
    if (m_slot < 0) {
        failStart();
        return;
    }
    QJniEnvironment env;
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass || !context.isValid()) {
        env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent);
        if (stringClass) env->DeleteLocalRef(stringClass);
        failStart();
        return;
    }
    jobjectArray args = env->NewObjectArray(m_arguments.size(), stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (!args) {
        env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent);
        failStart();
        return;
    }
    for (int i = 0; i < m_arguments.size(); ++i) {
        const auto value = QJniObject::fromString(m_arguments[i]);
        env->SetObjectArrayElement(args, i, value.object<jstring>());
    }
    const auto result = QJniObject::callStaticObjectMethod(bridge, "start",
        "(Landroid/content/Context;[Ljava/lang/String;I)[I", context.object(), args, jint(m_slot));
    env->DeleteLocalRef(args);
    if (!result.isValid() || env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent)) {
        failStart();
        return;
    }
    const auto values = result.object<jintArray>();
    if (env->GetArrayLength(values) != 2) {
        failStart();
        return;
    }
    jint pair[2] = {-1, -1};
    env->GetIntArrayRegion(values, 0, 2, pair);
    m_fd = pair[0];
    m_job = pair[1];
    if (m_fd < 0 || m_job < 0 || fcntl(m_fd, F_SETFL, O_NONBLOCK) < 0) {
        failStart();
        return;
    }
    m_state = QProcess::Starting;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { readPipe(); });
    m_pidTimer = new QTimer(this);
    connect(m_pidTimer, &QTimer::timeout, this, [this] {
        const int pid = QJniObject::callStaticMethod<jint>(bridge, "processId", "(I)I", jint(m_job));
        if (pid > 0 && pid != getpid()) {
            m_pid = pid;
            m_state = QProcess::Running;
            m_pidTimer->stop();
            emit started();
        }
    });
    m_pidTimer->start(25);
}

QByteArray NuvioAndroidProcess::readAllStandardOutput()
{
    QByteArray result;
    result.swap(m_output);
    return result;
}

void NuvioAndroidProcess::readPipe()
{
    char bytes[16384];
    while (m_fd >= 0) {
        const auto count = ::read(m_fd, bytes, sizeof(bytes));
        if (count > 0) {
            m_received = true;
            m_output.append(bytes, count);
            emit readyReadStandardOutput();
            if (m_fd < 0) return; // output guard may cancel synchronously
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        // Preserve the PID even if a fast host exits before the polling timer.
        if (!m_pid) {
            m_pid = QJniObject::callStaticMethod<jint>(bridge, "processId", "(I)I", jint(m_job));
            if (m_pid > 0) emit started();
        }
        const bool completed = count == 0 && m_received;
        cleanup(false);
        emit finished(completed ? 0 : -1, completed ? NormalExit : QProcess::CrashExit);
        return;
    }
}

void NuvioAndroidProcess::kill()
{
    if (m_state == NotRunning) return;
    cleanup(true);
    // QProcess completion is asynchronous. Preserve that contract on cancel.
    QTimer::singleShot(0, this, [this] { emit finished(-1, QProcess::CrashExit); });
}

void NuvioAndroidProcess::cleanup(bool cancel)
{
    if (m_notifier) { m_notifier->setEnabled(false); m_notifier->deleteLater(); m_notifier = nullptr; }
    if (m_pidTimer) { m_pidTimer->stop(); m_pidTimer->deleteLater(); m_pidTimer = nullptr; }
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
    if (m_job >= 0) {
        QJniObject::callStaticMethod<void>(bridge, cancel ? "cancel" : "finish", "(I)V", jint(m_job));
        m_job = -1;
    }
    if (m_slot >= 0) { workerSlots[m_slot] = false; m_slot = -1; }
    m_state = NotRunning;
}
