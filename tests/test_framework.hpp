// Minimal test registration, assertions and temporary-directory helper.

#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hdrcal::test {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

class Registrar {
public:
    Registrar(std::string name, std::function<void()> body);
};

[[noreturn]] void fail(const char* file, int line, const std::string& message);

template <typename T> void appendPrintable(std::ostringstream& stream, const T& value) {
    if constexpr (requires { stream << value; })
        stream << value;
    else
        stream << "<valor não imprimível>";
}

template <typename Actual, typename Expected>
void requireEqual(const Actual& actual, const Expected& expected, const char* actualExpression, const char* expectedExpression, const char* file, int line) {
    if (actual == expected)
        return;

    std::ostringstream message;
    message << "esperado " << expectedExpression << " == ";
    appendPrintable(message, expected);
    message << ", obtido " << actualExpression << " == ";
    appendPrintable(message, actual);
    fail(file, line, message.str());
}

inline void requireNear(double actual, double expected, double absoluteTolerance, const char* expression, const char* file, int line) {
    if (std::abs(actual - expected) <= absoluteTolerance)
        return;

    std::ostringstream message;
    message << expression << " = " << actual << ", esperado " << expected << " ± " << absoluteTolerance;
    fail(file, line, message.str());
}

inline void requireRelativeNear(double actual, double expected, double relativeTolerance, const char* expression, const char* file, int line) {
    const double scale = std::max(std::abs(expected), 1e-12);
    if (std::abs(actual - expected) / scale <= relativeTolerance)
        return;

    std::ostringstream message;
    message << expression << " = " << actual << ", esperado " << expected << " (tolerância relativa " << relativeTolerance << ")";
    fail(file, line, message.str());
}

// Temporary directory that removes itself on destruction. Tests that touch the
// filesystem use it so failures cannot leave artifacts behind.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::string& prefix);
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

} // namespace hdrcal::test

#define HDRCAL_TEST(name)                                                                                                                                      \
    static void name();                                                                                                                                        \
    static ::hdrcal::test::Registrar name##Registrar(#name, name);                                                                                             \
    static void name()

#define HDRCAL_REQUIRE(expression)                                                                                                                             \
    do {                                                                                                                                                       \
        if (!(expression))                                                                                                                                     \
            ::hdrcal::test::fail(__FILE__, __LINE__, "requisito falhou: " #expression);                                                                        \
    } while (false)

#define HDRCAL_REQUIRE_EQ(actual, expected) ::hdrcal::test::requireEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#define HDRCAL_REQUIRE_NEAR(actual, expected, tolerance) ::hdrcal::test::requireNear((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)

#define HDRCAL_REQUIRE_RELATIVE_NEAR(actual, expected, tolerance)                                                                                              \
    ::hdrcal::test::requireRelativeNear((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)
