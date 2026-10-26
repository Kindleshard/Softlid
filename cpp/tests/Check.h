#pragma once
// A small test harness, so the tests need nothing installed:
//   TEST(NightFactorRisesAfterStart) { CHECK(...); CHECK_EQ(a, b); CHECK_NEAR(x, y, 1e-9); }
// Every TEST in the program runs; a failed check prints where and what, and the run ends with a non-zero code.
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace bbtest {

struct Test {
    const char* name;
    void (*run)();
};

std::vector<Test>& Registry();
void Fail(const char* file, int line, const std::string& what);

struct Add {
    Add(const char* name, void (*run)()) { Registry().push_back({name, run}); }
};

template <typename T> std::string Show(const T& v)
{
    std::ostringstream out;
    if constexpr (std::is_same_v<T, std::wstring>) {
        for (wchar_t c : v) out << (c < 128 ? static_cast<char>(c) : '?');
    } else {
        out.precision(12);
        out << v;
    }
    return out.str();
}

} // namespace bbtest

#define TEST(name)                                                    \
    static void name();                                               \
    static const bbtest::Add name##Registered(#name, &name);          \
    static void name()

#define CHECK(x)                                                      \
    do {                                                              \
        if (!(x)) bbtest::Fail(__FILE__, __LINE__, #x);               \
    } while (0)

#define CHECK_EQ(a, b)                                                \
    do {                                                              \
        auto va = (a);                                                \
        auto vb = (b);                                                \
        if (!(va == vb))                                              \
            bbtest::Fail(__FILE__, __LINE__, std::string(#a " == " #b "  (") + bbtest::Show(va) + " vs " + bbtest::Show(vb) + ")"); \
    } while (0)

// A value pinned to what the code gave when the test was written (characterization: a change in behaviour shows up
// here). Written with NAN as the expected value, the check prints the current value to pin.
#define CHECK_GOLDEN(a, expected)                                     \
    do {                                                              \
        double va = (a), ve = (expected);                             \
        if (std::isnan(ve)) bbtest::Fail(__FILE__, __LINE__, std::string("golden " #a " = ") + bbtest::Show(va)); \
        else if (!(std::abs(va - ve) <= 1e-9 * (1 + std::abs(ve))))  \
            bbtest::Fail(__FILE__, __LINE__, std::string(#a " changed  (") + bbtest::Show(va) + " vs " + bbtest::Show(ve) + ")"); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                         \
    do {                                                              \
        double va = (a), vb = (b);                                    \
        if (!(std::abs(va - vb) <= (eps)))                            \
            bbtest::Fail(__FILE__, __LINE__, std::string(#a " ~ " #b "  (") + bbtest::Show(va) + " vs " + bbtest::Show(vb) + ")"); \
    } while (0)
