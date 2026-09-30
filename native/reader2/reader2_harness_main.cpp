// reader2_harness_main.cpp — TASK 5: the standalone "first pixels" harness.
//
// Boots straight into the fresh reader (native QML chrome over the vendored Anx
// foliate "paper") with a book shelf — NO Colosseum shell. Click a book on the
// shelf and it renders in the paper; arrow keys turn pages; Esc returns to the
// shelf. This exe exists to prove the paper loads and reports position under a
// real Qt viewport, ahead of the swap into Biblio (Task 16).
//
// Stores are SANDBOXED by default (QStandardPaths test mode) so a harness run
// never mutates the real reader's progress/settings/bookmarks/annotations; pass
// --real-stores to read/write the live files instead.
//
// The book SHELF, however, always reads the REAL downloaded-books folder
// (<AppData>/Brotherhood/Colosseum/books — where BookDownloader lands .epub/.pdf,
// see native/engine/BookDownloader.cpp baseDir()). We compute that path with the
// shell's real identity BEFORE enabling test mode, then hand it to QML as the
// context property `booksDir`. Sandboxing only redirects the stores, not the shelf.
//
// [Agent 2 (Claude), biblio]
#include "reader2/Reader2Bridge.h"
#include "ClipboardHelper.h"
#include "AudioPairingStore.h"
#include "engine/AudiobookDownloader.h"
#include "player/mpvitem.h"

#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>
#include <QtQml/qqml.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char** argv)
{
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    // mpvqt (the audiobook engine, Task 13) renders through OpenGL, so the whole Quick scene
    // must use the OpenGL RHI backend; WebEngine (the paper) shares GL contexts. Both are set
    // process-wide BEFORE the QGuiApplication + the first QQuickWindow (mirrors main.cpp).
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    // Shell identity — must match the real app so the books dir (and --real-stores)
    // resolve to the SAME <AppData>/Brotherhood/Colosseum tree BookDownloader uses.
    app.setOrganizationName(QStringLiteral("Brotherhood"));
    app.setApplicationName(QStringLiteral("Colosseum"));

    // Real downloaded-books folder — computed with test mode OFF so the shelf
    // always points at the live library, regardless of store sandboxing below.
    const QString booksDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/books");

    const bool realStores = app.arguments().contains(QStringLiteral("--real-stores"));
    if (!realStores) QStandardPaths::setTestModeEnabled(true);   // sandbox stores by default
    qInfo("[reader2] stores: %s", realStores ? "REAL" : "sandbox");
    qInfo("[reader2] booksDir: %s", qUtf8Printable(booksDir));

    Reader2Bridge bridge;
    ClipboardHelper clipboard;   // SelectionMenu "Copy" (Task 9) — same helper main.cpp exposes

    // ── read-along backends (Task 13) — the SAME types the main app registers ──
    // AudioPairing holds Task 12's book↔audiobook attachment; Audiobooks resolves the
    // attached audiobook's on-disk files/chapters. Wiring setPairing() mirrors main.cpp
    // so an in-harness download would auto-attach (the harness never runs a real one —
    // stream=nullptr — but the Audio tab READS the attachment the same way the app does).
    QNetworkAccessManager audioNam;
    AudioPairingStore audioPairing;
    AudiobookDownloader audiobooks(&audioNam, /*stream=*/nullptr);
    audiobooks.setPairing(&audioPairing);

    // The audiobook playback surface (mpv), reached from AudiobookSession.qml as
    // `import Colosseum.Player` — registered exactly as main.cpp does so the harness can
    // instantiate the SHARED session and drive a real read-along transport.
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "PlayerItem");
    qmlRegisterType<MpvItem>("Colosseum.Player", 1, 0, "MpvItem");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Reader2Bridge"), &bridge);
    engine.rootContext()->setContextProperty(QStringLiteral("Clipboard"), &clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("booksDir"), booksDir);
    engine.rootContext()->setContextProperty(QStringLiteral("AudioPairing"), &audioPairing);
    engine.rootContext()->setContextProperty(QStringLiteral("Audiobooks"), &audiobooks);

    // Exe lands in native/build-msvc/ (Ninja single-config) → repo root is two up,
    // so the QML tree is at ../../qml/reader2/Harness.qml.
    engine.load(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../qml/reader2/Harness.qml")));

    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
