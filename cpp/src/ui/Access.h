#pragma once
#include <windows.h>
#include <string>

namespace bb::ui {

class Control;
class DirectWindow;
class AccessRoot;

// Screen readers (Narrator, NVDA, JAWS) ask Windows' UI Automation what a window holds. A DirectWindow's controls
// have no windows of their own, so this answers for them: each control that says what it is (Control::Access) is one
// element right under the window, in Tab order, with the patterns its role needs (a button can be pressed, a switch
// turned, a slider set). Nothing is made until a screen reader first asks.
namespace Access {

LRESULT Answer(DirectWindow& window, AccessRoot*& root, WPARAM w, LPARAM l);   // WM_GETOBJECT
void FocusMoved(AccessRoot* root, Control* control);
void Changed(AccessRoot* root, Control* control);   // its value, state or name may have changed
void Gone(AccessRoot* root, Control* control);       // it is being destroyed
void WindowGone(AccessRoot*& root);                  // the window is being destroyed

// A screen reader, if one is running, says this now (a break starting, its next step): for windows that don't take
// the keyboard, so nothing else would be read.
void Announce(HWND window, const std::wstring& text);

} // namespace Access
} // namespace bb::ui
