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

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char** argv)
{
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Reader2Bridge"), &bridge);
    engine.rootContext()->setContextProperty(QStringLiteral("booksDir"), booksDir);

    // Exe lands in native/build-msvc/ (Ninja single-config) → repo root is two up,
    // so the QML tree is at ../../qml/reader2/Harness.qml.
    engine.load(QUrl::fromLocalFile(QCoreApplication::applicationDirPath()
                + QStringLiteral("/../../qml/reader2/Harness.qml")));

    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
