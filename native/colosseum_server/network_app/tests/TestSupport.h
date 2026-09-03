#pragma once

#include <QByteArray>
#include <QDebug>
#include <QString>

#include <cstdio>

struct TestState {
    int checks = 0;
    int failures = 0;

    void require(bool condition, const QString &message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            const QByteArray text = message.toUtf8();
            std::fprintf(stderr, "FAIL: %s\n", text.constData());
        }
    }

    template <typename A, typename B>
    void equal(const A &actual, const B &expected, const QString &message)
    {
        ++checks;
        if (!(actual == expected)) {
            ++failures;
            const QByteArray text = message.toUtf8();
            std::fprintf(stderr, "FAIL: %s\n", text.constData());
        }
    }
};

inline int finishTests(const TestState &state, const char *suite)
{
    if (state.failures) {
        std::fprintf(stderr, "%s RED %d of %d checks failed\n",
                     suite, state.failures, state.checks);
        return 1;
    }
    std::fprintf(stdout, "%s GREEN %d checks\n", suite, state.checks);
    return 0;
}
