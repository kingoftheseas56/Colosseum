#ifndef NUVIO_ENGINE_TEST_SUPPORT_HPP
#define NUVIO_ENGINE_TEST_SUPPORT_HPP

#include <cstdint>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nuvio::test {

using Test = std::pair<std::string, std::function<void()>>;

std::vector<Test>& registry();

std::string send_http_request(std::uint16_t port, const std::string& request);

struct Registrar {
    Registrar(std::string name, std::function<void()> test);
};

template <typename Actual, typename Expected>
void expect_equal(
    const Actual& actual,
    const Expected& expected,
    const char* actual_expression,
    const char* expected_expression
) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << "expected " << actual_expression << " to equal " << expected_expression;
        throw std::runtime_error(message.str());
    }
}

inline void expect_true(const bool value, const char* expression) {
    if (!value) {
        throw std::runtime_error(std::string("expected true: ") + expression);
    }
}

}

#define NUVIO_TEST_CONCAT_INNER(left, right) left##right
#define NUVIO_TEST_CONCAT(left, right) NUVIO_TEST_CONCAT_INNER(left, right)
#define NUVIO_TEST(name) \
    static void NUVIO_TEST_CONCAT(test_, __LINE__)(); \
    static ::nuvio::test::Registrar NUVIO_TEST_CONCAT(registrar_, __LINE__)( \
        name, NUVIO_TEST_CONCAT(test_, __LINE__) \
    ); \
    static void NUVIO_TEST_CONCAT(test_, __LINE__)()
#define NUVIO_EXPECT_EQ(actual, expected) \
    ::nuvio::test::expect_equal((actual), (expected), #actual, #expected)
#define NUVIO_EXPECT_TRUE(expression) ::nuvio::test::expect_true((expression), #expression)

#endif
