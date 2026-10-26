#include "platform/ResourceUse.h"
#include <winternl.h>
#include <d3dkmthk.h>
#include <psapi.h>
#include <algorithm>

namespace bb {
namespace {

uint64_t Ticks(FILETIME f) { return static_cast<uint64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime; }

constexpr double Mb = 1024.0 * 1024.0;

} // namespace

// The graphics card's figures come from the same per-process engine statistics Task Manager's are made from (asked
// of the graphics kernel directly: the performance counters that also give them cost a few MB just to open).
ResourceMeter::ResourceMeter()
{
    cpus = std::max(1, static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    self = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());   // (a pseudo-handle isn't enough)
    D3DKMT_ENUMADAPTERS2 list{};
    if (self && D3DKMTEnumAdapters2(&list) == 0 && list.NumAdapters > 0) {
        std::vector<D3DKMT_ADAPTERINFO> info(list.NumAdapters);
        list.pAdapters = info.data();
        if (D3DKMTEnumAdapters2(&list) == 0)
            for (ULONG i = 0; i < list.NumAdapters; i++) {
                D3DKMT_QUERYSTATISTICS q{};
                q.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
                q.AdapterLuid = info[i].AdapterLuid;
                if (D3DKMTQueryStatistics(&q) == 0) adapters.push_back({info[i].AdapterLuid, q.QueryResult.AdapterInformation.NodeCount});
            }
        for (ULONG i = 0; i < list.NumAdapters; i++) {
            D3DKMT_CLOSEADAPTER close{info[i].hAdapter};
            D3DKMTCloseAdapter(&close);
        }
    }
    Sample();
}

ResourceMeter::~ResourceMeter()
{
    if (self) CloseHandle(self);
}

double ResourceMeter::CpuPercent(uint64_t cpuTime, uint64_t wallTime, int cpus)
{
    if (wallTime == 0 || cpus <= 0) return 0;
    return std::clamp(100.0 * static_cast<double>(cpuTime) / (static_cast<double>(wallTime) * cpus), 0.0, 100.0);
}

double ResourceMeter::GpuPercent(const std::vector<int64_t>& before, const std::vector<int64_t>& after, uint64_t wallTime)
{
    if (wallTime == 0) return -1;
    double busiest = -1;
    for (size_t i = 0; i < before.size() && i < after.size(); i++)
        if (before[i] >= 0 && after[i] >= before[i])
            busiest = std::max(busiest, 100.0 * static_cast<double>(after[i] - before[i]) / static_cast<double>(wallTime));
    return busiest < 0 ? -1 : std::min(busiest, 100.0);
}

// Each engine of each graphics adapter: how long it has run this process's work. An adapter this process has never
// used doesn't answer (-1).
std::vector<int64_t> ResourceMeter::GpuTimes() const
{
    std::vector<int64_t> times;
    for (const Adapter& a : adapters)
        for (unsigned n = 0; n < a.engines; n++) {
            D3DKMT_QUERYSTATISTICS q{};
            q.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE;
            q.AdapterLuid = a.luid;
            q.hProcess = self;
            q.QueryProcessNode.NodeId = n;
            times.push_back(D3DKMTQueryStatistics(&q) == 0 ? q.QueryResult.ProcessNodeInformation.RunningTime.QuadPart : -1);
        }
    return times;
}

ResourceUse ResourceMeter::Sample()
{
    ResourceUse use;
    HANDLE process = GetCurrentProcess();

    FILETIME created, exited, kernel, user, now;
    GetSystemTimeAsFileTime(&now);
    std::vector<int64_t> gpu = GpuTimes();
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        uint64_t cpu = Ticks(kernel) + Ticks(user), wall = Ticks(now);
        if (lastWall) {
            use.cpu = CpuPercent(cpu - lastCpu, wall - lastWall, cpus);
            // (no engine answering: this process hasn't used the graphics card, so nothing on it)
            if (!adapters.empty()) use.gpu = std::max(0.0, GpuPercent(lastGpu, gpu, wall - lastWall));
        }
        lastCpu = cpu;
        lastWall = wall;
    }
    lastGpu = std::move(gpu);

    PROCESS_MEMORY_COUNTERS_EX2 memory{};
    memory.cb = sizeof memory;
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof memory)) {
        use.privateMb = memory.PrivateUsage / Mb;
        use.ramMb = memory.WorkingSetSize / Mb;
        use.ramOwnMb = memory.PrivateWorkingSetSize / Mb;
        use.ramSharedMb = (memory.WorkingSetSize - std::min<SIZE_T>(memory.WorkingSetSize, memory.PrivateWorkingSetSize)) / Mb;
    } else {
        PROCESS_MEMORY_COUNTERS_EX older{};
        older.cb = sizeof older;
        if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&older), sizeof older)) {
            use.privateMb = older.PrivateUsage / Mb;
            use.ramMb = older.WorkingSetSize / Mb;
        }
    }
    return use;
}

} // namespace bb
