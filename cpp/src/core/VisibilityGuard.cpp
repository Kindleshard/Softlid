#include "core/VisibilityGuard.h"
#include <algorithm>

namespace bb::VisibilityGuard {

Result Solve(double brightness, const Shown& shown)
{
    constexpr double darkGrey = 0.25;
    auto readable = [&](double b, double e) {
        return shown(b, e, 1) >= LowestBrightness - 1e-4 && shown(b, e, darkGrey) >= LowestBrightness * darkGrey - 1e-4;
    };
    // First, more brightness, a percent at a time.
    for (int step = 0;; step++) {
        double b = std::min(1.0, brightness + step * 0.01);
        if (readable(b, 1)) return {b, 1};
        if (b >= 1) break;
    }
    // Then, at full brightness, a gentler curve.
    for (int step = 1; step <= 20; step++) {
        double ease = 1 - step * 0.05;
        if (readable(1, ease)) return {1, ease};
    }
    return {1, 0};
}

} // namespace bb::VisibilityGuard
