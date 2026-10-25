#pragma once

namespace bb {

// Diagnostics ("--ui-gallery"): a window showing every control of the toolkit, to see them and try them out.
// With "--offscreen" it opens off the screen without taking focus (for a screenshot by PrintWindow), and closes after
// three minutes. Returns when the window closes.
int RunControlGallery(bool offscreen);

} // namespace bb
