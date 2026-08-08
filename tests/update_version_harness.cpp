#include "update/UpdateVersion.h"

#include <cstdlib>
#include <iostream>

using Colosseum::Update::Version;

static void require(bool ok, const char* message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const auto release = Version::parseCanonical(QStringView(u"1.1.0"));
    require(release.has_value(), "1.1.0 parses");
    require(release->canonical() == QStringLiteral("1.1.0"), "canonical keeps three parts");
    require(release->display() == QStringLiteral("1.1"), "display omits patch zero");
    require(Version::parseTag(QStringView(u"v2.4.7")).has_value(), "canonical tag parses");
    require(!Version::parseCanonical(QStringView(u"1.1")).has_value(), "two-part version rejected");
    require(!Version::parseTag(QStringView(u"1.1.0")).has_value(), "tag requires v");
    require(!Version::parseCanonical(QStringView(u"01.1.0")).has_value(), "leading zero rejected");
    require(!Version::parseCanonical(QStringView(u"1.1.0 ")).has_value(), "whitespace rejected");
    require(!Version::parseCanonical(QStringView(u"+1.1.0")).has_value(), "sign rejected");
    require(!Version::parseCanonical(QStringView(u"1.1.2147483648")).has_value(), "overflow rejected");
    require(Version::parseCanonical(QStringView(u"1.2.0"))->compare(*release) > 0,
            "newer minor compares greater");
    require(Version::parseCanonical(QStringView(u"1.0.9"))->compare(*release) < 0,
            "older minor compares lower");
    require(release->compare(*release) == 0, "equal versions compare equal");
    std::cout << "UPDATE_VERSION_OK\n";
}
