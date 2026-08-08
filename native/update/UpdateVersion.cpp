#include "update/UpdateVersion.h"

#include <QChar>
#include <QStringList>

namespace Colosseum::Update {
namespace {

std::optional<int> parseComponent(QStringView component)
{
    if (component.isEmpty())
        return std::nullopt;
    if (component.size() > 1 && component.front() == QChar(u'0'))
        return std::nullopt;

    for (const QChar ch : component) {
        if (!ch.isDigit())
            return std::nullopt;
    }

    bool ok = false;
    const int value = component.toInt(&ok, 10);
    if (!ok || value < 0)
        return std::nullopt;
    return value;
}

} // namespace

std::optional<Version> Version::parseCanonical(QStringView text)
{
    const QStringList parts = text.toString().split(QChar(u'.'), Qt::KeepEmptyParts);
    if (parts.size() != 3)
        return std::nullopt;

    const auto major = parseComponent(QStringView(parts.at(0)));
    const auto minor = parseComponent(QStringView(parts.at(1)));
    const auto patch = parseComponent(QStringView(parts.at(2)));
    if (!major || !minor || !patch)
        return std::nullopt;

    return Version{*major, *minor, *patch};
}

std::optional<Version> Version::parseTag(QStringView text)
{
    if (text.isEmpty() || text.front() != QChar(u'v'))
        return std::nullopt;
    return parseCanonical(text.mid(1));
}

QString Version::canonical() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString Version::display() const
{
    if (patch == 0)
        return QStringLiteral("%1.%2").arg(major).arg(minor);
    return canonical();
}

int Version::compare(const Version& other) const
{
    if (major != other.major)
        return major < other.major ? -1 : 1;
    if (minor != other.minor)
        return minor < other.minor ? -1 : 1;
    if (patch != other.patch)
        return patch < other.patch ? -1 : 1;
    return 0;
}

} // namespace Colosseum::Update
