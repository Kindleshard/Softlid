#pragma once
#include <functional>
#include <string>
#include <vector>

namespace bb {

// When each step of a guided break starts, so every spoken line plays to its end: a step lasts at least its line plus
// a short pause, spare time is shared out evenly (by weight), and the closing line fits in the time too. Without
// voice, the time is simply split by weight (but no step is shorter than minStep).
class SessionPlan {
public:
    using LengthOf = std::function<double(const std::wstring&)>;   // a line's spoken length in seconds

    SessionPlan(const std::vector<std::wstring>& keys, const std::wstring& doneKey, double total, double minStep, double lead,
                bool voiced, const std::vector<double>& weight, const LengthOf& length, double pause);

    std::vector<double> start, length;
    double end = 0;    // when the last step finishes and the closing line (if any) begins
    double lead = 0;   // quiet at the very start, for the opening chime to ring out before the first line

    int At(double t, double& into) const;   // the step running at time t (-1 before the first), and how far into it
};

} // namespace bb
