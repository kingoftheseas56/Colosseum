#include "AppLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>

namespace {

// 5 MB per file, 3 previous runs kept — a full boot+browse session is well
// under a megabyte, so this holds days of ordinary use and still bounds a
// pathological warning loop.
constexpr qint64 kMaxBytes = 5 * 1024 * 1024;
constexpr int    kKeep     = 3;

QMutex          g_mutex;
QFile*          g_file = nullptr;   // deliberately leaked: late log lines must
                                    // outlive any static destructor ordering
QtMessageHandler g_prev = nullptr;
qint64          g_bytes = 0;
QString         g_dir;
QString         g_path;
bool            g_installed = false;

QString rolledPath(int n)   // n == 0 is the live file
{
    return n == 0 ? g_path : QStringLiteral("%1/colosseum.%2.log").arg(g_dir).arg(n);
}

// Caller holds g_mutex.
void openLocked()
{
    if (!g_file)
        g_file = new QFile(g_path);
    if (g_file->isOpen())
        return;
    if (g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        g_bytes = g_file->size();
    else
        g_bytes = 0;
}

// Caller holds g_mutex. colosseum.log -> .1 -> .2 -> .3 -> dropped.
void rotateLocked()
{
    if (!g_file)
        return;
    g_file->close();
    QFile::remove(rolledPath(kKeep));
    for (int n = kKeep - 1; n >= 0; --n) {
        const QString from = rolledPath(n);
        if (QFile::exists(from))
            QFile::rename(from, rolledPath(n + 1));
    }
    g_file->setFileName(g_path);
    g_bytes = 0;
    openLocked();
}

char levelChar(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return 'D';
    case QtInfoMsg:     return 'I';
    case QtWarningMsg:  return 'W';
    case QtCriticalMsg: return 'C';
    case QtFatalMsg:    return 'F';
    }
    return '?';
}

QString formatLine(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    line += QStringLiteral(" [");
    line += QLatin1Char(levelChar(type));
    line += QStringLiteral("] ");
    if (ctx.category && qstrcmp(ctx.category, "default") != 0) {
        line += QString::fromLatin1(ctx.category);
        line += QStringLiteral(": ");
    }
    line += msg;
    // Source location is noise on info lines and gold on a warning — keep it
    // only where someone is actually going to go looking.
    if (type >= QtWarningMsg && ctx.file)
        line += QStringLiteral("   (%1:%2)").arg(QString::fromLatin1(ctx.file)).arg(ctx.line);
    line += QLatin1Char('\n');
    return line;
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    const QString line = formatLine(type, ctx, msg);
    {
        QMutexLocker lock(&g_mutex);
        if (g_file && g_file->isOpen()) {
            const QByteArray bytes = line.toUtf8();
            g_bytes += g_file->write(bytes);
            // Flush every line: this log exists for the hard-kill case, and a
            // buffered tail is precisely the part worth having.
            g_file->flush();
            if (g_bytes >= kMaxBytes)
                rotateLocked();
        }
    }
    // Console/stderr behaviour must be unchanged for the dev loop.
    if (g_prev) {
        g_prev(type, ctx, msg);
        return;   // the delegate owns fatal handling too
    }
    const QByteArray stderrBytes = line.toUtf8();
    const int writeStatus = std::fputs(stderrBytes.constData(), stderr);
    const int flushStatus = std::fflush(stderr);
    if (writeStatus == EOF || flushStatus == EOF)
        std::clearerr(stderr); // allow a later fallback write to retry cleanly
    if (type == QtFatalMsg)
        std::abort();   // Qt's default handler aborts; replicate it
}

}  // namespace

void AppLog::install()
{
    QString path;
    {
        QMutexLocker lock(&g_mutex);
        if (g_installed)
            return;
        g_installed = true;
        g_dir  = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/logs");
        QDir().mkpath(g_dir);
        g_path = g_dir + QStringLiteral("/colosseum.log");
        openLocked();
        if (g_bytes >= kMaxBytes)
            rotateLocked();
        if (g_file && g_file->isOpen()) {
            // Written directly, not through qInfo — the handler isn't installed
            // yet, and routing it through one we hold the lock for would deadlock.
            const QString banner =
                QStringLiteral("\n==== session start %1 · %2 · pid %3 ====\n")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                         QCoreApplication::applicationName())
                    .arg(QCoreApplication::applicationPid());
            const QByteArray bytes = banner.toUtf8();
            g_bytes += g_file->write(bytes);
            g_file->flush();
        }
        g_prev = qInstallMessageHandler(handler);
        path = g_path;
    }
    qInfo("[applog] logging to %s", qUtf8Printable(path));
}

QString AppLog::logDir()
{
    QMutexLocker lock(&g_mutex);
    return g_dir;
}

QString AppLog::currentLogPath()
{
    QMutexLocker lock(&g_mutex);
    return g_path;
}
