#include "../../native/devtools/CollectionsTestProfileController.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void require(bool ok, const char *message) {
    ++checks;
    if (ok) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open marker");
    require(file.write(bytes) == bytes.size(), "complete marker");
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    require(temp.isValid(), "temporary root exists");
    const QString tag = QStringLiteral("collections-fixture-controller-check");
    const QString root = temp.path() + "/Colosseum-dltest-" + tag;
    require(QDir().mkpath(root), "tagged directory created");
    const QString marker = root + '/' + CollectionsTestProfileController::markerName;
    qputenv("COLOSSEUM_APPDATA_TAG", tag.toUtf8());
    qunsetenv("COLOSSEUM_COLLECTIONS_TEST_FIXTURES");
    QString alias = QStringLiteral("local");
    int activations = 0;
    CollectionsTestProfileController controller(root,
        [&](const QString &next) { ++activations; alias = next; return true; },
        [&] { return QJsonObject{{"profileAlias", alias}, {"collectionsCount", 3}}; });
    require(!controller.enabled(), "no launch opt-in is disabled");
    qputenv("COLOSSEUM_COLLECTIONS_TEST_FIXTURES", "1");
    require(!controller.enabled(), "missing marker is disabled");
    write(marker, "wrong marker\n");
    require(!controller.enabled(), "wrong marker is disabled");
    write(marker, CollectionsTestProfileController::markerContents);
    require(controller.enabled(), "explicit isolated marker enables fixture");
    qputenv("COLOSSEUM_APPDATA_TAG", "daily");
    require(!controller.enabled(), "untagged daily shape is rejected");
    qputenv("COLOSSEUM_APPDATA_TAG", tag.toUtf8());
    CollectionsTestProfileController wrongRoot(temp.path(), {}, {});
    require(!wrongRoot.enabled(), "root must match tagged app identity");
    require(!controller.execute({{"action", "activate-arbitrary"}}).value("success").toBool(), "unknown action rejected");
    require(!controller.execute({{"action", "activate-a"}, {"id", "user-account"}}).value("success").toBool(), "extra account selector rejected");
    require(activations == 0, "invalid payload never invokes owner");
    auto status = controller.execute({{"action", "status"}});
    require(status.value("success").toBool() && status.value("collectionsCount").toInt() == 3, "status returns observed owner state");
    for (const auto &pair : {qMakePair(QStringLiteral("activate-a"), QStringLiteral("A")),
                             qMakePair(QStringLiteral("activate-b"), QStringLiteral("B")),
                             qMakePair(QStringLiteral("activate-local"), QStringLiteral("local"))}) {
        auto reply = controller.execute({{"action", pair.first}});
        require(reply.value("success").toBool() && reply.value("profileAlias").toString() == pair.second,
                "fixed alias activation reports observed result");
    }
    alias = "real-account";
    require(!controller.execute({{"action", "activate-a"}}).value("success").toBool(), "unknown current profile cannot be switched");
    require(activations == 3, "unknown profile did not invoke owner");
    alias = "local";
    CollectionsTestProfileController rejected(root, [](const QString &) { return false; },
        [] { return QJsonObject{{"profileAlias", "local"}}; });
    require(rejected.execute({{"action", "activate-b"}}).value("errorCategory") == "activation-failed", "owner failure is explicit");
    CollectionsTestProfileController falseSuccess(root, [](const QString &) { return true; },
        [] { return QJsonObject{{"profileAlias", "local"}}; });
    require(falseSuccess.execute({{"action", "activate-b"}}).value("errorCategory") == "activation-unconfirmed", "unobserved activation cannot claim success");
    require(QFile::remove(marker), "remove marker");
    require(!controller.execute({{"action", "activate-a"}}).value("success").toBool(), "containment rechecked per command");
    require(activations == 3, "disabled command never invokes owner");
    std::cout << "COLLECTIONS_FIXTURE_CONTRACT_OK " << checks << " assertions\n";
    return 0;
}
