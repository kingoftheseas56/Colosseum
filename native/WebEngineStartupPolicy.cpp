#include "WebEngineStartupPolicy.h"

namespace WebEngineStartupPolicy {
namespace {
bool permittedOption(const QString &option)
{
    // Deliberately finite: arbitrary enable/disable-features and new Chromium
    // switches can change trust boundaries. Add options only after review.
    static const QStringList allowed{
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--disable-gpu-compositing"),
        QStringLiteral("--disable-accelerated-video-decode"),
        QStringLiteral("--disable-accelerated-video-encode"),
        QStringLiteral("--enable-logging"),
        QStringLiteral("--disable-logging"),
        QStringLiteral("--use-gl=desktop"),
        QStringLiteral("--use-gl=egl"),
        QStringLiteral("--use-gl=angle"),
        QStringLiteral("--use-angle=default"),
        QStringLiteral("--use-angle=d3d11"),
        QStringLiteral("--use-angle=gl"),
        QStringLiteral("--log-level=0"),
        QStringLiteral("--log-level=1"),
        QStringLiteral("--log-level=2"),
        QStringLiteral("--log-level=3"),
        QStringLiteral("--v=0"),
        QStringLiteral("--v=1"),
        QStringLiteral("--v=2"),
        QStringLiteral("--v=3")
    };
    return allowed.contains(option);
}
}

QString violation(const QStringList &arguments, const QProcessEnvironment &environment)
{
    // Qt checks presence, not truthiness: even "0" or an empty value disables
    // sandboxing. Never silently strip a requested unsafe configuration.
    if (environment.contains(QStringLiteral("QTWEBENGINE_DISABLE_SANDBOX")))
        return QStringLiteral("Remove QTWEBENGINE_DISABLE_SANDBOX before starting Colosseum.");
    if (environment.contains(QStringLiteral("QTWEBENGINE_REMOTE_DEBUGGING")))
        return QStringLiteral("Remove QTWEBENGINE_REMOTE_DEBUGGING before starting Colosseum.");

    // Qt's environment tokenizer splits on ASCII spaces and interprets quotes.
    // Our accepted grammar is narrower: exact unquoted options, no free-form
    // values, escapes, switch aliases or feature lists. Every accepted token is
    // therefore interpreted identically by Qt and Chromium on supported hosts.
    const auto flags = environment.value(QStringLiteral("QTWEBENGINE_CHROMIUM_FLAGS"));
    for (const auto &option : flags.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!permittedOption(option))
            return QStringLiteral("QTWEBENGINE_CHROMIUM_FLAGS contains an unapproved option. "
                                  "Remove it or use the documented graphics/logging options.");
    }

    // Qt matches this separator case-insensitively. It ignores application
    // arguments before it. Validate both sources even when the environment
    // currently overrides argv, so an unsafe dormant configuration is rejected.
    bool engineArguments = false;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (!engineArguments) {
            engineArguments = arguments.at(i).compare(QStringLiteral("--webEngineArgs"),
                                                      Qt::CaseInsensitive) == 0;
        } else if (!permittedOption(arguments.at(i))) {
            return QStringLiteral("Unapproved option after --webEngineArgs. "
                                  "Remove it or use the documented graphics/logging options.");
        }
    }
    return {};
}

QString currentViolation(int argc, char *argv[])
{
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));
    return violation(arguments, QProcessEnvironment::systemEnvironment());
}
}
