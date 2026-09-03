#pragma once

#include <QJsonObject>
#include <QProcessEnvironment>
#include <QString>

namespace colosseum::server {

class ServerSettings
{
public:
    enum class Platform {
        Windows,
        Linux,
        MacOS,
        Android,
        Other,
    };

    explicit ServerSettings(QString appPath,
                            Platform platform,
                            QString settingsDirectory = QString(),
                            bool disableCaching = false);

    static QString resolveAppPath(Platform platform,
                                  const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment(),
                                  QString tempPath = QString());

    QJsonObject values() const { return m_values; }
    QString settingsFilePath() const;
    void extend(const QJsonObject &overrides);
    bool load(QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr) const;

private:
    static QJsonObject defaults(const QString &appPath,
                                Platform platform,
                                bool disableCaching);

    QString m_appPath;
    QString m_settingsDirectory;
    QJsonObject m_values;
};

} // namespace colosseum::server
