#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace Colosseum::Update {

struct Version final {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static std::optional<Version> parseCanonical(QStringView text);
    static std::optional<Version> parseTag(QStringView text);

    QString canonical() const;
    QString display() const;
    int compare(const Version& other) const;
};

} // namespace Colosseum::Update
