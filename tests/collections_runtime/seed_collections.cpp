#include "../../native/NamedCollectionsStore.h"
#include "../../native/engine/ColosseumTitleIdentityRegistry.h"
#include "../../native/devtools/CollectionsTestProfileController.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>
#include <utility>

namespace {
int fail(const QString &reason) {
    QTextStream(stderr) << "collections_seed: " << reason << '\n';
    return 2;
}
QString absoluteClean(const QString &path) {
    const QString native = QDir::fromNativeSeparators(path);
    if (!QFileInfo(native).isAbsolute() || QDir::cleanPath(native) != native) return {};
    return native;
}
bool exactKeys(const QJsonObject &object, const QSet<QString> &expected) {
    QSet<QString> actual;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) actual.insert(it.key());
    return actual == expected;
}
struct PreparedCollection {
    QString name;
    QString id;
    QVector<QVariantMap> references;
};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("collections_seed"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Create one new named-collections-v2.json using real identity/store owners in an explicitly marked synthetic fixture root."
        " Requires COLOSSEUM_APPDATA_TAG=collections-fixture-* and COLOSSEUM_COLLECTIONS_TEST_FIXTURES=1."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("root"), QStringLiteral("Existing marked Colosseum-dltest-collections-fixture-* root."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("fixture"), QStringLiteral("JSON containing collections:[{name,references:[raw candidate]}]."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("registry"), QStringLiteral("Existing title identity registry JSON; read only."), QStringLiteral("absolute-path")});
    parser.addOption({QStringLiteral("output"), QStringLiteral("Absent named-collections-v2.json in local/A/B synthetic profile."), QStringLiteral("absolute-path")});
    parser.process(app);
    if (!parser.positionalArguments().isEmpty()) return fail(QStringLiteral("Positional arguments are not accepted."));
    const QString root = absoluteClean(parser.value(QStringLiteral("root")));
    const QString input = absoluteClean(parser.value(QStringLiteral("fixture")));
    const QString registryPath = absoluteClean(parser.value(QStringLiteral("registry")));
    const QString output = absoluteClean(parser.value(QStringLiteral("output")));
    if (root.isEmpty() || input.isEmpty() || registryPath.isEmpty() || output.isEmpty())
        return fail(QStringLiteral("All four options must be explicit absolute, clean paths."));
    CollectionsTestProfileController containment(root, {}, {});
    if (!containment.enabled())
        return fail(QStringLiteral("Refusing an unmarked, untagged, linked, or non-fixture root."));
    // The same three fixed identities exposed by the test-only app controller.
    const QStringList allowed{
        root + QStringLiteral("/profiles/legacy/named-collections-v2.json"),
        root + QStringLiteral("/profiles/local/named-collections-v2.json"),
        root + QStringLiteral("/profiles/11111111-1111-4111-8111-111111111181/named-collections-v2.json"),
        root + QStringLiteral("/profiles/11111111-1111-4111-8111-111111111182/named-collections-v2.json")};
    if (!allowed.contains(output))
        return fail(QStringLiteral("Output must be the named-collections file of the local, A, or B synthetic profile."));
    if (QFileInfo::exists(output) || QFileInfo(output).isSymLink())
        return fail(QStringLiteral("The output already exists. Existing profile data is never replaced."));
    QFile source(input);
    if (!source.open(QIODevice::ReadOnly) || source.size() > 8 * 1024 * 1024)
        return fail(QStringLiteral("The raw fixture could not be read or exceeds 8 MiB."));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(source.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || !exactKeys(document.object(), {QStringLiteral("collections")})
            || !document.object().value(QStringLiteral("collections")).isArray())
        return fail(QStringLiteral("Fixture schema must be exactly {collections:[{name,references}]} with valid JSON."));
    const QJsonArray collections = document.object().value(QStringLiteral("collections")).toArray();
    if (collections.size() > 500) return fail(QStringLiteral("Fixture has more than 500 collections."));
    ColosseumTitleIdentityRegistry identity(registryPath);
    if (!identity.ready()) return fail(QStringLiteral("Identity registry is unavailable: ") + identity.errorCode());
    // No alias-union persistence path is configured: source fixtures cannot
    // write to the app's identity cache or any store other than this output.
    QVector<PreparedCollection> prepared;
    int candidateCount = 0;
    for (int ci = 0; ci < collections.size(); ++ci) {
        if (!collections[ci].isObject()) return fail(QStringLiteral("Collection %1 is not an object.").arg(ci));
        const QJsonObject object = collections[ci].toObject();
        if (!exactKeys(object, {QStringLiteral("name"), QStringLiteral("references")})
                || !object.value(QStringLiteral("name")).isString()
                || !object.value(QStringLiteral("references")).isArray())
            return fail(QStringLiteral("Collection %1 must contain only a name and references array.").arg(ci));
        PreparedCollection next;
        next.name = object.value(QStringLiteral("name")).toString();
        const QJsonArray references = object.value(QStringLiteral("references")).toArray();
        for (int ri = 0; ri < references.size(); ++ri) {
            if (++candidateCount > 10000 || !references[ri].isObject())
                return fail(QStringLiteral("Reference input is invalid or exceeds 10000 candidates."));
            const auto result = identity.collectionReference(references[ri].toObject().toVariantMap());
            if (!result.value(QStringLiteral("ok")).toBool())
                return fail(QStringLiteral("Collection %1, reference %2: %3").arg(ci).arg(ri)
                    .arg(result.value(QStringLiteral("error")).toString()));
            next.references.append(result.value(QStringLiteral("reference")).toMap());
        }
        prepared.append(std::move(next));
    }
    // Validate all references before creating even the isolated staging file.
    if (!containment.enabled() || QFileInfo::exists(output))
        return fail(QStringLiteral("Fixture containment or output availability changed."));
    const QString parent = QFileInfo(output).absolutePath();
    if (!QDir().mkpath(parent)) return fail(QStringLiteral("Could not create the synthetic profile directory."));
    QTemporaryDir staging(parent + QStringLiteral("/.collections-seed-XXXXXX"));
    if (!staging.isValid()) return fail(QStringLiteral("Could not create isolated seed staging."));
    const QString stagedFile = staging.filePath(QStringLiteral("named-collections-v2.json"));
    {
        NamedCollectionsStore store(stagedFile);
        if (!store.healthy()) return fail(store.error());
        // Store prepends new collections, so create in reverse to preserve the
        // requested overview order. Membership saves retain reference order.
        for (int i = prepared.size() - 1; i >= 0; --i) {
            prepared[i].id = store.createCollection(prepared[i].name);
            if (prepared[i].id.isEmpty()) return fail(store.error());
        }
        if (prepared.isEmpty()) {
            const QString transient = store.createCollection(QStringLiteral("Fixture staging"));
            if (transient.isEmpty() || !store.deleteCollection(transient)) return fail(store.error());
        }
        for (const auto &collection : std::as_const(prepared)) {
            for (const auto &reference : collection.references) {
                QStringList members = store.memberships(reference.value(QStringLiteral("key")).toString());
                if (!members.contains(collection.id)) members.append(collection.id);
                const auto result = store.saveMemberships(reference, members);
                if (!result.value(QStringLiteral("ok")).toBool()) return fail(store.error());
            }
        }
    }
    // A fresh real owner must reload the exact staged bytes before publication.
    NamedCollectionsStore reloaded(stagedFile);
    if (!reloaded.healthy() || reloaded.collections().size() != prepared.size())
        return fail(QStringLiteral("Staged store did not survive a real reload."));
    if (!containment.enabled() || QFileInfo::exists(output) || QFileInfo(output).isSymLink())
        return fail(QStringLiteral("Fixture containment or output availability changed before publication."));
    // Same-volume rename refuses an existing destination and does not expose a
    // partially written JSON file. Native store commits already used QSaveFile.
    if (!QFile::rename(stagedFile, output))
        return fail(QStringLiteral("Could not publish seed; an existing target is never overwritten."));
    QJsonArray summary;
    for (const auto &collection : std::as_const(prepared))
        summary.append(QJsonObject{{QStringLiteral("id"),collection.id},{QStringLiteral("name"),collection.name},
            {QStringLiteral("items"),reloaded.items(collection.id).size()}});
    const QJsonObject result{{QStringLiteral("ok"),true},{QStringLiteral("output"),output},
        {QStringLiteral("collections"),summary},{QStringLiteral("referenceCandidates"),candidateCount}};
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
    return 0;
}
