#include "../native/bootstrap/AppDataMigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (0)

static void writeFile(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(bytes);
}
static QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid(), "temporary root created");

    const QString legacy = QDir(temp.path()).filePath(QStringLiteral("legacy"));
    const QString current = QDir(temp.path()).filePath(QStringLiteral("current"));
    writeFile(QDir(legacy).filePath(QStringLiteral("downloads/a.bin")), "legacy-a");
    const AppDataMigrationResult oldOnly = reconcileAppData(legacy, current);
    CHECK(oldOnly.complete, "old-only migration completes");
    CHECK(readFile(QDir(current).filePath(QStringLiteral("downloads/a.bin"))) == "legacy-a",
          "old-only migration preserves bytes");
    CHECK(!QDir(legacy).exists(), "empty legacy root is removed");

    QDir().mkpath(legacy);
    writeFile(QDir(legacy).filePath(QStringLiteral("shared/settings.json")), "legacy-settings");
    writeFile(QDir(legacy).filePath(QStringLiteral("shared/legacy-only.db")), "legacy-db");
    writeFile(QDir(current).filePath(QStringLiteral("shared/settings.json")), "current-settings");
    writeFile(QDir(current).filePath(QStringLiteral("shared/current-only.db")), "current-db");

    const AppDataMigrationResult merged = reconcileAppData(legacy, current);
    CHECK(merged.complete, "both-roots reconciliation completes");
    CHECK(merged.conflicts == 1, "one collision is quarantined");
    CHECK(readFile(QDir(current).filePath(QStringLiteral("shared/settings.json"))) == "current-settings",
          "current root wins collisions");
    CHECK(readFile(QDir(current).filePath(QStringLiteral("shared/legacy-only.db"))) == "legacy-db",
          "legacy-only nested file is merged");
    CHECK(readFile(QDir(current).filePath(QStringLiteral("shared/current-only.db"))) == "current-db",
          "current-only nested file is preserved");
    CHECK(!merged.conflictRoot.isEmpty(), "collision exposes conflict root");
    CHECK(readFile(QDir(merged.conflictRoot).filePath(QStringLiteral("shared/settings.json")))
              == "legacy-settings",
          "legacy collision bytes are preserved in quarantine");

    const AppDataMigrationResult rerun = reconcileAppData(legacy, current);
    CHECK(rerun.complete, "rerun after completed reconciliation is harmless");
    CHECK(rerun.movedEntries == 0, "rerun moves nothing");
    CHECK(rerun.conflicts == 0, "rerun creates no duplicate conflicts");

    const QString currentOnlyLegacy = QDir(temp.path()).filePath(QStringLiteral("absent-legacy"));
    const AppDataMigrationResult currentOnly = reconcileAppData(currentOnlyLegacy, current);
    CHECK(currentOnly.complete, "new-only state is accepted");
    CHECK(readFile(QDir(current).filePath(QStringLiteral("shared/settings.json"))) == "current-settings",
          "new-only state leaves current data untouched");

    const AppDataMigrationResult sameRoot = reconcileAppData(current, current);
    CHECK(sameRoot.complete, "same-root request is a no-op");

    const QString dedupeLegacy = QDir(temp.path()).filePath(QStringLiteral("dedupe-legacy"));
    const QString dedupeCurrent = QDir(temp.path()).filePath(QStringLiteral("dedupe-current"));
    writeFile(QDir(dedupeLegacy).filePath(QStringLiteral("same.bin")), "identical");
    writeFile(QDir(dedupeCurrent).filePath(QStringLiteral("same.bin")), "identical");
    const AppDataMigrationResult deduped = reconcileAppData(dedupeLegacy, dedupeCurrent);
    CHECK(deduped.complete, "interrupted duplicate state reconciles");
    CHECK(deduped.conflicts == 0, "identical duplicate does not create false conflict");
    CHECK(!QDir(dedupeLegacy).exists(), "identical legacy duplicate is cleaned after proof");
    CHECK(readFile(QDir(dedupeCurrent).filePath(QStringLiteral("same.bin"))) == "identical",
          "dedupe preserves current bytes");

    const QString blockedLegacy = QDir(temp.path()).filePath(QStringLiteral("blocked-legacy"));
    const QString blockedCurrent = QDir(temp.path()).filePath(QStringLiteral("blocked-current"));
    writeFile(QDir(blockedLegacy).filePath(QStringLiteral("keep.bin")), "must-survive");
    writeFile(blockedCurrent, "not-a-directory");
    const AppDataMigrationResult blocked = reconcileAppData(blockedLegacy, blockedCurrent);
    CHECK(!blocked.complete, "unusable current root fails closed");
    CHECK(readFile(QDir(blockedLegacy).filePath(QStringLiteral("keep.bin"))) == "must-survive",
          "failed reconciliation never deletes legacy bytes");

    std::printf(fails ? "FAILS: %d\n" : "app_data_migration_harness: ALL PASS\n", fails);
    return fails;
}
