#pragma once

#include <QProcessEnvironment>
#include <QStringList>

// Process-wide policy: Feria and the EPUB reader share QtWebEngine.
namespace WebEngineStartupPolicy {
// Empty means permitted. Diagnostics must never include raw arguments or values.
QString violation(const QStringList &arguments, const QProcessEnvironment &environment);
QString currentViolation(int argc, char *argv[]);
}
