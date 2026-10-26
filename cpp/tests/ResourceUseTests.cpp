// The resource meter: its CPU and GPU arithmetic, and a real sample of this test program.
#include "Check.h"
#include "platform/ResourceUse.h"
#include <chrono>

using bb::ResourceMeter;

TEST(CpuPercentOfTheWholeProcessor)
{
    const uint64_t second = 10'000'000;   // 100 ns units
    CHECK_NEAR(ResourceMeter::CpuPercent(second, second, 4), 25, 1e-9);      // one core busy of four
    CHECK_NEAR(ResourceMeter::CpuPercent(second / 100, second, 1), 1, 1e-9);
    CHECK_NEAR(ResourceMeter::CpuPercent(5 * second, second, 4), 100, 1e-9); // (rounding between samples: held to 100)
    CHECK_NEAR(ResourceMeter::CpuPercent(second, 0, 4), 0, 1e-9);            // no time passed
}

TEST(GpuPercentOfTheBusiestEngine)
{
    const int64_t second = 10'000'000;   // 100 ns units
    std::vector<int64_t> before{100, 5'000, -1, 0}, after{100 + 200'000, 5'000 + 500'000, -1, 0};
    CHECK_NEAR(ResourceMeter::GpuPercent(before, after, second), 5, 1e-9);           // 2% and 5%: the busiest
    CHECK_NEAR(ResourceMeter::GpuPercent({0}, {0}, second), 0, 1e-9);               // answered, idle
    CHECK_NEAR(ResourceMeter::GpuPercent({-1, -1}, {-1, -1}, second), -1, 1e-9);    // never used the graphics card
    CHECK_NEAR(ResourceMeter::GpuPercent({}, {0}, second), -1, 1e-9);               // the first sample
    CHECK_NEAR(ResourceMeter::GpuPercent({0}, {3 * second}, second), 100, 1e-9);    // (rounding between samples)
    CHECK_NEAR(ResourceMeter::GpuPercent({0}, {second}, 0), -1, 1e-9);              // no time passed
}

TEST(ResourceSampleOfThisProgram)
{
    ResourceMeter meter;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    volatile double sink = 0;
    while (std::chrono::steady_clock::now() < until) sink = sink + 1.0;   // something for the CPU to do
    bb::ResourceUse use = meter.Sample();
    CHECK(use.cpu > 0 && use.cpu <= 100);
    CHECK(use.gpu >= 0 || use.gpu == -1);
    CHECK(use.privateMb > 0);
    CHECK(use.ramMb > 0);
    if (use.ramOwnMb >= 0) CHECK_NEAR(use.ramOwnMb + use.ramSharedMb, use.ramMb, 1e-6);
}
