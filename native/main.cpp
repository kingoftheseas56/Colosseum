// Colosseum native launcher — runs the SAME qml/ tree as qml.exe, but installs an on-disk HTTP cache
// on the QML engine's network manager. So every remote cover/banner downloads ONCE, is stored on
// disk keyed by URL, and thereafter is served straight from disk — instant, persists across restarts,
// LRU-evicts when full. This is the on-disk image cache qml.exe can't give us (and what Chromium's
// HTTP cache gave the Electron build for free). It scales to a real catalog of thousands of covers;
// you only ever pay for a cover once. Also the first brick of the real compiled Colosseum app.
//
// QML stays live-editable: we load qml/Main.qml from the FILESYSTEM (not qrc), so editing QML needs
// no recompile — only this launcher's C++ is compiled, once.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QString>

class CachingNam : public QNetworkAccessManager {
public:
    explicit CachingNam(QObject *parent = nullptr) : QNetworkAccessManager(parent) {
        auto *cache = new QNetworkDiskCache(this);
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/colosseum-images");
        QDir().mkpath(dir);
        cache->setCacheDirectory(dir);
        cache->setMaximumCacheSize(qint64(1024) * 1024 * 1024); // 1 GB LRU
        setCache(cache);
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoing) override {
        QNetworkRequest r(req);
        // Cover art is immutable CDN content: once it's in the cache, always serve from disk — no
        // network, no revalidation round-trip. (First request still downloads + stores.)
        r.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(op, r, outgoing);
    }
};

class CachingNamFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override { return new CachingNam(parent); }
};

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Colosseum"));

    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(new CachingNamFactory());

    const QString qmlPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                       : QStringLiteral("qml/Main.qml");
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
