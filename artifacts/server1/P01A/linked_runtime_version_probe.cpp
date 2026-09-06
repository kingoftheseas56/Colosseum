#include <cstring>
#include <iostream>

#include <libtorrent/version.hpp>

int main()
{
    const char* linkedRuntimeVersion = lt::version();
    std::cout << "header_declared_version=" << LIBTORRENT_VERSION << '\n';
    std::cout << "header_declared_revision=" << LIBTORRENT_REVISION << '\n';
    std::cout << "linked_runtime_version=" << linkedRuntimeVersion << '\n';
    if (std::strcmp(linkedRuntimeVersion, LIBTORRENT_VERSION) != 0) {
        std::cerr << "VERSION_MISMATCH" << '\n';
        return 2;
    }
    return 0;
}
