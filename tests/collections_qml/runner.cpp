#include "NamedCollectionsStore.h"
#include "engine/ColosseumTitleIdentityRegistry.h"

#include <QtQuickTest/quicktest.h>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

class CollectionsTestFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *store READ store NOTIFY storeChanged)
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString setupError READ setupError NOTIFY setupErrorChanged)
public:
    explicit CollectionsTestFixture(QObject *parent = nullptr) : QObject(parent)
    {
        if (!m_directory.isValid()) qFatal("Collections test temporary directory could not be created");
        const QString identityPath = m_directory.filePath(QStringLiteral("identity-fixture.json"));
        QFile identityFile(identityPath);
        const QByteArray identityBytes = R"({"version":1,"titles":[]})";
        if (!identityFile.open(QIODevice::WriteOnly)
            || identityFile.write(identityBytes) != identityBytes.size())
            qFatal("Collections test identity fixture could not be written");
        identityFile.close();
        m_identity = std::make_unique<ColosseumTitleIdentityRegistry>(identityPath);
        if (!m_identity->ready()) qFatal("Real CollectionsIdentity could not load the test catalogue");
        for (int i = 0; i < 2; ++i)
            if (!m_identity->collectionReference(rawReference(i)).value(QStringLiteral("ok")).toBool())
                qFatal("Real CollectionsIdentity rejected a declared exact-ID test fixture");
        if (!reset()) qFatal("Initial isolated Collections store could not be created");
    }

    QObject *store() const { return m_store.get(); }
    ColosseumTitleIdentityRegistry *identity() const { return m_identity.get(); }
    bool ready() const { return m_identity && m_identity->ready() && m_store && m_store->healthy(); }
    QString setupError() const { return m_setupError; }

    // Reset changes test profile ownership only. Every collection mutation is
    // performed through the real QML controls in tst_CollectionsNative.qml.
    Q_INVOKABLE bool reset()
    {
        emit profileChanging();
        const QString path = m_directory.filePath(QStringLiteral("profile-%1/collections.json").arg(++m_profile));
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            m_setupError = QStringLiteral("Could not create an isolated test profile");
            emit setupErrorChanged();
            return false;
        }
        auto previous = std::move(m_store);
        m_previousPath = previous ? previous->filePath() : QString();
        m_store = std::make_unique<NamedCollectionsStore>(path);
        m_setupError = m_store->error();
        emit storeChanged();
        emit setupErrorChanged();
        return m_store->healthy();
    }

    // Exact film and manga IDs are intentionally paired with fixture display
    // metadata and no artwork. The production registry creates their canonical
    // references; there is no test implementation of identity derivation.
    Q_INVOKABLE QVariantMap rawReference(int index) const
    {
        if (index == 0)
            return {{QStringLiteral("world"), QStringLiteral("theatre")},
                {QStringLiteral("kind"), QStringLiteral("movie")},
                {QStringLiteral("source"), QStringLiteral("theatre")},
                {QStringLiteral("sourceId"), QStringLiteral("tt2543164")},
                {QStringLiteral("editionId"), QString()},
                {QStringLiteral("title"), QStringLiteral("Fixture film")},
                {QStringLiteral("creator"), QStringLiteral("Fixture director")},
                {QStringLiteral("year"), QStringLiteral("2016")},
                {QStringLiteral("route"), QStringLiteral("theatre-detail")},
                {QStringLiteral("payload"), QVariantMap()}};
        if (index == 1)
            return {{QStringLiteral("world"), QStringLiteral("tankoban")},
                {QStringLiteral("kind"), QStringLiteral("manga")},
                {QStringLiteral("source"), QStringLiteral("mal")},
                {QStringLiteral("sourceId"), QStringLiteral("mal:2")},
                {QStringLiteral("editionId"), QString()},
                {QStringLiteral("title"), QStringLiteral("Fixture manga")},
                {QStringLiteral("creator"), QStringLiteral("Fixture author")},
                {QStringLiteral("year"), QString()},
                {QStringLiteral("route"), QStringLiteral("manga-detail")},
                {QStringLiteral("payload"), QVariantMap()}};
        return {};
    }
    Q_INVOKABLE QVariantMap reference(int index) const
    {
        return m_identity->collectionReference(rawReference(index)).value(QStringLiteral("reference")).toMap();
    }
    Q_INVOKABLE QVariantMap diskState(bool previous = false) const
    {
        const QString path = previous ? m_previousPath : m_store->filePath();
        if (path.isEmpty()) return {{QStringLiteral("ok"), false}};
        NamedCollectionsStore reread(path);
        return {{QStringLiteral("ok"), reread.healthy()},
            {QStringLiteral("fileExists"), QFileInfo::exists(path)},
            {QStringLiteral("collections"), reread.collections()},
            {QStringLiteral("error"), reread.error()}};
    }

signals:
    void profileChanging();
    void storeChanged();
    void setupErrorChanged();

private:
    QTemporaryDir m_directory;
    std::unique_ptr<ColosseumTitleIdentityRegistry> m_identity;
    std::unique_ptr<NamedCollectionsStore> m_store;
    QString m_previousPath;
    QString m_setupError;
    int m_profile = 0;
};

class CollectionsQmlSetup final : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ColosseumTests"));
        QCoreApplication::setApplicationName(QStringLiteral("NamedCollectionsQmlTests"));
        QStandardPaths::setTestModeEnabled(true);
    }
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        m_fixture = std::make_unique<CollectionsTestFixture>();
        engine->rootContext()->setContextProperty(QStringLiteral("CollectionsTest"), m_fixture.get());
        engine->rootContext()->setContextProperty(QStringLiteral("CollectionsIdentity"), m_fixture->identity());
        engine->rootContext()->setContextProperty(QStringLiteral("NamedCollections"), m_fixture->store());
        connect(m_fixture.get(), &CollectionsTestFixture::storeChanged, engine, [this, engine]() {
            engine->rootContext()->setContextProperty(QStringLiteral("NamedCollections"), m_fixture->store());
        });
        qInfo().noquote() << "COLLECTIONS_QML_NATIVE_OWNER" << m_fixture->store()->metaObject()->className()
                         << "IDENTITY_OWNER" << m_fixture->identity()->metaObject()->className()
                         << "QT" << qVersion() << "ISOLATED_TEMP_PROFILES";
    }
private:
    std::unique_ptr<CollectionsTestFixture> m_fixture;
};

QUICK_TEST_MAIN_WITH_SETUP(named_collections, CollectionsQmlSetup)
#include "runner.moc"
