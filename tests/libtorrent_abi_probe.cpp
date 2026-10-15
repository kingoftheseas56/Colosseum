// A library-only session can hide an incompatible application Asio destructor
// through COMDAT selection. Keep the client destructor in the link graph, as
// Colosseum's TorrentEngine does, while using only libtorrent's real context.
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/version.hpp>
#include <iostream>
#include <Windows.h>

__declspec(noinline) void destroyClientContext(boost::asio::io_context* context)
{
    delete context;
}

int main(int argc, char**)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    void (*volatile destructor)(boost::asio::io_context*) = destroyClientContext;
    if (argc == 1000)
        destructor(nullptr);

    std::cout << "BOOST_VERSION=" << BOOST_VERSION << std::endl;
    {
        libtorrent::session_params params;
        params.settings.set_str(libtorrent::settings_pack::listen_interfaces, "");
        params.settings.set_bool(libtorrent::settings_pack::enable_dht, false);
        params.settings.set_bool(libtorrent::settings_pack::enable_lsd, false);
        params.settings.set_bool(libtorrent::settings_pack::enable_upnp, false);
        params.settings.set_bool(libtorrent::settings_pack::enable_natpmp, false);
        libtorrent::session session(std::move(params));
        std::cout << "SESSION_CONSTRUCTED" << std::endl;
    }
    std::cout << "SESSION_DESTROYED" << std::endl;
}
