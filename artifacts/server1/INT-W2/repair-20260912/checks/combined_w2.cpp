#include "server1/policy/Value.h"
#include "server1/policy/TorrentMetadata.h"
#include "server1/settings/SettingsStore.h"
#include "server1/settings/CachePolicy.h"
#include "server1/platform/DiskSpace.h"
#include "server1/http/HttpContract.h"
#include "server1/ports/ProcessPort.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        QFile file(temporary.filePath("server-settings.json"));
        require(file.open(QIODevice::WriteOnly), "settings fixture unavailable");
        require(file.write("{}") == 2, "settings fixture write failed");
        file.close();
        const auto root = temporary.path().toStdString();
        const auto value = server1::policy::Value::boolean(true);
        require(server1::policy::jsTruthy(value), "K00 conversion unavailable");
        const auto geometry = server1::policy::VirtualPieceMap::create(32769, 32768);
        require(geometry.wireBlocks().size() == 3, "K01 block geometry mismatch");
        require(geometry.wireBlocks().back().length == 1, "K01 final block mismatch");
        std::string metadataError;
        require(!server1::policy::TorrentMetadata::parse({}, &metadataError),
                "K01 malformed metadata unexpectedly accepted");
        server1::settings::SettingsStore settings({root, "4.21.0", root, false, false});
        require(settings.serverVersion() == "4.21.0", "K13 version mismatch");
        require(server1::settings::effectiveEngineDefaults(settings).connections == 55,
                "K13 effective defaults mismatch");
        require(server1::cache::planDeletions({}, 0).empty(), "K13 empty cache mismatch");
        const auto disk = server1::platform::diskSpace(std::filesystem::path(root));
        require(disk.has_value(), "K13 disk-space interface unavailable");
        std::unique_ptr<void, decltype(&server1_http_parser_destroy)> parser(
            server1_http_parser_create(4), &server1_http_parser_destroy);
        const std::string wire = "POST / HTTP/1.1\r\nHost: localhost\r\n"
            "Transfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
        require(server1_http_parser_feed(parser.get(), wire.data(), wire.size()) == 2,
                "H00 oversized chunk not rejected");
        require(server1_http_parser_error_status(parser.get()) == 413,
                "H00 repaired chunk parser did not return 413");
        std::unique_ptr<void, decltype(&server1_http_router_destroy)> router(
            server1_http_router_create(), &server1_http_router_destroy);
        std::unique_ptr<void, decltype(&server1_http_server_destroy)> listener(
            server1_http_server_create(router.get(), 65536), &server1_http_server_destroy);
        require(server1_http_server_listen(listener.get(), 0) == 1,
                "H00 combined listener failed");
        server1_http_server_stop(listener.get());
        server1::media::ProcessPort processPort;
        server1::media::ProcessDriver processDriver(processPort);
        require(processPort.activeCount() == 0, "M00 unexpected owned process");
        require(!processDriver.running(1), "M00 unexpected remote state");
        std::cout << "COMBINED-W2 PASS: K00 K01 K13 H00 M00; oversized-chunk-status=413\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "COMBINED-W2 FAIL: " << error.what() << '\n';
        return 1;
    }
}
