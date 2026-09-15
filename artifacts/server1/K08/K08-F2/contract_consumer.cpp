#include "server1/policy/FileReader.h"

#include <string>
#include <string_view>
#include <type_traits>

using server1::policy::FileReader;

#if defined(K08F2_NEGATE_RETURN)
using ExpectedFail = bool (FileReader::*)(std::string);
#elif defined(K08F2_NEGATE_CONST)
using ExpectedFail = void (FileReader::*)(std::string) const;
#elif defined(K08F2_NEGATE_NOEXCEPT)
using ExpectedFail = void (FileReader::*)(std::string) noexcept;
#elif defined(K08F2_NEGATE_VIEW)
using ExpectedFail = void (FileReader::*)(std::string_view);
#elif defined(K08F2_NEGATE_CONST_REF)
using ExpectedFail = void (FileReader::*)(const std::string &);
#elif defined(K08F2_NEGATE_NO_ARG)
using ExpectedFail = void (FileReader::*)();
#else
using ExpectedFail = void (FileReader::*)(std::string);
#endif

static_assert(std::is_same_v<decltype(&FileReader::fail), ExpectedFail>,
              "K08-F2 FileReader::fail contract mismatch");

int main()
{
    return 0;
}
