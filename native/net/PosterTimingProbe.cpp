#include "PosterTimingProbe.h"

#include <QChildEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMutexLocker>
#include <QTimer>
#include <QThread>
#include <functional>

PosterTimingProbe::PosterTimingProbe(bool enabled, QObject *parent)
    : QObject(parent), m_enabled(enabled), m_epochMs(QDateTime::currentMSecsSinceEpoch())
{
    m_clock.start();
}

qint64 PosterTimingProbe::nowMs() const { return m_clock.elapsed(); }

PosterTimingProbe::Row *PosterTimingProbe::row(int id)
{
    return id > 0 && id <= m_rows.size() ? &m_rows[id - 1] : nullptr;
}

int PosterTimingProbe::networkStart(const QUrl &url, const QString &host)
{
    if (!m_enabled) return 0;
    const QString key = url.toString(QUrl::FullyEncoded);
    // CachingNam normally creates QML Image requests on the GUI thread. Sample
    // known matching items here, at the request boundary, instead of guessing
    // from a late Image discovery. Multiple items may share a URL: true means
    // at least one matching item is on screen, which the basis records.
    int requestVisibility = -1;
    if (QThread::currentThread() == thread() && m_window) {
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            if (it->url != key || !it->ptr) continue;
            const int visible = onScreen(it->ptr, m_window) ? 1 : 0;
            requestVisibility = qMax(requestVisibility, visible);
            if (requestVisibility == 1) break;
        }
    }
    QMutexLocker lock(&m_mutex);
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Row &candidate = m_rows[i];
        if (candidate.url == key && candidate.imageSeen && candidate.requestMs < 0) {
            candidate.requestMs = nowMs();
            candidate.host = host;
            if (requestVisibility >= 0) {
                candidate.onScreenAtRequest = requestVisibility;
                candidate.visibilityBasis = QStringLiteral("network-start-url-match");
            }
            return candidate.id;
        }
    }
    Row value;
    value.id = m_rows.size() + 1;
    value.url = key;
    value.host = host;
    value.requestMs = nowMs();
    if (requestVisibility >= 0) {
        value.onScreenAtRequest = requestVisibility;
        value.visibilityBasis = QStringLiteral("network-start-url-match");
    }
    m_rows.append(value);
    return value.id;
}

void PosterTimingProbe::requestSent(int id)
{
    if (!id) return;
    QMutexLocker lock(&m_mutex);
    if (Row *value = row(id); value && value->sentMs < 0) value->sentMs = nowMs();
}

void PosterTimingProbe::responseHeaders(int id)
{
    if (!id) return;
    QMutexLocker lock(&m_mutex);
    if (Row *value = row(id); value && value->headersMs < 0) value->headersMs = nowMs();
}

void PosterTimingProbe::networkDone(int id, qint64 bytes, const QString &contentType,
                                    const QString &protocol, const QString &cache, int status)
{
    if (!id) return;
    QMutexLocker lock(&m_mutex);
    if (Row *value = row(id)) {
        value->replyMs = nowMs();
        value->bytes = bytes;
        value->contentType = contentType;
        value->protocol = protocol;
        value->cache = cache;
        value->status = status;
    }
}

int PosterTimingProbe::imageSource(const QUrl &url, bool visible, const QString &observation,
                                   const QString &objectName)
{
    if (!m_enabled || url.isEmpty()) return 0;
    QMutexLocker lock(&m_mutex);
    const QString key = url.toString(QUrl::FullyEncoded);
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        Row &candidate = m_rows[i];
        if (candidate.url == key && !candidate.imageSeen) {
            candidate.imageSeen = true;
            candidate.objectName = objectName;
            candidate.sourceMs = nowMs();
            candidate.onScreenAtObservation = visible ? 1 : 0;
            candidate.observation = observation;
            return candidate.id;
        }
    }
    Row value;
    value.id = m_rows.size() + 1;
    value.url = key;
    value.host = url.host();
    value.cache = QStringLiteral("memory-or-local");
    value.protocol = QStringLiteral("none");
    value.imageSeen = true;
    value.objectName = objectName;
    value.sourceMs = nowMs();
    value.onScreenAtObservation = visible ? 1 : 0;
    value.observation = observation;
    m_rows.append(value);
    return value.id;
}

void PosterTimingProbe::decodeDone(int id)
{
    if (!id) return;
    QMutexLocker lock(&m_mutex);
    if (Row *value = row(id); value && value->decodedMs < 0) value->decodedMs = nowMs();
}

void PosterTimingProbe::visiblePaint(int id)
{
    if (!id) return;
    QMutexLocker lock(&m_mutex);
    if (Row *value = row(id); value && value->paintMs < 0) value->paintMs = nowMs();
}

QJsonArray PosterTimingProbe::rows() const
{
    QMutexLocker lock(&m_mutex);
    QJsonArray output;
    for (const Row &value : m_rows) {
        if (!value.imageSeen && !value.contentType.startsWith(QLatin1String("image/"))) continue;
        QJsonObject item{
            {QStringLiteral("id"), value.id}, {QStringLiteral("url"), value.url},
            {QStringLiteral("host"), value.host}, {QStringLiteral("observation"), value.observation},
            {QStringLiteral("objectName"), value.objectName},
            {QStringLiteral("visibilityBasis"), value.visibilityBasis},
            {QStringLiteral("contentType"), value.contentType},
            {QStringLiteral("protocol"), value.protocol}, {QStringLiteral("cache"), value.cache},
            {QStringLiteral("requestMs"), value.requestMs}, {QStringLiteral("sourceMs"), value.sourceMs},
            {QStringLiteral("sentMs"), value.sentMs},
            {QStringLiteral("headersMs"), value.headersMs}, {QStringLiteral("replyMs"), value.replyMs},
            {QStringLiteral("decodedMs"), value.decodedMs}, {QStringLiteral("paintMs"), value.paintMs},
            {QStringLiteral("bytes"), value.bytes}, {QStringLiteral("status"), value.status},
            {QStringLiteral("imageSeen"), value.imageSeen},
            {QStringLiteral("onScreenAtObservation"), value.onScreenAtObservation < 0
                ? QJsonValue(QJsonValue::Null) : QJsonValue(value.onScreenAtObservation == 1)},
            {QStringLiteral("visibleAtEnd"), value.visibleAtEnd < 0
                ? QJsonValue(QJsonValue::Null) : QJsonValue(value.visibleAtEnd == 1)},
            {QStringLiteral("onScreenAtRequest"), value.onScreenAtRequest < 0
                ? QJsonValue(QJsonValue::Null) : QJsonValue(value.onScreenAtRequest == 1)}
        };
        output.append(item);
    }
    return output;
}

bool PosterTimingProbe::writeArtifact(const QString &path)
{
    if (!m_enabled) return false;
    // aboutToQuit runs on the GUI thread while QML items still exist. Record the
    // final viewport set so a journey can distinguish painted visible images
    // from visible images whose paint never completed.
    if (m_window && QThread::currentThread() == thread()) {
        QHash<int, int> visible;
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            if (!it->id || !it->ptr) continue;
            const int current = onScreen(it->ptr, m_window) ? 1 : 0;
            visible[it->id] = qMax(visible.value(it->id, 0), current);
        }
        QMutexLocker lock(&m_mutex);
        for (Row &value : m_rows)
            if (visible.contains(value.id)) value.visibleAtEnd = visible.value(value.id);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QJsonObject artifact{{QStringLiteral("schema"), QStringLiteral("colosseum.poster-timing.v1")},
                               {QStringLiteral("armedEpochMs"), m_epochMs},
                               {QStringLiteral("rows"), rows()}};
    return file.write(QJsonDocument(artifact).toJson()) > 0;
}

bool PosterTimingProbe::onScreen(QQuickItem *item, QQuickWindow *window)
{
    if (!item || !window || !item->isVisible() || item->opacity() <= 0 ||
        item->width() <= 0 || item->height() <= 0) return false;
    QRectF bounds = item->mapRectToScene(item->boundingRect());
    bounds = bounds.intersected(QRectF(0, 0, window->width(), window->height()));
    for (QQuickItem *parent = item->parentItem(); parent && !bounds.isEmpty(); parent = parent->parentItem()) {
        if (!parent->isVisible() || parent->opacity() <= 0) return false;
        if (parent->clip()) bounds = bounds.intersected(parent->mapRectToScene(parent->boundingRect()));
    }
    return !bounds.isEmpty();
}

void PosterTimingProbe::observe(QObject *object)
{
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item || m_items.contains(item)) return;
    const QMetaObject *meta = item->metaObject();
    if (!QByteArray(meta->className()).contains("Image")) return;
    const int sourceIndex = meta->indexOfProperty("source");
    const int statusIndex = meta->indexOfProperty("status");
    if (sourceIndex < 0 || statusIndex < 0) return;
    m_items.insert(item, Item{item});
    const int slotIndex = this->metaObject()->indexOfSlot("imageChanged()");
    const QMetaMethod slot = this->metaObject()->method(slotIndex);
    for (const int index : {sourceIndex, statusIndex}) {
        const QMetaProperty property = meta->property(index);
        if (property.hasNotifySignal()) QObject::connect(item, property.notifySignal(), this, slot);
    }
    QObject::connect(item, &QObject::destroyed, this, [this, item] { m_items.remove(item); });
    syncImage(item, QStringLiteral("first-observed"));
}

void PosterTimingProbe::syncImage(QQuickItem *item, const QString &observation)
{
    if (!item || !m_items.contains(item)) return;
    const QUrl url = item->property("source").toUrl();
    Item &state = m_items[item];
    const QString key = url.toString(QUrl::FullyEncoded);
    if (state.url != key) {
        state.url = key;
        state.ready = false;
        state.id = imageSource(url, onScreen(item, m_window), observation, item->objectName());
    }
    if (state.id && item->property("status").toInt() == 1 && !state.ready) {
        state.ready = true;
        decodeDone(state.id);
    }
}

void PosterTimingProbe::imageChanged()
{
    syncImage(qobject_cast<QQuickItem *>(sender()));
}

bool PosterTimingProbe::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (m_enabled && event->type() == QEvent::ChildAdded) {
        QPointer<QObject> child(static_cast<QChildEvent *>(event)->child());
        QTimer::singleShot(0, this, [this, child] { if (child) observe(child); });
    }
    return false;
}

void PosterTimingProbe::attach(QQuickWindow *window)
{
    if (!m_enabled || !window) return;
    m_window = window;
    scanTree(window);
    auto *discovery = new QTimer(this);
    discovery->setInterval(100);
    QObject::connect(discovery, &QTimer::timeout, this, [this] { if (m_window) scanTree(m_window); });
    discovery->start();
    QObject::connect(window, &QQuickWindow::frameSwapped, this, [this] {
        if (!m_paintQueued.exchange(true))
            QMetaObject::invokeMethod(this, &PosterTimingProbe::paintFrame, Qt::QueuedConnection);
    }, Qt::DirectConnection);
}

void PosterTimingProbe::scanTree(QObject *object)
{
    QSet<QObject *> visited;
    std::function<void(QObject *)> visit = [&](QObject *node) {
        if (!node || visited.contains(node)) return;
        visited.insert(node);
        observe(node);
        for (QObject *child : node->children()) visit(child);
        if (auto *item = qobject_cast<QQuickItem *>(node))
            for (QQuickItem *child : item->childItems()) visit(child);
        if (auto *window = qobject_cast<QQuickWindow *>(node)) visit(window->contentItem());
    };
    visit(object);
}

void PosterTimingProbe::paintFrame()
{
    m_paintQueued = false;
    if (!m_window) return;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        QQuickItem *item = it.key();
        syncImage(item);
        if (it->ready && onScreen(item, m_window)) visiblePaint(it->id);
    }
}
