#include "../../native/devtools/CollectionsTestProfileController.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QRect>
#include <QSettings>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"root", "Existing marked synthetic AppData directory.", "path"});
    parser.addOption({"width", "Window logical width (1024 to 1920).", "pixels", "1440"});
    parser.addOption({"height", "Window logical height (640 to 1080).", "pixels", "960"});
    parser.process(app);
    const QString root = QDir::fromNativeSeparators(parser.value("root"));
    if (!CollectionsTestProfileController(root, {}, {}).enabled()) {
        QTextStream(stderr) << "Synthetic Collections root is not enabled.\n";
        return 2;
    }
    bool widthOk = false, heightOk = false;
    const int width = parser.value("width").toInt(&widthOk);
    const int height = parser.value("height").toInt(&heightOk);
    if (!widthOk || !heightOk || width < 1024 || width > 1920 || height < 640 || height > 1080)
        return 2;
    // QSettings uses exactly the tagged application identity selected by main.
    // QRect is written by Qt itself, preserving its platform-specific encoding.
    QSettings settings(QStringLiteral("Brotherhood"), QFileInfo(root).fileName());
    settings.setValue(QStringLiteral("window/baseMode"), QStringLiteral("windowed"));
    settings.setValue(QStringLiteral("window/fillPolicy"), 2);
    settings.setValue(QStringLiteral("window/maximized"), false);
    settings.setValue(QStringLiteral("window/normalGeometry"), QRect(16, 16, width, height));
    settings.sync();
    if (settings.status() != QSettings::NoError) return 3;
    QTextStream(stdout) << "COLLECTIONS_FIXTURE_WINDOW " << width << 'x' << height << '\n';
    return 0;
}
