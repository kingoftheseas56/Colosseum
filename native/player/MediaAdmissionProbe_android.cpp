#include "MediaAdmissionProbe.h"

#include <QCoreApplication>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
MediaAdmissionProbe::Verdict verdictFromString(const QString &value)
{
    if (value == QLatin1String("Admitted"))
        return MediaAdmissionProbe::Verdict::Admitted;
    if (value == QLatin1String("RejectedNoVideo"))
        return MediaAdmissionProbe::Verdict::RejectedNoVideo;
    if (value == QLatin1String("RejectedTimeout"))
        return MediaAdmissionProbe::Verdict::RejectedTimeout;
    return MediaAdmissionProbe::Verdict::RejectedError;
}
}

MediaAdmissionProbe::Result MediaAdmissionProbe::probe(const QString &path, int timeoutMs)
{
    Result result;
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        result.detail = QStringLiteral("Android context unavailable");
        return result;
    }
    const QJniObject source = QJniObject::fromString(path);
    const QJniObject payload = QJniObject::callStaticObjectMethod(
        "org/colosseum/vault/VaultMediaProbe", "probe",
        "(Landroid/content/Context;Ljava/lang/String;I)Ljava/lang/String;",
        context.object<jobject>(), source.object<jstring>(), jint(timeoutMs));
    if (!payload.isValid()) {
        result.detail = QStringLiteral("Android media probe returned no result");
        return result;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(
        payload.toString().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.detail = QStringLiteral("Android media probe returned invalid JSON");
        return result;
    }

    const QJsonObject object = document.object();
    result.verdict = verdictFromString(object.value(QStringLiteral("verdict")).toString());
    result.width = object.value(QStringLiteral("width")).toInt();
    result.height = object.value(QStringLiteral("height")).toInt();
    result.detail = object.value(QStringLiteral("detail")).toString();
    return result;
}
