#include <cstring>
#include <iostream>

#include <libtorrent/version.hpp>

int main()
{
    const char* linked = lt::version();
    std::cout << "compile_version=" << LIBTORRENT_VERSION << '\n';
    std::cout << "compile_revision=" << LIBTORRENT_REVISION << '\n';
    std::cout << "linked_version=" << linked << '\n';
    if (std::strcmp(linked, LIBTORRENT_VERSION) != 0) {
        std::cerr << "IDENTITY_MISMATCH" << '\n';
        return 2;
    }
    return 0;
}
