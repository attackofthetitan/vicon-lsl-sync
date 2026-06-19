#pragma once

#ifdef VICON_LSL_USE_CATCH2

#include <catch2/catch_test_macros.hpp>

#define REQUIRE_EQ(left, right) REQUIRE((left) == (right))

#else

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test_support {

using TestFunction = std::function<void()>;

struct TestCase {
    std::string name;
    TestFunction function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string name, TestFunction function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

class AssertionFailure : public std::runtime_error {
public:
    explicit AssertionFailure(const std::string& message)
        : std::runtime_error(message) {}
};

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }

    std::ostringstream out;
    out << file << ":" << line << ": requirement failed: " << expression;
    throw AssertionFailure(out.str());
}

template <class Left, class Right>
void requireEqual(const Left& left,
                  const Right& right,
                  const char* left_expr,
                  const char* right_expr,
                  const char* file,
                  int line) {
    if (left == right) {
        return;
    }

    std::ostringstream out;
    out << file << ":" << line << ": equality failed: " << left_expr << " == " << right_expr
        << " (values differ)";
    throw AssertionFailure(out.str());
}

inline int runAllTests() {
    int failures = 0;
    for (const auto& test : registry()) {
        try {
            test.function();
            std::cout << "[pass] " << test.name << std::endl;
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": " << ex.what() << std::endl;
        } catch (...) {
            ++failures;
            std::cerr << "[fail] " << test.name << ": unknown exception" << std::endl;
        }
    }

    std::cout << registry().size() - static_cast<std::size_t>(failures) << "/"
              << registry().size() << " tests passed" << std::endl;
    return failures == 0 ? 0 : 1;
}

} // namespace test_support

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name) \
    static void TEST_CONCAT(test_case_, __LINE__)(); \
    static test_support::Registrar TEST_CONCAT(test_registrar_, __LINE__)( \
        name, TEST_CONCAT(test_case_, __LINE__)); \
    static void TEST_CONCAT(test_case_, __LINE__)()

#define REQUIRE(expression) \
    test_support::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define REQUIRE_EQ(left, right) \
    test_support::requireEqual((left), (right), #left, #right, __FILE__, __LINE__)

#endif
