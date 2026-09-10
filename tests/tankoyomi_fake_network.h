#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

namespace TankoyomiTest {
struct ReplyProbe {
    int aborts = 0;
    int ticks = 0;
};
struct ReplySpec {
    int status = 200;
    QByteArray body = "accepted";
    QUrl redirect;
    QByteArray contentType = "text/plain";
    int delayMs = 0;
    bool neverFinish = false;
    bool trickle = false;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    std::shared_ptr<ReplyProbe> probe = std::make_shared<ReplyProbe>();
};
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
          ReplySpec spec, QObject *parent)
        : QNetworkReply(parent), m_spec(std::move(spec))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_spec.status);
        setHeader(QNetworkRequest::ContentTypeHeader, m_spec.contentType);
        if (!m_spec.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, m_spec.redirect);
        if (m_spec.trickle) {
            auto *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, [this] {
                if (isFinished()) return;
                ++m_spec.probe->ticks;
                emit readyRead();
                emit downloadProgress(m_spec.probe->ticks, -1);
            });
            timer->start(50);
        }
        if (!m_spec.neverFinish) {
            QTimer::singleShot(m_spec.delayMs, this, [this] {
                if (isFinished()) return;
                if (m_spec.error != QNetworkReply::NoError)
                    setError(m_spec.error, QStringLiteral("injected network failure"));
                setFinished(true);
                emit readyRead();
                emit finished();
            });
        }
    }
    void abort() override
    {
        if (isFinished()) return;
        ++m_spec.probe->aborts;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_spec.body.size() - m_offset + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *destination, qint64 maximum) override
    {
        const qint64 count = std::min(maximum, qint64(m_spec.body.size()) - m_offset);
        if (count <= 0) return -1;
        std::memcpy(destination, m_spec.body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }
private:
    ReplySpec m_spec;
    qint64 m_offset = 0;
};
class Nam final : public QNetworkAccessManager {
public:
    QList<QNetworkRequest> requests;
    QList<Operation> operations;
    std::function<ReplySpec(const QNetworkRequest &, Operation, int)> respond;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        Q_UNUSED(outgoingData)
        requests.append(request);
        operations.append(operation);
        return new Reply(request, operation,
                         respond ? respond(request, operation, requests.size()) : ReplySpec{}, this);
    }
};
} // namespace TankoyomiTest
