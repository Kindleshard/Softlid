#pragma once

namespace bb {

// Gives memory Softlid no longer needs back to Windows: free blocks at the end of the heap, and the pages of the
// working set (they come back from the page file if used again). For after something big closes - a break, the
// settings window, grain drawn into the compositor.
void TrimMemory();

} // namespace bb
