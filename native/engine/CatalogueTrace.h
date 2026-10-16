#pragma once
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QVariantMap>
#include <algorithm>
#include <utility>

// Opt-in diagnostics only. Never include arguments, credentials, endpoints or
// cache paths. queryKey is the existing native exact-request digest.
namespace CatalogueTrace {
inline bool enabled() { return qEnvironmentVariableIntValue("COLOSSEUM_CATALOGUE_TRACE") == 1; }
inline QString queryKey(const QString& method, const QVariantList& args) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject{
        {"method", method}, {"args", QJsonArray::fromVariantList(args)}}).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}
inline QString ownerId(const void* owner) { return QString::number(quintptr(owner), 16); }
inline QVariantMap file(const QString& path) {
    if (!enabled()) return {};
    // A digest joins logical file events without revealing URLs or local paths.
    const auto clean = path.section('?', 0, 0).section('#', 0, 0);
    const auto name = QFileInfo(clean).completeBaseName();
    const auto candidate = name.startsWith("index-") ? name.mid(6) : name;
    const bool isHash = candidate.size() == 64 && std::all_of(candidate.begin(), candidate.end(),
        [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    return {{"fileId", QString::fromLatin1(QCryptographicHash::hash(clean.toUtf8(), QCryptographicHash::Sha256).toHex())},
            {"fileHash", isHash ? candidate : QString()},
            {"fileRole", name == "current" ? "entry" : name.startsWith("index-") ? "index" : isHash ? "shared" : "owner-cache"}};
}
inline QVariantMap merge(QVariantMap context, const QVariantMap& fields) {
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) context.insert(it.key(), it.value());
    return context;
}
inline void event(const QString& name, QVariantMap fields = {}) {
    if (!enabled()) return;
    QElapsedTimer monotonic; monotonic.start();
    fields.insert("event", name);
    fields.insert("epochMs", QDateTime::currentMSecsSinceEpoch());
    fields.insert("monotonicMs", monotonic.msecsSinceReference());
    fields.insert("pid", QCoreApplication::applicationPid());
    fields.insert("threadId", QString::number(quintptr(QThread::currentThreadId()), 16));
    qInfo().noquote() << "CATALOGUE_TRACE" << QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(fields)).toJson(QJsonDocument::Compact));
}
class Span {
    bool active;
    QString name;
    QVariantMap context;
    QElapsedTimer clock;
public:
    Span(QString phase, QVariantMap fields = {}) : active(enabled()), name(std::move(phase)), context(std::move(fields)) {
        if (active) { clock.start(); event(name + "_begin", context); }
    }
    void finish(const QString& status = "success", QVariantMap fields = {}) {
        if (!active) return;
        active = false;
        fields.insert("status", status); fields.insert("elapsedMs", clock.nsecsElapsed() / 1e6);
        event(name + "_end", merge(context, fields));
    }
    ~Span() { finish("failure"); }
};
}
