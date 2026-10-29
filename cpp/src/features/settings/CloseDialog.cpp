#include "features/settings/CloseDialog.h"
#include "ui/DirectWindow.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"

namespace bb::CloseDialog {
namespace {

using namespace ui;

class Dialog : public DirectWindow {
public:
    Dialog(HWND owner, std::function<void(int, bool)> answer) : owner(owner), answer(std::move(answer))
    {
        Root().Add<Label>(L"Close Softlid?", Fonts::Heading()).SetBounds(20, 16, 380, 28);
        auto& text = Root().Add<Label>(L"In the tray it keeps reminding you to blink and look away, and keeps the screen filter on. "
                                       L"Exiting stops all of that until you start it again.", Fonts::Small(), Theme::Dim);
        text.wrap = text.top = true;
        text.SetBounds(20, 48, 380, 54);
        auto& tray = Root().Add<Button>(L"Keep running in the tray");
        tray.SetBounds(20, 108, 220, 36);
        tray.SetTint(Theme::Accent());
        tray.onClick = [this] { Choose(1); };
        auto& exit = Root().Add<Button>(L"Exit");
        exit.SetBounds(250, 108, 150, 36);
        exit.onClick = [this] { Choose(2); };
        remember = &Root().Add<Toggle>(false);
        remember->SetBounds(20, 157, 44, 22);
        auto& rememberText = Root().Add<Label>(L"Remember my choice (change it in Options)", Fonts::Small(), Theme::Dim);
        rememberText.SetBounds(74, 155, 330, 26);
        auto& area = Root().Add<HotArea>();   // the words switch it too
        area.SetBounds(74, 155, 330, 26);
        area.onClick = [this] { remember->Click(); };
        onClosed = [this] {
            auto done = this->answer;
            int c = choice;
            bool keep = remember->On() && c != 0;
            DeleteLater(this);
            done(c, keep);
        };
    }

protected:
    LRESULT OnMessage(UINT message, WPARAM w, LPARAM l) override
    {
        if (message == WM_CLOSE) EnableWindow(owner, TRUE);   // before it goes, so the owner gets the focus back
        return DirectWindow::OnMessage(message, w, l);
    }

    bool Shortcut(UINT key) override
    {
        if (key != VK_ESCAPE) return false;
        Close();
        return true;
    }

private:
    void Choose(int c)
    {
        choice = c;
        Close();
    }

    HWND owner;
    std::function<void(int, bool)> answer;
    Toggle* remember = nullptr;
    int choice = 0;
};

} // namespace

void Ask(HWND owner, std::function<void(int, bool)> answer)
{
    auto* dialog = new Dialog(owner, std::move(answer));   // (it deletes itself when it closes)
    // Centred on its owner.
    RECT o{};
    GetWindowRect(owner, &o);
    float scale = GetDpiForWindow(owner) / 96.0f;
    POINT at{(o.left + o.right) / 2 - static_cast<LONG>(210 * scale), (o.top + o.bottom) / 2 - static_cast<LONG>(120 * scale)};
    DirectWindow::Options options;
    options.owner = owner;
    options.dialog = true;
    dialog->Open(420, 196, &at, options);
    EnableWindow(owner, FALSE);
    dialog->Show();
}

} // namespace bb::CloseDialog
