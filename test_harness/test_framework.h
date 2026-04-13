// =============================================================================
// test_framework.h — Lightweight test framework for NTUmbrella plugins
// =============================================================================
// Single-header, zero-dependency test framework compatible with the existing
// LofiOsc/UnitTests.cpp style (struct TestResult, ASSERT_* macros).
//
// Usage:
//   #include "test_framework.h"
//
//   TestResult my_test() {
//       TEST_BEGIN("My Test");
//       ASSERT_TRUE(1 + 1 == 2, "basic math");
//       ASSERT_NEAR(3.14f, 3.14f, 0.01f, "pi approx");
//       TEST_PASS();
//   }
//
//   int main() {
//       return TestRunner::run({my_test});
//   }
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <functional>

// ---------------------------------------------------------------------------
// TestResult — compatible with LofiOsc's existing pattern
// ---------------------------------------------------------------------------
struct TestResult {
    std::string name;
    bool passed;
};

// ---------------------------------------------------------------------------
// Global assertion counters (compatible with LofiOsc UnitTests.cpp)
// ---------------------------------------------------------------------------
static int assertion_count = 0;
static int failed_assertion_count = 0;

// ---------------------------------------------------------------------------
// Macros — same names and semantics as LofiOsc UnitTests.cpp
// ---------------------------------------------------------------------------

// Declares test_name for the ASSERT macros to reference on failure.
#define TEST_BEGIN(name_str) \
    std::string test_name = (name_str); \
    (void)test_name

#define TEST_PASS() \
    return { test_name, true }

#define ASSERT_TRUE(condition, message) \
    do { \
        assertion_count++; \
        if (!(condition)) { \
            std::cerr << "  FAILED: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while (0)

#define ASSERT_FALSE(condition, message) \
    ASSERT_TRUE(!(condition), message)

#define ASSERT_NEAR(a, b, epsilon, message) \
    do { \
        assertion_count++; \
        auto _a = (a); auto _b = (b); \
        if (std::abs(static_cast<double>(_a) - static_cast<double>(_b)) > static_cast<double>(epsilon)) { \
            std::cerr << "  FAILED: " << message \
                      << " (Expected: " << _b << ", Actual: " << _a \
                      << ", Epsilon: " << (epsilon) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while (0)

#define ASSERT_EQ(a, b, message) \
    ASSERT_NEAR(a, b, 0, message)

#define ASSERT_GT(a, b, message) \
    do { \
        assertion_count++; \
        if (!((a) > (b))) { \
            std::cerr << "  FAILED: " << message \
                      << " (" << (a) << " not > " << (b) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while (0)

#define ASSERT_LT(a, b, message) \
    do { \
        assertion_count++; \
        if (!((a) < (b))) { \
            std::cerr << "  FAILED: " << message \
                      << " (" << (a) << " not < " << (b) << ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            failed_assertion_count++; \
            return {test_name, false}; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr, message) \
    ASSERT_TRUE((ptr) != nullptr, message)

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------
namespace TestRunner {

    using TestFn = std::function<TestResult()>;

    inline int run(const std::vector<TestFn>& tests) {
        int passed = 0, failed = 0;
        assertion_count = 0;
        failed_assertion_count = 0;

        std::cout << "\n========================================" << std::endl;
        std::cout << "  NTUmbrella Test Harness" << std::endl;
        std::cout << "========================================\n" << std::endl;

        for (auto& testFn : tests) {
            TestResult result = testFn();
            if (result.passed) {
                std::cout << "  [PASS] " << result.name << std::endl;
                passed++;
            } else {
                std::cout << "  [FAIL] " << result.name << std::endl;
                failed++;
            }
        }

        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "  Results: " << passed << " passed, " << failed << " failed" << std::endl;
        std::cout << "  Assertions: " << assertion_count << " total, "
                  << failed_assertion_count << " failed" << std::endl;
        std::cout << "----------------------------------------\n" << std::endl;

        return (failed > 0) ? 1 : 0;
    }

}  // namespace TestRunner
