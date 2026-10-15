#pragma once

#include <QString>
#include <QVariantMap>

#include <optional>

struct TankoyomiQualifiedChapter
{
    QString language;
    QString providerId;
    QVariantMap chapter;
};

class TankoyomiIdentity
{
public:
    static QString qualifyChapter(const QString &language,
                                  const QString &providerId,
                                  const QVariantMap &chapter);
    static std::optional<TankoyomiQualifiedChapter> parseChapter(const QString &qualifiedId);
    static bool isQualifiedChapter(const QString &value);

private:
    static QString normalizeLanguage(const QString &language);
    static bool safeProviderId(const QString &providerId);
};
