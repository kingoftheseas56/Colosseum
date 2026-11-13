#pragma once
#include "server1/archive/ArchiveSources.h"
namespace server1::archive {
// Bounded random access across ordered HTTP archive parts. Physical byte
// coordinates are used internally; M277's public range policy is kept separate.
class ArchiveByteSource final : public QObject {
public:
    using Token = RemoteByteSource::Token;
    using Completion = RemoteByteSource::Completion;
    explicit ArchiveByteSource(QObject *parent = nullptr);
    ~ArchiveByteSource() override;
    void open(SourceFamily family, QJsonArray urls, std::function<void(QString)> completion);
    qint64 size() const;
    QList<qint64> partLengths() const;
    Token read(qint64 offset, qint64 count, Completion completion);
    void cancel(Token token);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
