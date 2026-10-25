#include "core/SessionPlan.h"
#include <algorithm>
#include <numeric>

namespace bb {

SessionPlan::SessionPlan(const std::vector<std::wstring>& keys, const std::wstring& doneKey, double total, double minStep, double leadTime,
                         bool voiced, const std::vector<double>& weight, const LengthOf& lengthOf, double pause)
{
    size_t n = keys.size();
    start.assign(n, 0);
    length.assign(n, 0);
    auto w = [&](size_t i) { return weight.empty() ? 1.0 : weight[i]; };
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += w(i);
    if (voiced) {
        double spoken = lengthOf(doneKey);
        for (size_t i = 0; i < n; i++) spoken += lengthOf(keys[i]) + pause;
        double spare = std::max(0.0, total - leadTime - spoken);
        for (size_t i = 0; i < n; i++) length[i] = lengthOf(keys[i]) + pause + spare * w(i) / sum;
        length[0] += leadTime;
        lead = leadTime;
    } else {
        for (size_t i = 0; i < n; i++) length[i] = std::max(minStep, total * w(i) / sum);
    }
    double t = 0;
    for (size_t i = 0; i < n; i++) {
        start[i] = t;
        t += length[i];
    }
    end = t;
}

int SessionPlan::At(double t, double& into) const
{
    into = 0;
    if (t < 0) return -1;
    for (int i = static_cast<int>(start.size()) - 1; i >= 0; i--)
        if (t >= start[i]) {
            into = t - start[i];
            return i;
        }
    return 0;
}

} // namespace bb
