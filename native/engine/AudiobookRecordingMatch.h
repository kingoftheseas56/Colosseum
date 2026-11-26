#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Pure, conservative source-to-catalogue recording matcher. No network or state.
namespace AudiobookRecordingMatch {
QVariantMap match(const QVariantMap& source, const QVariantList& recordings);
}

class AudiobookRecordingMatcher : public QObject {
    Q_OBJECT
public:
    explicit AudiobookRecordingMatcher(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE QVariantMap matchSource(const QVariantMap& source,
                                       const QVariantList& recordings) const {
        return AudiobookRecordingMatch::match(source, recordings);
    }
};
