#pragma once

#include <QVariantMap>
#include <QString>
#include <functional>

class ColosseumWebBridge;

// An action completes exactly once by calling Completion after its native work settles.
// Its translation unit registers it without editing the bridge or another action file.
class ActionRegistry final {
public:
    using Completion = std::function<void(const QVariantMap &)>;
    using Validator = bool (*)(const QVariantMap &);
    using Handler = void (*)(ColosseumWebBridge &, const QVariantMap &, Completion);

    struct Entry {
        QString name;
        Validator valid = nullptr;
        Handler handle = nullptr;
    };

    static bool add(Entry entry);
    static const Entry *find(const QString &name);
};
