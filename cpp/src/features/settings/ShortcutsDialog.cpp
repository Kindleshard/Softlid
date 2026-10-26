#include "features/settings/ShortcutsDialog.h"
#include "features/settings/SettingsHost.h"
#include "core/Hotkeys.h"
#include "platform/Keys.h"
#include "core/Lang.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"

namespace bb::ShortcutsDialog {
namespace {

using namespace ui;

// A box showing an action's keys; focused, it records the next key press (with its modifiers).
class KeyBox : public Control {
public:
    std::function<void(const std::wstring&)> onKeys;   // the keys pressed ("" to remove the shortcut)
    std::wstring shown;

    KeyBox()
    {
        focusable = true;
        cursor = Cursor::Hand;
    }

    void Paint(Canvas& c) override
    {
        D2D1_RECT_F r{0.5f, 0.5f, W() - 0.5f, H() - 0.5f};
        c.FillRound(r, 4, Focused() ? Palette::Argb(38, 79, 112) : Palette::Argb(45, 45, 45));
        c.OutlineRound(r, 4, Focused() ? Theme::Accent() : Theme::ButtonEdge);
        c.Text(shown, Fonts::Body(), {0, 0, W(), H()}, Theme::Fg, HAlign::Center);
    }

    bool Key(UINT vk) override
    {
        if (vk == VK_ESCAPE) {
            Window()->SetFocus(nullptr);
            Invalidate();
            return true;
        }
        if (vk == VK_BACK || vk == VK_DELETE) {
            onKeys(L"");
            return true;
        }
        std::wstring keys = Keys::Describe(vk, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_MENU) < 0, GetKeyState(VK_SHIFT) < 0);
        if (!keys.empty()) onKeys(keys);   // (only modifiers so far: wait for the key)
        return true;
    }
};

class Dialog : public DirectWindow {
public:
    Dialog(SettingsHost& app, HWND owner) : app(app), owner(owner)
    {
        constexpr float rowH = 36, top = 56;
        auto& intro = Root().Add<Label>(L"Click a box and press the keys you want. Backspace removes a shortcut. The reading strip's "
                                        L"movement keys work while it stays put.", Fonts::Small(), Theme::Dim);
        intro.wrap = intro.top = true;
        intro.SetBounds(20, 10, 560, 40);
        for (int id = 1; id <= Hotkeys::Count; id++) {
            float y = top + (id - 1) * rowH;
            Root().Add<Label>(Hotkeys::Names[id - 1]).SetBounds(20, y + 4, 300, 26);
            boxes[id] = &Root().Add<KeyBox>();
            boxes[id]->SetBounds(326, y + 3, 200, 28);
            boxes[id]->onKeys = [this, id](const std::wstring& keys) {
                Hotkeys::Set(this->app.S(), id, keys);
                Changed();
            };
            marks[id] = &Root().Add<Label>(L"In use", Fonts::Small(), Theme::Bad);
            marks[id]->SetBounds(532, y + 6, 64, 22);
        }
        float by = top + static_cast<float>(Hotkeys::Count) * rowH + 14;
        auto& reset = Root().Add<Button>(L"Reset all to default");
        reset.SetBounds(20, by, 200, 34);
        reset.onClick = [this] {
            Hotkeys::ResetAll(this->app.S());
            Changed();
        };
        auto& close = Root().Add<Button>(L"Close");
        close.SetBounds(460, by, 120, 34);
        close.SetTint(Theme::Accent());
        close.onClick = [this] { Close(); };
        onClosed = [this] {
            this->app.RegisterHotkeys();   // back on, as now set
            DeleteLater(this);
        };
        Refresh();
    }

    static constexpr float Width = 600, Height = 56 + static_cast<float>(Hotkeys::Count) * 36 + 64;

protected:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_CLOSE) EnableWindow(owner, TRUE);   // before it goes, so the owner gets the focus back
        return DirectWindow::OnMessage(message, w, l);
    }

private:
    // Saved, tried out (to see what's in use), and shown again.
    void Changed()
    {
        app.Changed();
        app.RegisterHotkeys();
        Refresh();
        app.SuspendHotkeys();
    }

    void Refresh()
    {
        for (int id = 1; id <= Hotkeys::Count; id++) {
            std::wstring keys = Hotkeys::Get(app.S(), id);
            boxes[id]->shown = keys.empty() ? Lang::T(L"None") : Hotkeys::Pretty(keys);
            boxes[id]->Invalidate();
            marks[id]->SetVisible(app.HotkeyFailed(id));
        }
    }

    SettingsHost& app;
    HWND owner;
    KeyBox* boxes[Hotkeys::Count + 1] = {};
    Label* marks[Hotkeys::Count + 1] = {};
};

} // namespace

void Open(SettingsHost& app, HWND owner, bool activate)
{
    auto* dialog = new Dialog(app, owner);   // (it deletes itself when it closes)
    RECT o{};
    GetWindowRect(owner, &o);
    float scale = GetDpiForWindow(owner) / 96.0f;
    POINT at{(o.left + o.right) / 2 - static_cast<LONG>(Dialog::Width / 2 * scale), o.top + static_cast<LONG>(20 * scale)};
    DirectWindow::Options options;
    options.title = L"";
    options.owner = owner;
    options.dialog = true;
    std::wstring title = Lang::T(L"Keyboard shortcuts");
    options.title = title.c_str();
    dialog->Open(Dialog::Width, Dialog::Height, &at, options);
    app.SuspendHotkeys();
    EnableWindow(owner, FALSE);
    dialog->Show(activate);
}

} // namespace bb::ShortcutsDialog
