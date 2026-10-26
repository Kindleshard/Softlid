#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bb {

// What BlinkBuddy itself is using, counted the way Task Manager counts it.
struct ResourceUse {
    double cpu = 0;           // % of the whole processor (all cores), since the last sample
    double gpu = -1;          // % of the graphics card: its busiest kind of engine (3D, copy, video...); -1 unknown
    double privateMb = 0;     // memory only BlinkBuddy uses
    double ramMb = 0;         // in RAM right now (the working set)
    double ramOwnMb = -1;     // of that: BlinkBuddy's own pages (-1 unknown, on older Windows)
    double ramSharedMb = -1;  // and pages shared with other programs (Windows' own libraries, kept in RAM once)
};

// Samples it; meant for about once a second, while something shows it (it costs nothing when not asked).
class ResourceMeter {
public:
    ResourceMeter();
    ~ResourceMeter();
    ResourceMeter(const ResourceMeter&) = delete;
    ResourceMeter& operator=(const ResourceMeter&) = delete;

    ResourceUse Sample();   // (the first CPU and GPU figures are 0: they need two samples)

    // The arithmetic, on its own for testing.
    // CPU time used (100 ns units) over a stretch of wall-clock time on `cpus` logical processors, as a percent.
    static double CpuPercent(uint64_t cpuTime, uint64_t wallTime, int cpus);
    // Windows' GPU engine counters ("pid_1234_luid_..._eng_0_engtype_3D", percent): this process's engines of each
    // kind added up, and the busiest kind; -1 if the process has none.
    static double GpuPercent(const std::vector<std::pair<std::wstring, double>>& engines, DWORD pid);

private:
    void* query = nullptr;     // PDH (the GPU counters); null when Windows has none
    void* counter = nullptr;
    uint64_t lastCpu = 0, lastWall = 0;
    int cpus = 1;
};

} // namespace bb
