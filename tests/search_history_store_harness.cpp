#include "SearchHistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireList(const QStringList &actual, const QStringList &expected, const char *message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\nexpected: "
                  << expected.join(" | ").toStdString() << "\nactual: "
                  << actual.join(" | ").toStdString() << '\n';
        std::exit(1);
    }
}

void writeProbe(const QString &path)
{
    SearchHistoryStore store(path);
    requireList(store.record("tankoban", "Batman"), {"Batman"}, "write probe records query");
}

void verifyProbe(const QString &path)
{
    SearchHistoryStore store(path);
    requireList(store.list("tankoban"), {"Batman"}, "separate process reloads query");
}

void runSuite()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary QSettings directory exists");
    const QString path = temporary.filePath("history.ini");

    {
        SearchHistoryStore store(path);
        requireList(store.record(" Tankoban ", "  Batman  "), {"Batman"}, "trim and normalize scope");
        requireList(store.record("TANKOBAN", "BATMAN"), {"BATMAN"}, "case-insensitive dedupe updates casing");
        requireList(store.record("tankoban", "Superman"), {"Superman", "BATMAN"}, "newest entry is first");
        requireList(store.record("tankoban", "Batman"), {"Batman", "Superman"}, "repeat moves entry to front");
        requireList(store.record("tankoban", "x"), {"Batman", "Superman"}, "one-character query ignored");
        requireList(store.record("tankoban", "   "), {"Batman", "Superman"}, "empty query ignored");

        for (const QString &query : {"One", "Two", "Three", "Four", "Five", "Six", "Seven"})
            store.record("tankoban", query);
        requireList(store.list("tankoban"), {"Seven", "Six", "Five", "Four", "Three", "Two"}, "history caps at newest six");
        requireList(store.record("biblio", "Dune"), {"Dune"}, "biblio stores independently");
        requireList(store.record("theatre", "Severance"), {"Severance"}, "theatre stores independently");
        requireList(store.remove("TANKOBAN", "sIx"), {"Seven", "Five", "Four", "Three", "Two"}, "remove is case-insensitive");
        store.clear("biblio");
        requireList(store.list("biblio"), {}, "clear removes selected scope");
        requireList(store.list("theatre"), {"Severance"}, "clear preserves other scope");
    }

    {
        SearchHistoryStore restored(path);
        requireList(restored.list("tankoban"), {"Seven", "Five", "Four", "Three", "Two"}, "reconstructed store restores disk data");
        requireList(restored.list("theatre"), {"Severance"}, "reconstructed store preserves independent scope");
    }

    QSettings corrupt(path, QSettings::IniFormat);
    corrupt.setValue("searchHistory/tankoban", 42);
    corrupt.sync();
    SearchHistoryStore malformed(path);
    requireList(malformed.list("tankoban"), {}, "malformed scope data is ignored safely");
    requireList(malformed.list("theatre"), {"Severance"}, "malformed scope does not erase other scopes");

    corrupt.setValue("searchHistory/tankoban",
                     QStringList{" Batman ", "batman", "x", "", "One", "Two", "Three", "Four", "Five", "Six", "Seven"});
    corrupt.sync();
    SearchHistoryStore sanitized(path);
    requireList(sanitized.list("tankoban"), {"Batman", "One", "Two", "Three", "Four", "Five"},
                "type-valid persisted data is trimmed, deduplicated, and capped safely");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 3 && args.at(1) == "--write") {
        writeProbe(args.at(2));
        return 0;
    }
    if (args.size() == 3 && args.at(1) == "--verify") {
        verifyProbe(args.at(2));
        return 0;
    }
    runSuite();
    std::cout << "SearchHistoryStore behavioral tests passed.\n";
    return 0;
}
