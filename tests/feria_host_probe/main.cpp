#include "FeriaHostItem.h"

#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

QPixmap printWindow(HWND hwnd)
{
    RECT rect{};
    if (!hwnd || !GetWindowRect(hwnd, &rect))
        return {};
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return {};

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                       &pixels, nullptr, 0);
    QPixmap result;
    if (memory && bitmap && pixels) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        if (PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT)) {
            const QImage image(static_cast<const uchar *>(pixels), width,
                               height, QImage::Format_RGB32);
            result = QPixmap::fromImage(image.copy());
        }
        SelectObject(memory, old);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (memory)
        DeleteDC(memory);
    if (screen)
        ReleaseDC(nullptr, screen);
    return result;
}

BOOL CALLBACK findPopup(HWND hwnd, LPARAM context)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd))
        return TRUE;
    wchar_t title[128]{};
    GetWindowTextW(hwnd, title, 128);
    if (QString::fromWCharArray(title) == QStringLiteral("Feria Provider Popup")) {
        *reinterpret_cast<HWND *>(context) = hwnd;
        return FALSE;
    }
    return TRUE;
}

QJsonArray browserProcesses()
{
    QJsonArray processes;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString name =
                QString::fromWCharArray(entry.szExeFile).toLower();
            if (name == QStringLiteral("chrome.exe")
                || name == QStringLiteral("msedge.exe")
                || name == QStringLiteral("firefox.exe")
                || name == QStringLiteral("brave.exe")
                || name == QStringLiteral("opera.exe")
                || name == QStringLiteral("vivaldi.exe")
                || name == QStringLiteral("iexplore.exe")
                || name == QStringLiteral("arc.exe")) {
                processes.append(QJsonObject{
                    {QStringLiteral("pid"), static_cast<qint64>(entry.th32ProcessID)},
                    {QStringLiteral("parentPid"),
                     static_cast<qint64>(entry.th32ParentProcessID)},
                    {QStringLiteral("name"), name}
                });
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processes;
}

} // namespace

class ProbeController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString profilePath READ profilePath CONSTANT)
    Q_PROPERTY(QString initialUrl READ initialUrl CONSTANT)
    Q_PROPERTY(QString automation READ automation CONSTANT)
    Q_PROPERTY(QString evidenceDir READ evidenceDir CONSTANT)
    Q_PROPERTY(QString marker READ marker CONSTANT)
    Q_PROPERTY(QString scaleLabel READ scaleLabel CONSTANT)
    Q_PROPERTY(qint64 pid READ pid CONSTANT)

public:
    ProbeController(QString profilePath,
                    QString initialUrl,
                    QString automation,
                    QString evidenceDir,
                    QString marker,
                    QString scaleLabel,
                    QObject *parent = nullptr)
        : QObject(parent)
        , m_profilePath(std::move(profilePath))
        , m_initialUrl(std::move(initialUrl))
        , m_automation(std::move(automation))
        , m_evidenceDir(std::move(evidenceDir))
        , m_marker(std::move(marker))
        , m_scaleLabel(std::move(scaleLabel))
        , m_pid(static_cast<qint64>(GetCurrentProcessId()))
    {
        QDir().mkpath(m_evidenceDir);
        m_prefix = m_automation.startsWith(QStringLiteral("a2-"))
            ? QStringLiteral("a2") : QStringLiteral("a1");
        m_eventPath = QDir(m_evidenceDir).filePath(
            QStringLiteral("%1-%2-events.jsonl").arg(m_prefix).arg(m_pid));
        record(QStringLiteral("probe-start"),
               QStringLiteral("automation=%1 scale=%2 profile=%3 url=%4")
                   .arg(m_automation, m_scaleLabel,
                        m_profilePath, m_initialUrl));
        m_browserWatch.setInterval(100);
        connect(&m_browserWatch, &QTimer::timeout, this, [this]() {
            for (const QJsonValue &value : browserProcesses()) {
                const QJsonObject process = value.toObject();
                const qint64 processId =
                    process.value(QStringLiteral("pid")).toInteger();
                if (m_seenBrowserPids.contains(processId))
                    continue;
                m_seenBrowserPids.insert(processId);
                record(QStringLiteral("browser-process-observed"),
                       QString::fromUtf8(
                           QJsonDocument(process).toJson(QJsonDocument::Compact)));
            }
        });
    }

    QString profilePath() const { return m_profilePath; }
    QString initialUrl() const { return m_initialUrl; }
    QString automation() const { return m_automation; }
    QString evidenceDir() const { return m_evidenceDir; }
    QString marker() const { return m_marker; }
    QString scaleLabel() const { return m_scaleLabel; }
    qint64 pid() const { return m_pid; }

    Q_INVOKABLE void record(const QString &event,
                            const QString &payload = QString())
    {
        QJsonObject entry{
            {QStringLiteral("timestamp"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("pid"), m_pid},
            {QStringLiteral("event"), event},
            {QStringLiteral("payload"), payload}
        };
        const QByteArray line =
            QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';

        QFile file(m_eventPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            file.write(line);
            file.close();
        }
        qInfo().noquote() << "FERIA_A1_PROBE" << event << payload;
    }

    Q_INVOKABLE void observe(const QString &json)
    {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            record(QStringLiteral("observer-invalid"), error.errorString());
            return;
        }
        QJsonObject object = document.object();
        if (object.value(QStringLiteral("kind")).toString()
            != QStringLiteral("feria-youtube-observation-v1"))
            return;
        const QUrl url(object.value(QStringLiteral("href")).toString());
        const QString host = url.host().toLower();
        const QString id = (host == QStringLiteral("youtube.com")
                            || host.endsWith(QStringLiteral(".youtube.com")))
                           && url.path() == QStringLiteral("/watch")
            ? QUrlQuery(url).queryItemValue(QStringLiteral("v")) : QString();
        const QJsonObject media = object.value(QStringLiteral("media")).toObject();
        const bool ad = object.value(QStringLiteral("adShowing")).toBool()
            || object.value(QStringLiteral("adInterrupting")).toBool();
        const bool paused = media.value(QStringLiteral("paused")).toBool(true);
        const bool ended = media.value(QStringLiteral("ended")).toBool();
        const double position = media.value(QStringLiteral("currentTime")).toDouble(-1);
        const double duration = media.value(QStringLiteral("duration")).toDouble(-1);
        const QString title = object.value(QStringLiteral("title")).toString().trimmed();
        object.insert(QStringLiteral("videoId"), id);
        record(QStringLiteral("observer-sample"),
               QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));

        if (id != m_observedId) {
            m_observedId = id;
            m_lastPosition = -1;
            m_advances = 0;
            m_qualified = false;
            m_completed = false;
        }
        if (id.isEmpty() || title.isEmpty() || ad || media.isEmpty() || position < 0
            || duration <= 0 || position > duration + 3) {
            m_lastPosition = -1;
            m_advances = 0;
            return;
        }
        if (ended) {
            if (m_qualified && !m_completed) {
                m_completed = true;
                record(QStringLiteral("observer-completed"),
                       QStringLiteral("id=%1 position=%2 duration=%3")
                           .arg(id).arg(position).arg(duration));
            }
            m_lastPosition = -1;
            m_advances = 0;
            return;
        }
        if (paused) {
            m_lastPosition = -1;
            m_advances = 0;
            return;
        }
        if (m_lastPosition >= 0) {
            const double delta = position - m_lastPosition;
            if (delta >= 0.4 && delta <= 3.5)
                ++m_advances;
            else
                m_advances = 0;
        }
        m_lastPosition = position;
        if (m_advances >= 2 && !m_qualified) {
            m_qualified = true;
            record(QStringLiteral("observer-qualified"),
                   QStringLiteral("id=%1 position=%2 duration=%3 title=%4")
                       .arg(id).arg(position).arg(duration)
                       .arg(object.value(QStringLiteral("title")).toString()));
        }
    }

    Q_INVOKABLE QString capture(QObject *windowObject,
                                const QString &label)
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        if (!window)
            return QString();
        return captureHwnd(reinterpret_cast<HWND>(window->winId()), label);
    }

    Q_INVOKABLE QString capturePopup(const QString &label)
    {
        HWND popup = nullptr;
        EnumWindows(findPopup, reinterpret_cast<LPARAM>(&popup));
        return captureHwnd(popup, label);
    }

    Q_INVOKABLE bool hover(QObject *windowObject, int logicalX, int logicalY)
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        const HWND hwnd = window
            ? reinterpret_cast<HWND>(window->winId()) : nullptr;
        if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
            return false;
        POINT point{static_cast<LONG>(logicalX * window->devicePixelRatio()),
                    static_cast<LONG>(logicalY * window->devicePixelRatio())};
        RECT client{};
        GetClientRect(hwnd, &client);
        if (point.x < 0 || point.y < 0
            || point.x >= client.right || point.y >= client.bottom)
            return false;
        GetCursorPos(&m_priorCursor);
        m_hasPriorCursor = true;
        ClientToScreen(hwnd, &point);
        const bool moved = SetCursorPos(point.x, point.y) != FALSE;
        record(QStringLiteral("probe-hover"),
               QStringLiteral("moved=%1 hwnd=0x%2 x=%3 y=%4")
                   .arg(moved).arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                   .arg(point.x).arg(point.y));
        return moved;
    }

    Q_INVOKABLE void restoreCursor()
    {
        if (m_hasPriorCursor)
            SetCursorPos(m_priorCursor.x, m_priorCursor.y);
        m_hasPriorCursor = false;
    }

    Q_INVOKABLE void beginBrowserWatch()
    {
        const QJsonArray before = browserProcesses();
        for (const QJsonValue &value : before)
            m_seenBrowserPids.insert(
                value.toObject().value(QStringLiteral("pid")).toInteger());
        saveBrowserSnapshot(QStringLiteral("before"), before);
        m_browserWatch.start();
    }

    Q_INVOKABLE void endBrowserWatch()
    {
        m_browserWatch.stop();
        saveBrowserSnapshot(QStringLiteral("after"), browserProcesses());
    }

    Q_INVOKABLE bool activateForFocusProof(QObject *windowObject)
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        const HWND hwnd = window
            ? reinterpret_cast<HWND>(window->winId()) : nullptr;
        if (!hwnd)
            return false;
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
            record(QStringLiteral("focus-proof-window-restored"));
        }
        if (GetForegroundWindow() != hwnd) {
            const HWND previous = GetForegroundWindow();
            const DWORD previousThread = GetWindowThreadProcessId(previous, nullptr);
            const DWORD currentThread = GetCurrentThreadId();
            const bool attached = previousThread && previousThread != currentThread
                && AttachThreadInput(currentThread, previousThread, TRUE);
            const BOOL requested = SetForegroundWindow(hwnd);
            if (attached)
                AttachThreadInput(currentThread, previousThread, FALSE);
            record(QStringLiteral("focus-proof-activation"),
                   QStringLiteral("previous=0x%1 requested=%2 attached=%3 foreground=0x%4")
                       .arg(reinterpret_cast<quintptr>(previous), 0, 16)
                       .arg(requested != FALSE).arg(attached)
                       .arg(reinterpret_cast<quintptr>(GetForegroundWindow()), 0, 16));
        }
        return GetForegroundWindow() == hwnd && !IsIconic(hwnd);
    }

    Q_INVOKABLE bool webViewHasFocus(QObject *windowObject)
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        const HWND hwnd = window
            ? reinterpret_cast<HWND>(window->winId()) : nullptr;
        const HWND focused = GetFocus();
        wchar_t className[128]{};
        if (focused)
            GetClassNameW(focused, className, 128);
        return hwnd && GetForegroundWindow() == hwnd && !IsIconic(hwnd)
            && focused && IsChild(hwnd, focused)
            && QString::fromWCharArray(className).startsWith(
                QStringLiteral("Chrome_"));
    }

    Q_INVOKABLE bool sendKey(int virtualKey, QObject *windowObject)
    {
        auto *window = qobject_cast<QQuickWindow *>(windowObject);
        const HWND hwnd = window
            ? reinterpret_cast<HWND>(window->winId()) : nullptr;
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foreground, &foregroundPid);
        const bool targeted = hwnd && foreground == hwnd && !IsIconic(hwnd)
            && webViewHasFocus(windowObject);
        if (!targeted) {
            record(QStringLiteral("send-key-skipped"),
                   QStringLiteral("vk=%1 probe=0x%2 foreground=0x%3 foregroundPid=%4")
                       .arg(virtualKey)
                       .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                       .arg(reinterpret_cast<quintptr>(foreground), 0, 16)
                       .arg(foregroundPid));
            return false;
        }
        INPUT input[2]{};
        input[0].type = INPUT_KEYBOARD;
        input[0].ki.wVk = static_cast<WORD>(virtualKey);
        input[1].type = INPUT_KEYBOARD;
        input[1].ki.wVk = static_cast<WORD>(virtualKey);
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT sent = SendInput(2, input, sizeof(INPUT));
        record(QStringLiteral("send-key"),
               QStringLiteral("vk=%1 sent=%2 foreground=0x%3")
                   .arg(virtualKey).arg(sent)
                   .arg(reinterpret_cast<quintptr>(foreground), 0, 16));
        return sent == 2;
    }

    Q_INVOKABLE void quit()
    {
        record(QStringLiteral("probe-exit"));
        QCoreApplication::quit();
    }

private:
    void saveBrowserSnapshot(const QString &label, const QJsonArray &processes)
    {
        const QString path = QDir(m_evidenceDir).filePath(
            QStringLiteral("a1-%1-policy-browser-%2.json")
                .arg(m_pid).arg(label));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            record(QStringLiteral("browser-snapshot-failed"), path);
            return;
        }
        file.write(QJsonDocument(QJsonObject{
            {QStringLiteral("probePid"), m_pid},
            {QStringLiteral("timestamp"),
             QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("processes"), processes}
        }).toJson(QJsonDocument::Indented));
        file.close();
        record(QStringLiteral("browser-snapshot"),
               QStringLiteral("label=%1 path=%2 count=%3")
                   .arg(label, path).arg(processes.size()));
    }

    QString captureHwnd(HWND hwnd, const QString &label)
    {
        if (!hwnd) {
            record(QStringLiteral("capture-failed"),
                   QStringLiteral("missing HWND label=%1").arg(label));
            return {};
        }
        const QPixmap pixmap = printWindow(hwnd);
        if (pixmap.isNull()) {
            record(QStringLiteral("capture-failed"),
                   QStringLiteral("PrintWindow failed label=%1 hwnd=0x%2")
                       .arg(label)
                       .arg(reinterpret_cast<quintptr>(hwnd), 0, 16));
            return {};
        }

        const QString safeLabel = QString(label)
            .replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")),
                     QStringLiteral("_"));
        const QString path = QDir(m_evidenceDir).filePath(
            QStringLiteral("%1-%2-%3-%4.png")
                .arg(m_prefix).arg(m_pid)
                .arg(m_scaleLabel, safeLabel));
        if (!pixmap.save(path, "PNG")) {
            record(QStringLiteral("capture-failed"), path);
            return QString();
        }

        QFile file(path);
        QString hash;
        if (file.open(QIODevice::ReadOnly)) {
            hash = QString::fromLatin1(
                QCryptographicHash::hash(
                    file.readAll(), QCryptographicHash::Sha256).toHex());
        }
        record(QStringLiteral("capture"),
               QStringLiteral("path=%1 sha256=%2 hwnd=0x%3 width=%4 height=%5")
                   .arg(path, hash)
                   .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                   .arg(pixmap.width()).arg(pixmap.height()));
        return path;
    }
    QString m_profilePath;
    QString m_initialUrl;
    QString m_automation;
    QString m_evidenceDir;
    QString m_marker;
    QString m_scaleLabel;
    QString m_eventPath;
    QString m_prefix;
    QString m_observedId;
    double m_lastPosition = -1;
    int m_advances = 0;
    bool m_qualified = false;
    bool m_completed = false;
    POINT m_priorCursor{};
    bool m_hasPriorCursor = false;
    QTimer m_browserWatch;
    QSet<qint64> m_seenBrowserPids;
    qint64 m_pid = 0;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("feria_host_probe"));
    QCoreApplication::setOrganizationName(QStringLiteral("Colosseum"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Feria A1 Qt Quick native WebView2 host probe"));
    parser.addHelpOption();

    QCommandLineOption profileOption(
        QStringList{QStringLiteral("profile")},
        QStringLiteral("WebView2 user-data folder."),
        QStringLiteral("path"));
    QCommandLineOption urlOption(
        QStringList{QStringLiteral("url")},
        QStringLiteral("Initial provider URL."),
        QStringLiteral("url"),
        QStringLiteral("https://www.youtube.com/watch?v=jNQXAC9IVRw"));
    QCommandLineOption automationOption(
        QStringList{QStringLiteral("automation")},
        QStringLiteral("Automation scenario."),
        QStringLiteral("name"),
        QStringLiteral("manual"));
    QCommandLineOption evidenceOption(
        QStringList{QStringLiteral("evidence")},
        QStringLiteral("Evidence output directory."),
        QStringLiteral("path"));
    QCommandLineOption markerOption(
        QStringList{QStringLiteral("marker")},
        QStringLiteral("Profile persistence marker."),
        QStringLiteral("value"),
        QStringLiteral("feria-a1-persisted-20260925"));
    QCommandLineOption scaleOption(
        QStringList{QStringLiteral("scale-label")},
        QStringLiteral("Evidence scale label."),
        QStringLiteral("value"),
        QStringLiteral("100"));

    parser.addOptions({profileOption, urlOption, automationOption,
                       evidenceOption, markerOption, scaleOption});
    parser.process(app);

    const QString localAppData =
        qEnvironmentVariable("LOCALAPPDATA",
            QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation));
    const QString defaultRoot = QDir(localAppData).filePath(
        QStringLiteral("Colosseum/feria-host-probe"));
    const QString profile = parser.value(profileOption).isEmpty()
        ? QDir(defaultRoot).filePath(QStringLiteral("profile-a"))
        : QDir::cleanPath(parser.value(profileOption));
    const QString evidence = parser.value(evidenceOption).isEmpty()
        ? QDir(defaultRoot).filePath(QStringLiteral("evidence"))
        : QDir::cleanPath(parser.value(evidenceOption));

    qmlRegisterType<FeriaHostItem>(
        "Colosseum.FeriaHost", 1, 0, "FeriaHostItem");

    ProbeController probe(
        profile,
        parser.value(urlOption),
        parser.value(automationOption),
        evidence,
        parser.value(markerOption),
        parser.value(scaleOption));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        QStringLiteral("probe"), &probe);
    engine.load(QUrl::fromLocalFile(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("Main.qml"))));

    if (engine.rootObjects().isEmpty())
        return 2;

    return app.exec();
}

#include "main.moc"
