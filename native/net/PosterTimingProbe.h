#pragma once

#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QUrl>
#include <atomic>

// Opt-in measurement only. NAM events are exact; QML Image status marks decode completion,
// and the first subsequent swapped frame with the item inside its clipping ancestors marks
// visible paint. Request-time visibility is null unless the Image can be sampled
// at the NAM request boundary; source observation alone cannot establish it.
class PosterTimingProbe : public QObject {
    Q_OBJECT
public:
    explicit PosterTimingProbe(bool enabled, QObject *parent = nullptr);
    bool enabled() const { return m_enabled; }
    int networkStart(const QUrl &url, const QString &host);
    void requestSent(int id);
    void responseHeaders(int id);
    void networkDone(int id, qint64 bytes, const QString &contentType,
                     const QString &protocol, const QString &cache, int status);
    int imageSource(const QUrl &url, bool onScreen, const QString &observation,
                    const QString &objectName = {});
    void decodeDone(int id);
    void visiblePaint(int id);
    QJsonArray rows() const;
    bool writeArtifact(const QString &path);

    void attach(QQuickWindow *window);
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void imageChanged();
    void paintFrame();

private:
    struct Row {
        int id = 0;
        QString url;
        QString host;
        QString observation;
        QString objectName;
        QString visibilityBasis;
        QString contentType;
        QString protocol;
        QString cache;
        qint64 requestMs = -1;
        qint64 sourceMs = -1;
        qint64 sentMs = -1;
        qint64 headersMs = -1;
        qint64 replyMs = -1;
        qint64 decodedMs = -1;
        qint64 paintMs = -1;
        qint64 bytes = -1;
        int status = 0;
        int onScreenAtRequest = -1; // -1 = unavailable at the exact request boundary
        int onScreenAtObservation = -1; // geometry when Image source was observed
        int visibleAtEnd = -1; // final GUI-thread viewport snapshot; -1 = item gone
        bool imageSeen = false;
    };
    struct Item {
        QPointer<QQuickItem> ptr;
        QString url;
        int id = 0;
        bool ready = false;
    };
    qint64 nowMs() const;
    Row *row(int id);
    void observe(QObject *object);
    void scanTree(QObject *object);
    void syncImage(QQuickItem *item, const QString &observation = QStringLiteral("source-change"));
    static bool onScreen(QQuickItem *item, QQuickWindow *window);

    bool m_enabled = false;
    qint64 m_epochMs = 0;
    QElapsedTimer m_clock;
    mutable QMutex m_mutex;
    QList<Row> m_rows;
    QHash<QQuickItem *, Item> m_items; // GUI thread only
    QPointer<QQuickWindow> m_window;
    std::atomic_bool m_paintQueued{false};
};
