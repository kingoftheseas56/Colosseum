#pragma once

#include <QString>

struct AppDataMigrationResult {
    bool complete = true;
    int movedEntries = 0;
    int conflicts = 0;
    QString conflictRoot;
    QString logPath;
    QString error;
};

AppDataMigrationResult reconcileAppData(const QString& legacyRoot,
                                        const QString& currentRoot);
