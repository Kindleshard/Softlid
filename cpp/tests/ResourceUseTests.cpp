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

TEST(GpuPercentOfTheBusiestEngineKind)
{
    std::vector<std::pair<std::wstring, double>> engines{
        {L"pid_42_luid_0x0_0xD1A3_phys_0_eng_0_engtype_3D", 3.0},
        {L"pid_42_luid_0x0_0xD1A3_phys_0_eng_1_engtype_3D", 2.5},   // two 3D engines: 5.5 together
        {L"pid_42_luid_0x0_0xD1A3_phys_0_eng_5_engtype_Copy", 4.0},
        {L"pid_420_luid_0x0_0xD1A3_phys_0_eng_0_engtype_3D", 90.0}, // another program (and not "pid_42_")
    };
    CHECK_NEAR(ResourceMeter::GpuPercent(engines, 42), 5.5, 1e-9);
    CHECK_NEAR(ResourceMeter::GpuPercent(engines, 420), 90, 1e-9);
    CHECK_NEAR(ResourceMeter::GpuPercent(engines, 7), -1, 1e-9);   // no engines at all
    CHECK_NEAR(ResourceMeter::GpuPercent({{L"pid_7_eng_0_engtype_3D", 250.0}}, 7), 100, 1e-9);
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
