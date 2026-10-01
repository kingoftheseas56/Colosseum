#include <QBuffer>
#include <QCoreApplication>
#include <QDirIterator>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString root = QCoreApplication::applicationDirPath() + "/../plugins";
    // No host SDK/plugin fallback may mask an incomplete package.
    QCoreApplication::setLibraryPaths({root});
    QJsonArray failures, loaded;
    const QStringList groups = {"imageformats", "platforms", "wayland-graphics-integration-client",
                                "wayland-shell-integration", "wayland-decoration-client"};
    for (const QString &group : groups) {
        QDirIterator plugins(root + '/' + group, {"*.so"}, QDir::Files);
        bool found = false;
        while (plugins.hasNext()) {
            const QString path = plugins.next();
            found = true;
            QPluginLoader plugin(path);
            if (!plugin.instance())
                failures.append(path + ": " + plugin.errorString());
            else
                loaded.append(path);
        }
        if (!found)
            failures.append("empty plugin group: " + group);
    }
    // Independently generated lossless WebP: 2x2 opaque pixels RGB(17,83,149).
    QByteArray bytes = QByteArray::fromBase64("UklGRh4AAABXRUJQVlA4TBEAAAAvAUAAAAfQqUa0sv+BiOh/AAA=");
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input, "webp");
    const QImage decoded = reader.read();
    bool webp = decoded.size() == QSize(2, 2);
    for (int y = 0; webp && y < 2; ++y)
        for (int x = 0; webp && x < 2; ++x)
            webp = decoded.pixelColor(x, y) == QColor(17, 83, 149, 255);
    if (!webp)
        failures.append("known WebP decode failed: " + reader.errorString());
    QJsonObject result{{"qt", qVersion()}, {"webp_decoded", webp},
                       {"loaded_plugins", loaded}, {"failures", failures},
                       {"passed", failures.isEmpty()}};
    std::cout << QJsonDocument(result).toJson().constData();
    return failures.isEmpty() ? 0 : 1;
}
