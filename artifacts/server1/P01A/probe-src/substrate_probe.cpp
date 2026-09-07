#include <boost/version.hpp>
#include <libtorrent/version.hpp>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <QtCore/qglobal.h>

#include <cstdint>
#include <iostream>

int main()
{
    std::cout << "libtorrent.compile=" << LIBTORRENT_VERSION << '\n';
    std::cout << "libtorrent.runtime=" << libtorrent::version() << '\n';
    std::cout << "libtorrent.revision=" << LIBTORRENT_REVISION << '\n';
    std::cout << "libtorrent.version_num=" << LIBTORRENT_VERSION_NUM << '\n';
    std::cout << "libtorrent.abi=" << TORRENT_ABI_VERSION << '\n';
#ifdef TORRENT_LINKING_SHARED
    std::cout << "libtorrent.linking_shared=1\n";
#else
    std::cout << "libtorrent.linking_shared=0\n";
#endif
#ifdef TORRENT_EXPORT_EXTRA
    std::cout << "libtorrent.export_extra=1\n";
#else
    std::cout << "libtorrent.export_extra=0\n";
#endif

    std::cout << "boost.version=" << BOOST_VERSION << '\n';
    std::cout << "boost.lib_version=" << BOOST_LIB_VERSION << '\n';
    std::cout << "openssl.compile=" << OPENSSL_VERSION_TEXT << '\n';
    std::cout << "openssl.runtime=" << OpenSSL_version(OPENSSL_VERSION) << '\n';
    std::cout << "qt.compile=" << QT_VERSION_STR << '\n';
    std::cout << "qt.runtime=" << qVersion() << '\n';

#if defined(_MSC_VER)
    std::cout << "compiler.family=msvc\n";
    std::cout << "compiler.msc_ver=" << _MSC_VER << '\n';
    std::cout << "compiler.msc_full_ver=" << _MSC_FULL_VER << '\n';
# if defined(_DLL)
    std::cout << "crt.linkage=dynamic\n";
# else
    std::cout << "crt.linkage=static\n";
# endif
#elif defined(__clang__)
    std::cout << "compiler.family=clang\n";
    std::cout << "compiler.version=" << __clang_version__ << '\n';
#elif defined(__GNUC__)
    std::cout << "compiler.family=gcc\n";
    std::cout << "compiler.version=" << __VERSION__ << '\n';
#else
    std::cout << "compiler.family=unknown\n";
#endif

    std::cout << "pointer.bits=" << (sizeof(void*) * 8) << '\n';

    const std::string compileVersion = LIBTORRENT_VERSION;
    const std::string runtimeVersion = libtorrent::version();
    if (compileVersion != runtimeVersion)
    {
        std::cerr << "P01A_FAIL libtorrent header/runtime mismatch: "
                  << compileVersion << " != " << runtimeVersion << '\n';
        return 23;
    }

    std::cout << "P01A_IDENTITY_MATCH=1\n";
    return 0;
}
