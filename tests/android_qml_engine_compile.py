#!/usr/bin/env python3
"""Compile the packaged Main graph with the real Qt engine and Android selectors.

This is a QML compile regression, not a native-app runtime/first-frame test. Only
PlayerItem, SeekThumbnailer and ActivityPlaybackTracker (native registrations) are mocked; no
shipping QML is replaced. Use emulator_smoke.sh for full application proof.

Usage: python3 tests/android_qml_engine_compile.py --qt-root QT_HOST_KIT --qml-root PACKAGED_QML
"""
import argparse
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile

PROBE = r'''
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlFileSelector>
#include <QDebug>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlFileSelector selector(&engine);
    selector.setExtraSelectors({"android"});
    const QString mocks = QString::fromLocal8Bit(argv[1]);
    qmlRegisterType(QUrl::fromLocalFile(mocks + "/PlayerItem.qml"),
                    "Colosseum.Player", 1, 0, "PlayerItem");
    qmlRegisterType(QUrl::fromLocalFile(mocks + "/ActivityPlaybackTracker.qml"),
                    "Colosseum.Activity", 1, 0, "ActivityPlaybackTracker");
    qmlRegisterType(QUrl::fromLocalFile(mocks + "/SeekThumbnailer.qml"),
                    "Colosseum.Player", 1, 0, "SeekThumbnailer");
    int failed = 0;
    for (int i = 2; i < argc; ++i) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromLocal8Bit(argv[i])),
                                QQmlComponent::PreferSynchronous);
        if (!component.isReady()) {
            qCritical().noquote() << "ANDROID_QML_COMPILE_FAILED" << argv[i] << component.errorString();
            ++failed;
        } else {
            qInfo() << "ANDROID_QML_COMPILE_READY" << qVersion() << component.url();
        }
    }
    qInfo() << "ANDROID_QML_COMPILE_SUMMARY" << "entries=" << argc - 2 << "failed=" << failed;
    return failed ? 1 : 0;
}
'''
PLAYER = '''import QtQuick
Item {
    property real position: 0
    property real duration: 0
    property bool pause: true
    property real speed: 1
    property real volume: 100
    property bool mute: false
    property url currentUrl: ""
    property var chapters: []
    property real cacheBufferingState: 100
    property bool coreSeeking: false
    signal fileStarted()
    signal fileLoaded()
    signal playbackError(string code, string message)
    signal endFile(string reason)
    signal gifSaved(string path)
    signal gifFailed()
    signal trackListChanged()
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--qml-root", type=Path, required=True)
    parser.add_argument("--entry", action="append", help="QML path relative to packaged root; repeatable")
    parser.add_argument("--loader-roots", action="store_true",
                        help="Also compile every existing literal QML URL named anywhere in the shipped tree")
    args = parser.parse_args()
    qt = args.qt_root.resolve()
    root = args.qml_root.resolve()
    entries = {root / name for name in (args.entry or ["Main.qml"])}
    if args.loader_roots:
        # Source URLs include deferred Loader pages, world registries and setSource
        # targets. Compilation alone of Main never visits these dynamic graphs.
        for source in root.rglob("*.qml"):
            for name in re.findall(r'["\']([^"\'\n]+\.qml)["\']', source.read_text()):
                target = (source.parent / name).resolve()
                if target.is_file() and target.is_relative_to(root):
                    entries.add(target)
    for entry in entries:
        if not entry.is_file():
            parser.error(f"missing packaged entrypoint: {entry}")
    env = dict(os.environ)
    env["PKG_CONFIG_PATH"] = str(qt / "lib/pkgconfig")
    env["QT_QPA_PLATFORM"] = "offscreen"
    # Never set GC/JIT overrides: compile the real graph under ordinary Qt policy.
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "Qt6Quick"], env=env, text=True))
    with tempfile.TemporaryDirectory(prefix="android-qml-engine-") as directory:
        work = Path(directory)
        (work / "probe.cpp").write_text(PROBE)
        (work / "PlayerItem.qml").write_text(PLAYER)
        (work / "ActivityPlaybackTracker.qml").write_text(
            "import QtQuick\nQtObject { property var sink: null; property int lifecycleScopeGeneration: 0 }\n")
        command = ["g++", "-std=c++17", "-fPIC", str(work / "probe.cpp"),
                   "-o", str(work / "probe"), *flags, f"-Wl,-rpath,{qt / 'lib'}"]
        print("BUILD:", shlex.join(command), flush=True)
        subprocess.run(command, env=env, check=True)
        (work / "SeekThumbnailer.qml").write_text(
            "import QtQuick\nQtObject { signal thumbReady(real bucketSec, string imageUrl) }\n")
        command = [str(work / "probe"), str(work), *map(str, sorted(entries))]
        print("RUN:", shlex.join(command), flush=True)
        return subprocess.run(command, env=env).returncode


if __name__ == "__main__":
    raise SystemExit(main())
