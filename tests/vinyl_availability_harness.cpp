#include "vinyl/VinylAvailability.h"
#include <QByteArray>
#include <cstdio>
int main() {
    const bool hadValue = qEnvironmentVariableIsSet("COLOSSEUM_VINYL_LIVE");
    const QByteArray previous = qgetenv("COLOSSEUM_VINYL_LIVE");
    int failures = 0;
    for (const QByteArray value : {QByteArray(), QByteArray("0"), QByteArray("1"), QByteArray("2")}) {
        if (value.isEmpty()) qunsetenv("COLOSSEUM_VINYL_LIVE");
        else qputenv("COLOSSEUM_VINYL_LIVE", value);
#if defined(Q_OS_ANDROID)
        const bool expected = false;
#elif defined(COLOSSEUM_ENABLE_VINYL_QUALIFICATION)
        const bool expected = value == "1";
#else
        const bool expected = true;
#endif
        if (Colosseum::vinylLiveEnabled() != expected) {
            std::fprintf(stderr, "Unexpected Vinyl availability for env=%s\n", value.constData());
            ++failures;
        }
    }
    if (hadValue) qputenv("COLOSSEUM_VINYL_LIVE", previous);
    else qunsetenv("COLOSSEUM_VINYL_LIVE");
    return failures ? 1 : 0;
}
