#include "platform/ResourceUse.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <algorithm>
#include <map>

#pragma comment(lib, "pdh.lib")

namespace bb {
namespace {

uint64_t Ticks(FILETIME f) { return static_cast<uint64_t>(f.dwHighDateTime) << 32 | f.dwLowDateTime; }

constexpr double Mb = 1024.0 * 1024.0;

} // namespace

ResourceMeter::ResourceMeter()
{
    cpus = std::max(1, static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    // Only this process's engines ("pid_1234_..."): far fewer than every program's.
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER c = nullptr;
    std::wstring path = L"\\GPU Engine(pid_" + std::to_wstring(GetCurrentProcessId()) + L"_*)\\Utilization Percentage";
    if (PdhOpenQueryW(nullptr, 0, &q) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterW(q, path.c_str(), 0, &c) == ERROR_SUCCESS) {
            query = q;
            counter = c;
            PdhCollectQueryData(q);   // (a rate needs a first sample)
        } else {
            PdhCloseQuery(q);
        }
    }
    Sample();
}

ResourceMeter::~ResourceMeter()
{
    if (query) PdhCloseQuery(static_cast<PDH_HQUERY>(query));
}

double ResourceMeter::CpuPercent(uint64_t cpuTime, uint64_t wallTime, int cpus)
{
    if (wallTime == 0 || cpus <= 0) return 0;
    return std::clamp(100.0 * static_cast<double>(cpuTime) / (static_cast<double>(wallTime) * cpus), 0.0, 100.0);
}

double ResourceMeter::GpuPercent(const std::vector<std::pair<std::wstring, double>>& engines, DWORD pid)
{
    std::wstring mine = L"pid_" + std::to_wstring(pid) + L"_";
    std::map<std::wstring, double> byKind;
    bool any = false;
    for (const auto& [name, value] : engines) {
        if (name.rfind(mine, 0) != 0) continue;
        any = true;
        size_t kind = name.find(L"engtype_");
        byKind[kind == std::wstring::npos ? L"" : name.substr(kind + 8)] += value;
    }
    if (!any) return -1;
    double busiest = 0;
    for (const auto& [kind, value] : byKind) busiest = std::max(busiest, value);
    return std::clamp(busiest, 0.0, 100.0);
}

ResourceUse ResourceMeter::Sample()
{
    ResourceUse use;
    HANDLE self = GetCurrentProcess();

    FILETIME created, exited, kernel, user, now;
    GetSystemTimeAsFileTime(&now);
    if (GetProcessTimes(self, &created, &exited, &kernel, &user)) {
        uint64_t cpu = Ticks(kernel) + Ticks(user), wall = Ticks(now);
        if (lastWall) use.cpu = CpuPercent(cpu - lastCpu, wall - lastWall, cpus);
        lastCpu = cpu;
        lastWall = wall;
    }

    if (query && PdhCollectQueryData(static_cast<PDH_HQUERY>(query)) == ERROR_SUCCESS) {
        DWORD size = 0, count = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE, &size, &count, nullptr);
        std::vector<std::pair<std::wstring, double>> engines;
        if (status == static_cast<PDH_STATUS>(PDH_MORE_DATA) && size > 0) {
            std::vector<BYTE> buffer(size);
            auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(counter), PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS)
                for (DWORD i = 0; i < count; i++)
                    if (items[i].FmtValue.CStatus == ERROR_SUCCESS) engines.emplace_back(items[i].szName, items[i].FmtValue.doubleValue);
        }
        double gpu = GpuPercent(engines, GetCurrentProcessId());
        use.gpu = gpu < 0 ? 0 : gpu;   // (no engines in use: nothing on the graphics card)
    }

    PROCESS_MEMORY_COUNTERS_EX2 memory{};
    memory.cb = sizeof memory;
    if (GetProcessMemoryInfo(self, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof memory)) {
        use.privateMb = memory.PrivateUsage / Mb;
        use.ramMb = memory.WorkingSetSize / Mb;
        use.ramOwnMb = memory.PrivateWorkingSetSize / Mb;
        use.ramSharedMb = (memory.WorkingSetSize - std::min<SIZE_T>(memory.WorkingSetSize, memory.PrivateWorkingSetSize)) / Mb;
    } else {
        PROCESS_MEMORY_COUNTERS_EX older{};
        older.cb = sizeof older;
        if (GetProcessMemoryInfo(self, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&older), sizeof older)) {
            use.privateMb = older.PrivateUsage / Mb;
            use.ramMb = older.WorkingSetSize / Mb;
        }
    }
    return use;
}

} // namespace bb
