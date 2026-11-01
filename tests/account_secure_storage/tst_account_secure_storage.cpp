#include "account/PlatformAccountCredentialStore.h"
#include "account/AccountCredentialStoreFactory.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTest>
#ifdef COLOSSEUM_SECRET_SERVICE_TEST
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#endif
#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QJniObject>
#include "account/AndroidSecureStorageBackend.h"
#include "account/AndroidJniSecureStorageBackend.h"
#include "account/AndroidAccountCredentialStore.h"
#endif

namespace {
const QString kAccount = QStringLiteral("12345678-1234-1234-1234-123456789abc");
const QString kDevice = QStringLiteral("abcdefab-1234-1234-1234-123456789abc");
const QString kRequest = QStringLiteral("fedcbafe-1234-1234-1234-123456789abc");
const QByteArray kToken("refresh\0binary", 14);

class MemorySecrets final : public AccountSecretBackend {
public:
    bool available = true;
    bool failWrites = false;
    QHash<QString, QByteArray> entries;
    bool isAvailable() const override { return available; }
    bool write(const QString &name, const QByteArray &blob) override {
        if (!available || failWrites)
            return false;
        entries.insert(name, blob);
        return true;
    }
    std::optional<QByteArray> read(const QString &name) const override {
        if (!available || !entries.contains(name))
            return std::nullopt;
        return entries.value(name);
    }
    bool remove(const QString &name) override {
        if (!available)
            return false;
        entries.remove(name);
        return true;
    }
    QStringList targets(const QString &prefix) const override {
        QStringList result;
        for (const QString &name : entries.keys()) {
            if (name.startsWith(prefix))
                result.append(name);
        }
        return result;
    }
};

StoredAccountCredential credential() { return {kAccount, kDevice, kToken}; }
StoredAccountDeletion deletion() { return {kAccount, kRequest, QByteArray(32, '\x07')}; }

bool empty(AccountCredentialStore &store) {
    return !store.loadActive() && store.pendingRevocations().isEmpty()
        && store.pendingDeletions().isEmpty() && !store.loadStremio(QStringLiteral("local"), QStringLiteral("remote"));
}

bool nativeMode(const QString &mode) {
    if (!qEnvironmentVariable("COLOSSEUM_APPDATA_TAG").startsWith(QStringLiteral("account-storage-test-")))
        return false;
    auto ownedStore = createAccountCredentialStore();
    AccountCredentialStore &store = *ownedStore;
    if (mode == QLatin1String("--unavailable"))
        return !store.isAvailable() && !store.saveActive(credential()) && !store.loadActive();
    if (mode == QLatin1String("--clear"))
        return store.clearActive() && store.removePendingRevocation(kToken)
            && store.removePendingDeletion(kRequest) && store.clearStremio(QStringLiteral("local"));
    if (!store.isAvailable())
        return false;
#ifdef COLOSSEUM_SECRET_SERVICE_TEST
    if (mode == QLatin1String("--locked-delete")) {
        QDBusInterface service(QStringLiteral("org.freedesktop.secrets"),
            QStringLiteral("/org/freedesktop/secrets"), QStringLiteral("org.freedesktop.Secret.Service"), QDBusConnection::sessionBus());
        QDBusReply<QDBusObjectPath> alias = service.call(QStringLiteral("ReadAlias"), QStringLiteral("default"));
        if (!alias.isValid() || alias.value().path() == QLatin1String("/"))
            return false;
        const auto locked = service.call(QStringLiteral("Lock"), QVariant::fromValue(QList<QDBusObjectPath>{alias.value()}));
        if (locked.type() == QDBusMessage::ErrorMessage)
            return false;
        QDBusInterface collection(QStringLiteral("org.freedesktop.secrets"), alias.value().path(),
            QStringLiteral("org.freedesktop.Secret.Collection"), QDBusConnection::sessionBus());
        if (!collection.property("Locked").toBool())
            return false;
        const auto before = qdbus_cast<QList<QDBusObjectPath>>(collection.property("Items"));
        const bool rejected = !store.clearActive() && !store.removePendingRevocation(kToken)
            && !store.removePendingDeletion(kRequest) && !store.clearStremio(QStringLiteral("local"));
        const auto after = qdbus_cast<QList<QDBusObjectPath>>(collection.property("Items"));
        return rejected && before.size() == 4 && after == before;
    }
#endif
    if (mode == QLatin1String("--seed"))
        return empty(store) && store.saveActive(credential())
            && store.addPendingRevocation(kToken) && store.savePendingDeletion(deletion())
            && store.saveStremio({QStringLiteral("local"), QStringLiteral("remote"), kToken});
    if (mode == QLatin1String("--verify")) {
        const auto active = store.loadActive();
        const auto stremio = store.loadStremio(QStringLiteral("local"), QStringLiteral("remote"));
        const auto deletions = store.pendingDeletions();
        return active && active->accountId == kAccount && active->deviceId == kDevice
            && active->refreshToken == kToken && store.pendingRevocations() == QList<QByteArray>{kToken}
            && deletions.size() == 1 && deletions.first().retryCapability == deletion().retryCapability
            && stremio && stremio->authKey == kToken;
    }
#ifdef Q_OS_ANDROID
    if (mode == QLatin1String("--tamper")) {
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        const auto directory = context.callObjectMethod("getNoBackupFilesDir", "()Ljava/io/File;");
        const QString root = directory.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
        const QString name = QString::fromLatin1(QCryptographicHash::hash(PlatformAccountCredentialStore::activeTargetName().toUtf8(), QCryptographicHash::Sha256).toHex());
        QFile file(root + QStringLiteral("/colosseum-account-secrets-v1/") + name + QStringLiteral(".secret"));
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QByteArray original = file.readAll();
        file.close();
        QJsonObject envelope = QJsonDocument::fromJson(original).object();
        QByteArray payload = QByteArray::fromBase64(envelope.value(QStringLiteral("ciphertext")).toString().toLatin1());
        if (payload.isEmpty() || original.contains(kToken))
            return false;
        payload[0] = char(payload[0] ^ 1);
        envelope.insert(QStringLiteral("ciphertext"), QString::fromLatin1(payload.toBase64()));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        file.write(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
        file.close();
        const bool rejected = !store.loadActive();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const bool restored = file.write(original) == original.size();
        file.close();
        return rejected && restored && store.loadActive().has_value();
    }
    if (mode == QLatin1String("--key-loss")) {
        const auto provider = QJniObject::fromString(QStringLiteral("AndroidKeyStore"));
        const auto alias = QJniObject::fromString(QStringLiteral("org.brotherhood.colosseum.account.v1"));
        const auto keystore = QJniObject::callStaticObjectMethod("java/security/KeyStore", "getInstance",
            "(Ljava/lang/String;)Ljava/security/KeyStore;", provider.object<jstring>());
        keystore.callMethod<void>("load", "(Ljava/io/InputStream;[C)V", nullptr, nullptr);
        keystore.callMethod<void>("deleteEntry", "(Ljava/lang/String;)V", alias.object<jstring>());
        return !store.isAvailable() && !store.loadActive() && !store.saveActive(credential());
    }
#endif
    return mode == QLatin1String("--empty") && empty(store);
}
}

class AccountSecureStorageTest : public QObject {
    Q_OBJECT
private slots:
    void factorySelectsCurrentPlatform() {
        auto store = createAccountCredentialStore();
        QVERIFY(dynamic_cast<PlatformAccountCredentialStore *>(store.get()));
#ifdef Q_OS_ANDROID
        QVERIFY(store->isAvailable());
#endif
    }
#ifdef Q_OS_ANDROID
    void legacyAndroidJavaVaultSurvivesRotation() {
        const QByteArray tag = qgetenv("COLOSSEUM_APPDATA_TAG");
        struct RestoreTag { QByteArray tag; ~RestoreTag() { qputenv("COLOSSEUM_APPDATA_TAG", tag); } } restore{tag};
        qunsetenv("COLOSSEUM_APPDATA_TAG");
        AndroidJniSecureStorageBackend backend;
        AndroidAccountCredentialStore legacy(&backend);
        auto current = createAccountCredentialStore();
        QVERIFY(current->clearActive());
        QVERIFY(current->removePendingRevocation(kToken));
        QVERIFY(legacy.saveActive(credential()));
        QVERIFY(legacy.addPendingRevocation(kToken));
        const auto active = current->loadActive();
        QVERIFY(active);
        QCOMPARE(active->refreshToken, kToken);
        QCOMPARE(current->pendingRevocations(), QList<QByteArray>{kToken});
        auto rotated = credential();
        rotated.refreshToken = "rotated-java-session";
        QVERIFY(current->saveActive(rotated));
        QVERIFY(!legacy.loadActive());
        QVERIFY(current->removePendingRevocation(kToken));
        QVERIFY(legacy.pendingRevocations().isEmpty());
        QVERIFY(current->clearActive());
        QVERIFY(!current->loadActive());
    }
    void legacyAndroidSessionsSurviveRotation() {
        class LegacySecrets final : public AndroidSecureStorageBackend {
        public:
            QHash<QString, QByteArray> entries;
            bool failRemoval = false;
            bool isAvailable() const override { return true; }
            std::optional<QByteArray> read(const QString &name) const override {
                return entries.contains(name) ? std::optional<QByteArray>(entries.value(name)) : std::nullopt;
            }
            bool write(const QString &name, const QByteArray &value) override { entries.insert(name, value); return true; }
            bool remove(const QString &name) override { if (failRemoval) return false; entries.remove(name); return true; }
            QStringList keys(const QString &prefix) const override {
                QStringList result;
                for (const QString &name : entries.keys()) { if (name.startsWith(prefix)) result.append(name); }
                return result;
            }
        } legacy;
        const QByteArray tag = qgetenv("COLOSSEUM_APPDATA_TAG");
        struct RestoreTag { QByteArray tag; ~RestoreTag() { qputenv("COLOSSEUM_APPDATA_TAG", tag); } } restore{tag};
        qunsetenv("COLOSSEUM_APPDATA_TAG"); // Separate test APK; exercises actual historical key names.
        PlatformAccountCredentialStore store(createAndroidAccountSecretBackend(&legacy));
        QVERIFY(store.clearActive());
        QVERIFY(store.removePendingRevocation(kToken));
        const QString oldActive = QStringLiteral("colosseum.account.active.v1");
        const QString oldPending = QStringLiteral("colosseum.account.pending-revocation.v1.")
            + QString::fromLatin1(QCryptographicHash::hash(kToken, QCryptographicHash::Sha256).toHex());
        legacy.entries.insert(oldActive, QJsonDocument(QJsonObject{
            {QStringLiteral("version"), 1}, {QStringLiteral("account_id"), kAccount},
            {QStringLiteral("device_id"), kDevice}, {QStringLiteral("refresh_token"), QString::fromLatin1(kToken.toBase64())}}).toJson());
        legacy.entries.insert(oldPending, kToken);
        const auto restored = store.loadActive();
        QVERIFY(restored);
        QCOMPARE(restored->refreshToken, kToken);
        QCOMPARE(store.pendingRevocations(), QList<QByteArray>{kToken});
        legacy.failRemoval = true;
        QVERIFY(!store.clearActive());
        QVERIFY(legacy.entries.contains(oldActive));
        legacy.failRemoval = false;
        auto rotated = credential();
        rotated.refreshToken = "rotated-android-session";
        QVERIFY(store.saveActive(rotated));
        QVERIFY(!legacy.entries.contains(oldActive));
        QCOMPARE(store.loadActive()->refreshToken, rotated.refreshToken);
        QVERIFY(store.removePendingRevocation(kToken));
        QVERIFY(legacy.entries.isEmpty());
        QVERIFY(store.clearActive());
        QVERIFY(!store.loadActive());
    }
#endif
    void activeRoundTripAndRotation() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        QVERIFY(store.saveActive(credential()));
        QCOMPARE(store.loadActive()->refreshToken, kToken);
        auto rotated = credential();
        rotated.refreshToken = "rotated";
        QVERIFY(store.saveActive(rotated));
        QCOMPARE(store.loadActive()->refreshToken, QByteArray("rotated"));
        QVERIFY(store.clearActive());
        QVERIFY(!store.loadActive());
        QVERIFY(store.clearActive());
    }
    void invalidOrCorruptCredentialsAreRejected() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        auto invalid = credential();
        invalid.deviceId = "invalid";
        QVERIFY(!store.saveActive(invalid));
        QVERIFY(backend->entries.isEmpty());
        backend->entries.insert(store.activeTargetName(), "{corrupt");
        QVERIFY(!store.loadActive());
        backend->entries.insert(store.activeTargetName(), "{\"version\":999}");
        QVERIFY(!store.loadActive());
    }
    void failuresDoNotBecomeSuccessfulSessions() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        backend->available = false;
        QVERIFY(!store.isAvailable());
        QVERIFY(!store.saveActive(credential()));
        QVERIFY(!store.clearActive());
        backend->available = true;
        backend->failWrites = true;
        QVERIFY(!store.saveActive(credential()));
        QVERIFY(!store.loadActive());
    }
    void revocationEnumerationUsesExactNamespace() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        QVERIFY(store.addPendingRevocation(kToken));
        QVERIFY(store.addPendingRevocation(kToken));
        backend->entries.insert(store.pendingTargetPrefix() + "not-a-digest", "foreign");
        backend->entries.insert(store.pendingTargetPrefix() + QString(65, 'a'), "foreign");
        QCOMPARE(store.pendingRevocations(), QList<QByteArray>{kToken});
        QVERIFY(store.removePendingRevocation(kToken));
        QVERIFY(store.pendingRevocations().isEmpty());
    }
    void deletionRetryCannotBeReplaced() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        QVERIFY(store.savePendingDeletion(deletion()));
        QVERIFY(store.savePendingDeletion(deletion()));
        auto other = deletion();
        other.retryCapability = QByteArray(32, '\x08');
        QVERIFY(!store.savePendingDeletion(other));
        QCOMPARE(store.pendingDeletions().size(), 1);
        QCOMPARE(store.pendingDeletions().first().retryCapability, deletion().retryCapability);
        QVERIFY(store.removePendingDeletion(kRequest));
        QVERIFY(store.pendingDeletions().isEmpty());
    }
    void stremioCredentialsStayBoundToProfileAndAccount() {
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        QVERIFY(store.saveStremio({QStringLiteral("local"), QStringLiteral("remote"), kToken}));
        QVERIFY(!store.loadStremio(QStringLiteral("local"), QStringLiteral("other")));
        QVERIFY(!store.loadStremio(kAccount, QStringLiteral("remote")));
        QCOMPARE(store.loadStremio(QStringLiteral("local"), QStringLiteral("remote"))->authKey, kToken);
    }
    void taggedRunsCannotReadDailyCredentials() {
        const QByteArray previous = qgetenv("COLOSSEUM_APPDATA_TAG");
        auto backend = std::make_shared<MemorySecrets>();
        PlatformAccountCredentialStore store(backend);
        qunsetenv("COLOSSEUM_APPDATA_TAG");
        QVERIFY(store.saveActive(credential()));
        const QString daily = store.activeTargetName();
        qputenv("COLOSSEUM_APPDATA_TAG", "isolated-test");
        QVERIFY(!store.loadActive());
        QVERIFY(store.activeTargetName() != daily);
        QVERIFY(store.saveActive(credential()));
        QCOMPARE(backend->entries.size(), 2);
        if (previous.isNull())
            qunsetenv("COLOSSEUM_APPDATA_TAG");
        else
            qputenv("COLOSSEUM_APPDATA_TAG", previous);
    }
};

int main(int argc, char **argv) {
#ifdef Q_OS_ANDROID
    QGuiApplication app(argc, argv);
    qputenv("COLOSSEUM_APPDATA_TAG", "account-storage-test-android-qualification");
#else
    QCoreApplication app(argc, argv);
#endif
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--integration")) {
        bool success = true;
        for (const QString &mode : {QStringLiteral("--seed"), QStringLiteral("--verify"), QStringLiteral("--clear"), QStringLiteral("--empty")}) {
            QProcess process;
            process.start(app.applicationFilePath(), {mode});
            if (!process.waitForFinished(20000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
                qCritical("Platform credential step failed: %s", qPrintable(mode));
                success = false;
                break;
            }
        }
        QProcess cleanup;
        cleanup.start(app.applicationFilePath(), {QStringLiteral("--clear")});
        const bool cleaned = cleanup.waitForFinished(20000) && cleanup.exitCode() == 0;
        return success && cleaned ? 0 : 1;
    }
    if (argc == 2 && QString::fromLocal8Bit(argv[1]).startsWith(QStringLiteral("--"))) {
        const bool passed = nativeMode(QString::fromLocal8Bit(argv[1]));
        qInfo("ACCOUNT_STORAGE_MODE %s %s", argv[1], passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }
    AccountSecureStorageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_account_secure_storage.moc"
