#pragma once

namespace bb {

// The focus timer's phases: work, then rest, again and again (paused keeps the time left).
enum class Focus { Off, Working, Paused, Resting };

} // namespace bb
