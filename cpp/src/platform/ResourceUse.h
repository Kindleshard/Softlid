#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace bb {

// What Softlid itself is using, counted the way Task Manager counts it.
struct ResourceUse {
    double cpu = 0;           // % of the whole processor (all cores), since the last sample
    double gpu = -1;          // % of the graphics card: its busiest engine (3D, copy, video...); -1 unknown
    double privateMb = 0;     // memory only Softlid uses
    double ramMb = 0;         // in RAM right now (the working set)
    double ramOwnMb = -1;     // of that: Softlid's own pages (-1 unknown, on older Windows)
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
    // The busiest graphics engine: each engine's running time for this process (100 ns units, -1 where it didn't
    // answer) before and after a stretch of wall-clock time, as a percent; -1 if no engine answered both times.
    static double GpuPercent(const std::vector<int64_t>& before, const std::vector<int64_t>& after, uint64_t wallTime);

private:
    struct Adapter { LUID luid; unsigned engines; };
    std::vector<int64_t> GpuTimes() const;
    HANDLE self = nullptr;          // this process, with the rights the graphics statistics ask for
    std::vector<Adapter> adapters;  // (none: Windows didn't list any)
    std::vector<int64_t> lastGpu;
    uint64_t lastCpu = 0, lastWall = 0;
    int cpus = 1;
};

} // namespace bb
