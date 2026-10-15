#pragma once

#include <QString>
#include <QStringList>

#include <optional>

struct StartupLayout {
    QString qmlPath;
    QString resourceRoot;
    bool qmlOverride = false;
};

QString qmlTreeFingerprint(const QString& qmlRoot, QString* error = nullptr);

std::optional<StartupLayout> resolveStartupLayout(const QStringList& arguments,
                                                  const QString& applicationDirPath,
                                                  QString* error = nullptr);
