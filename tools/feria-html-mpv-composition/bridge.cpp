#include "bridge.h"
#include "mpvitem.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>
#include <ctime>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtTest/QTest>

Bridge::Bridge(QString fixtures, bool selfTest, QObject *parent)
    : QObject(parent), m_fixtures(QDir(fixtures).canonicalPath()), m_selfTest(selfTest) {
    elapsed.start();
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &Bridge::publish);
}
void Bridge::bindSurface(QObject *object) { if (auto *player = qobject_cast<MpvItem *>(object)) bind(player); }
void Bridge::bind(MpvItem *p) {
    m_player = p;
    const QString boundSession = m_gate.session();
    // Explicitly test audio decode, not audible output. Null AO keeps real audio decoding.
    p->setPropertyBlocking("ao", "null");
    p->setPropertyBlocking("hwdec", "no");
    p->setPropertyBlocking("keep-open", "no");
    connect(p, &MpvItem::ready, this, [this,p,boundSession] {
        if (m_player != p || boundSession != m_gate.session()) return;
        m_rendererReady = true;
        eventLog.append(QJsonObject{{"atMs", elapsed.elapsed()}, {"event", "rendererReady"}, {"session", boundSession}});
        qInfo() << "RENDERER READY" << boundSession; startPending();
    }, Qt::QueuedConnection);
    connect(p, &MpvItem::positionChanged, this, [this] { ++positionNotifications; });
    connect(p, &MpvItem::fileLoaded, this, [this,p,boundSession] {
        if (m_player != p || boundSession != m_gate.session()) return;
        m_loaded = true; m_error.clear(); publish();
    });
    connect(p, &MpvItem::endFile, this, [this,p,boundSession](QString reason) {
        if (m_player != p || boundSession != m_gate.session()) return;
        eventLog.append(QJsonObject{{"atMs", elapsed.elapsed()}, {"event", "endFile"}, {"reason", reason}, {"session", boundSession}});
        m_active = false; m_loaded = false;
        if (reason == "eof") m_ended = true;
        else if (reason == "error" || reason == "other") m_error = "MpvQt end-file: " + reason;
        publish();
    });
    m_tick.start();
}
QString Bridge::fixturePath(const QString &id) const {
    const QString filename = id == "h264" ? "h264-aac.mp4" : id == "ffv1" ? "ffv1-pcm.mkv" : id == "missing" ? "intentionally-missing.mp4" : "";
    if (m_fixtures.isEmpty() || filename.isEmpty()) return {};
    QFileInfo file(m_fixtures + '/' + filename);
    // A fixture symlink may not escape the one root supplied by the launcher.
    if (file.exists() && QFileInfo(file.canonicalFilePath()).absolutePath() != m_fixtures) return {};
    return file.absoluteFilePath();
}
QVariantMap Bridge::bootstrap() const {
    return {{"document", m_gate.document()}, {"session", m_gate.session()}, {"selfTest", m_selfTest},
            {"baselineSources", QVariantMap{{"h264", QUrl::fromLocalFile(fixturePath("h264")).toString()}, {"ffv1", QUrl::fromLocalFile(fixturePath("ffv1")).toString()}}}};
}
QVariantMap Bridge::reject(const QString &reason) const { return {{"ok", false}, {"reason", reason}}; }
void Bridge::navigate() {
    m_gate.navigate(); m_pending.clear(); m_fixture.clear(); m_loaded = false; m_ended = false; m_error.clear(); m_visible = false;
    if (m_player) disconnect(m_player, nullptr, this, nullptr);
    m_player = nullptr; m_rendererReady = false; m_active = false;
    emit surfaceRequested(false); emit apertureChanged(); publish();
}
QVariantMap Bridge::load(const QString &doc, const QString &session, const QString &fixture) {
    const QString path = fixturePath(fixture);
    if (path.isEmpty() || !m_gate.replace(doc, session, fixture)) return reject("stale identity or forbidden fixture");
    m_fixture = fixture; m_pending = path; m_loaded = false; m_ended = false; m_error.clear();
    // MpvQt 1.1 drops STOP end-file events. A new Qt-owned surface is a real
    // lifecycle boundary, not a timer pretending that old native events drained.
    if (m_player) disconnect(m_player, nullptr, this, nullptr);
    m_player = nullptr; m_rendererReady = false; m_active = false;
    emit surfaceRequested(true); emit apertureChanged(); publish();
    return {{"ok", true}, {"session", m_gate.session()}};
}
void Bridge::startPending() {
    if (!m_player || !m_rendererReady || m_pending.isEmpty() || m_gate.session().isEmpty()) return;
    const QString path = m_pending; m_pending.clear(); m_active = true; m_activeSession = m_gate.session();
    m_player->setPause(true);
    eventLog.append(QJsonObject{{"atMs", elapsed.elapsed()}, {"event", "loadFile"}, {"session", m_gate.session()}, {"fixture", m_fixture}});
    qInfo() << "LOAD AFTER READY" << m_gate.session() << m_fixture;
    m_player->loadFile(path);
}
QVariantMap Bridge::command(const QString &doc, const QString &session, const QString &action, double value) {
    if (!m_gate.accepts(doc, session)) return reject("stale document/session or closed");
    if (!std::isfinite(value)) return reject("non-finite value");
    if (action == "close") {
        m_gate.close(doc, session); m_pending.clear(); m_loaded = false; m_visible = false;
        if (m_player) disconnect(m_player, nullptr, this, nullptr);
        m_player = nullptr; m_rendererReady = false; m_active = false;
        emit surfaceRequested(false);
        emit apertureChanged(); publish(); return {{"ok", true}};
    }
    if (action == "fullscreen") { emit fullscreenRequested(value != 0); return {{"ok", true}}; }
    if (!m_player || !m_loaded) return reject("source not ready");
    if (action == "play") m_player->setPause(false);
    else if (action == "pause") m_player->setPause(true);
    else if (action == "seek" && value >= 0 && value <= m_player->duration()) m_player->seekExact(value);
    else if (action == "volume" && value >= 0 && value <= 100) m_player->setVolume(qRound(value));
    else return reject("unknown action or value outside bounds");
    return {{"ok", true}};
}
QVariantMap Bridge::geometry(const QString &doc, const QVariantMap &r) {
    if (doc != m_gate.document()) return reject("stale document");
    const double x = r.value("x").toDouble(), y = r.value("y").toDouble(), w = r.value("width").toDouble(), h = r.value("height").toDouble();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w < 0 || h < 0 || w > 10000 || h > 10000 || std::abs(x)>10000 || std::abs(y)>10000) return reject("invalid rectangle");
    m_aperture = QRectF(x*m_zoom, y*m_zoom, w*m_zoom, h*m_zoom);
    m_visible = r.value("visible", true).toBool(); emit apertureChanged(); return {{"ok", true}};
}
QVariantMap Bridge::snapshot() const {
    QVariantMap out{{"document", m_gate.document()}, {"session", m_gate.session()}, {"fixture", m_fixture}, {"loaded", m_loaded}, {"ended", m_ended}, {"error", m_error}, {"elapsedMs", elapsed.elapsed()},
        {"x", m_aperture.x()}, {"y", m_aperture.y()}, {"width", m_aperture.width()}, {"height", m_aperture.height()}, {"zoom", m_zoom}, {"surfaceVisible", surfaceVisible()}};
    if (m_player) {
        out.insert("position", m_player->position()); out.insert("duration", m_player->duration());
        out.insert("paused", m_player->pause()); out.insert("volume", m_player->volume());
        out.insert("decodedWidth", m_player->decodedWidth()); out.insert("decodedHeight", m_player->decodedHeight());
    }
    return out;
}
void Bridge::publish() { ++stateNotifications; emit state(snapshot()); }
QVariantMap Bridge::diagnostics() {
    QVariantMap result = snapshot();
    // Bounded, explicit test checkpoints only. Never on a state polling path.
    for (const QString &name : {"video-codec", "audio-codec", "hwdec-current", "frame-drop-count", "decoder-frame-drop-count", "estimated-vf-fps", "container-fps"})
        result.insert(name, m_player->mpvProperty(name));
    for (const QString &name : {"vo", "ao", "audio-params", "audio-out-params", "audio-pts", "aid", "video-params", "estimated-frame-number", "mpv-version"}) result.insert(name, m_player->getProperty(name));
    result.insert("nativeProcessCpuSeconds", double(std::clock())/CLOCKS_PER_SEC);
    result.insert("counterScope", "Qt frame swaps and mpv estimated frame number; no claim of exact decoded-frame count; CPU excludes Chromium child process");
    result.insert("audioEvidence", "decoded through null AO; audible output not tested");
    result.insert("stateNotifications", stateNotifications); result.insert("positionNotifications", positionNotifications); result.insert("qtFrameSwaps", renderedFrames);
    return result;
}
TestProbe::TestProbe(Bridge *bridge, QString evidence) : b(bridge), directory(std::move(evidence)) { QDir().mkpath(directory); }
QVariantMap TestProbe::check(QString name, QVariantMap expected, QVariantMap html) {
    const QVariantMap state = b->snapshot(); bool pass = true;
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        const QString key = it.key();
        if (key.endsWith("Min")) pass &= state.value(key.chopped(3)).toDouble() >= it.value().toDouble();
        else if (key.endsWith("Max")) pass &= state.value(key.chopped(3)).toDouble() <= it.value().toDouble();
        else pass &= state.value(key) == it.value();
    }
    checks.append(QJsonObject{{"name", name}, {"passed", pass}, {"native", QJsonObject::fromVariantMap(state)}, {"expected", QJsonObject::fromVariantMap(expected)}, {"html", QJsonObject::fromVariantMap(html)}});
    failed |= !pass; write(); qInfo().noquote() << (pass ? "PASS" : "FAIL") << name;
    return {{"ok", pass}, {"snapshot", state}};
}
QVariantMap TestProbe::capture(QString name, QVariantMap html) {
    if (!QRegularExpression("^[a-z0-9-]{1,50}$").match(name).hasMatch()) return {{"ok", false}};
    const QImage image = b->window->grabWindow();
    const bool ok = !image.isNull() && image.save(directory + '/' + name + ".png");
    captures.append(QJsonObject{{"name", name}, {"saved", ok}, {"native", QJsonObject::fromVariantMap(b->snapshot())}, {"html", QJsonObject::fromVariantMap(html)}, {"imageWidth", image.width()}, {"imageHeight", image.height()}, {"windowWidth", b->window->width()}, {"windowHeight", b->window->height()}, {"dpr", b->window->devicePixelRatio()}, {"exposed", b->window->isExposed()}});
    failed |= !ok; write(); return {{"ok", ok}};
}
void TestProbe::result(QString name, bool passed, QVariantMap detail) {
    checks.append(QJsonObject{{"name", name}, {"passed", passed}, {"html", QJsonObject::fromVariantMap(detail)}});
    failed |= !passed; write(); qInfo().noquote() << (passed ? "PASS" : "FAIL") << name;
}
QVariantMap TestProbe::diagnostics() { const auto d = b->diagnostics(); stats.append(QJsonObject::fromVariantMap(d)); write(); return d; }
void TestProbe::click(double x, double y) {
    const double zoom = b->snapshot().value("zoom").toDouble();
    QTimer::singleShot(0, this, [this,x,y,zoom] { QTest::mouseClick(b->window, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(x*zoom), qRound(y*zoom))); });
}
void TestProbe::resize(int w, int h) { if (w>=600 && w<=1400 && h>=500 && h<=1000) b->window->resize(w,h); }
void TestProbe::zoom(double factor) { if (factor >= 0.75 && factor <= 1.5) { b->setZoom(factor); emit zoomRequested(factor); } }
void TestProbe::write() {
    QFile f(directory + "/outcomes.json");
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(QJsonObject{{"prototype", "controlled HTML aperture over unchanged MpvItem"}, {"compileQt", QT_VERSION_STR}, {"runtimeQt", qVersion()}, {"webEngine", qWebEngineVersion()}, {"chromium", qWebEngineChromiumVersion()}, {"chromiumSecurityPatch", qWebEngineChromiumSecurityPatchVersion()}, {"passed", !failed}, {"passScope", "native and HTML assertions only; external pixel verification required"}, {"pixelVerificationRequired", true}, {"checks", checks}, {"captures", captures}, {"diagnostics", stats}, {"nativeEvents", b->eventLog}, {"elapsedMs", b->elapsed.elapsed()}, {"audioOutput", "null"}, {"audibleOutputVerified", false}}).toJson());
}
void TestProbe::finish() { write(); qInfo() << "SELFTEST COMPLETE exit" << (failed?2:0); QTimer::singleShot(300, this, [this] { QCoreApplication::exit(failed?2:0); }); }
void TestProbe::fail(QString why) { result("fatal", false, {{"reason", why}}); finish(); }
