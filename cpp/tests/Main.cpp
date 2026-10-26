// Runs every TEST and reports the failures. Exit code: the number of failed tests (0 = all passed).
#include "Check.h"
#include <cstdio>

namespace bbtest {

std::vector<Test>& Registry()
{
    static std::vector<Test> tests;
    return tests;
}

static int failures = 0;

void Fail(const char* file, int line, const std::string& what)
{
    std::printf("    FAILED %s:%d  %s\n", file, line, what.c_str());
    failures++;
}

} // namespace bbtest

int main()
{
    int failedTests = 0;
    for (const bbtest::Test& t : bbtest::Registry()) {
        int before = bbtest::failures;
        t.run();
        if (bbtest::failures != before) {
            std::printf("FAIL %s\n", t.name);
            failedTests++;
        }
    }
    std::printf("%zu tests, %d failed\n", bbtest::Registry().size(), failedTests);
    return failedTests;
}
