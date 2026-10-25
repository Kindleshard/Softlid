#include "features/settings/AppRuleDialog.h"
#include "core/Lang.h"
#include "platform/FileDialog.h"
#include "platform/ForegroundApp.h"
#include "platform/ProgramInfo.h"
#include "platform/Text.h"
#include "ui/DirectWindow.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/controls/Basic.h"
#include "ui/controls/Choices.h"
#include <algorithm>
#include <cmath>

namespace bb::AppRuleDialog {
namespace {

using namespace ui;
using Running = ForegroundApp::Running;

// One app in the list: its icon, its own name, and under that its file and window title. A click picks it; a double
// click picks it and adds it.
class AppRow : public Container {
public:
    AppRow(const Running& app, int iconPx) : app(app)
    {
        cursor = Cursor::Hand;
        if (HICON icon = ProgramInfo::Icon(app.path, iconPx)) Add<IconImage>(icon).SetBounds(10, 8, 28, 28);
        else Add<Icon>(L'\uE7C4', 14.0f, Theme::Dim).SetBounds(10, 8, 28, 28);
    }

    Running app;
    bool selected = false;
    std::function<void(AppRow&, bool)> onPick;   // (bool: a double click)

    Control* HitTest(D2D1_POINT_2F p) override { return Visible() && Contains(Bounds(), p) ? this : nullptr; }

    void Paint(Canvas& c) override
    {
        if (selected) c.Fill({0, 0, W(), H()}, Mix(Theme::Bg, Theme::Accent(), 0.25f));
        float x = 48, half = H() / 2;
        c.Text(ProgramInfo::Name(app.app, app.path), Fonts::Body(), {x, 2, W() - 10, half + 2}, Theme::Fg);
        std::wstring sub = app.title.empty() ? app.app : app.app + L"  ·  " + app.title;
        c.Text(sub, Fonts::Small(), {x, half, W() - 10, H() - 4}, Theme::Dim);
    }

    void MouseDown(D2D1_POINT_2F) override
    {
        LONG now = GetMessageTime();
        bool twice = now - lastDown <= static_cast<LONG>(GetDoubleClickTime());
        lastDown = now;
        if (onPick) onPick(*this, twice);
    }

private:
    LONG lastDown = -100000;
};

class Dialog : public DirectWindow {
public:
    Dialog(HWND owner, const std::vector<std::wstring>& taken, std::function<void(const Modes::Rule&)> added)
        : owner(owner), added(std::move(added)), taken(taken)
    {
        Root().Add<Label>(L"Choose an app", Fonts::Heading()).SetBounds(20, 12, 400, 28);
        Root().Add<Label>(L"Apps that are open now. Not in the list? Find its program with Browse.", Fonts::Small(), Theme::Dim)
            .SetBounds(20, 40, 400, 20);
        list = &Root().Add<ScrollPage>();
        list->SetBounds(20, 66, 400, 236);
        auto& browse = Root().Add<Button>(L"Browse...");
        browse.SetBounds(20, 310, 120, 32);
        browse.onClick = [this] { Browse(); };

        Root().Add<Label>(L"Mode", Fonts::Heading()).SetBounds(20, 356, 400, 28);
        std::vector<std::wstring> glyphs(Modes::Glyphs, Modes::Glyphs + Modes::Custom), names(Modes::Names, Modes::Names + Modes::Custom);
        modes = &Root().Add<ModeGrid>(glyphs, names);   // every mode but Custom (your own settings: the same as no rule)
        modes->SetBounds(20, 388, 400, 88);
        modes->onChange = [this](int) { Check(); };

        auto& cancel = Root().Add<Button>(L"Cancel");
        cancel.SetBounds(204, 490, 104, 34);
        cancel.onClick = [this] { Close(); };
        add = &Root().Add<Button>(L"Add");
        add->SetBounds(316, 490, 104, 34);
        add->onClick = [this] { if (add->Enabled()) Done(); };
        Check();
        onClosed = [this] { delete this; };
    }

    // The apps open now (listed once the window knows its scale, for the icons).
    void Fill()
    {
        for (const Running& app : ForegroundApp::RunningApps())
            if (std::find(taken.begin(), taken.end(), app.app) == taken.end()) AddApp(app, false);
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
    AppRow& AddApp(const Running& app, bool first)
    {
        auto& row = list->Add<AppRow>(app, static_cast<int>(std::lround(28 * Scale())));
        row.onPick = [this](AppRow& picked, bool twice) {
            Select(&picked);
            if (twice && add->Enabled()) Done();
        };
        if (first) rows.insert(rows.begin(), &row);
        else rows.push_back(&row);
        for (size_t i = 0; i < rows.size(); i++) rows[i]->SetBounds(0, i * 44.0f, list->W() - 8, 44);
        list->SetContentHeight(rows.size() * 44.0f);
        return row;
    }

    void Select(AppRow* row)
    {
        for (AppRow* r : rows) {
            r->selected = r == row;
            r->Invalidate();
        }
        selected = row;
        Check();
    }

    void Check()
    {
        bool ready = selected && modes->Selected() >= 0;
        add->SetEnabled(ready);
        add->SetTint(ready ? Theme::Accent() : 0);   // (accent once it can be used)
    }

    void Done()
    {
        Modes::Rule rule{selected->app.app, modes->Selected(), selected->app.path};
        auto give = added;
        Close();
        give(rule);
    }

    void Browse()
    {
        std::wstring path;
        if (!FileDialog::PickProgram(Handle(), Lang::T(L"Add an app"), Lang::T(L"Programs"), path)) return;
        std::wstring app = Lower(path.substr(path.find_last_of(L'\\') + 1));
        for (AppRow* r : rows)
            if (r->app.app == app) {
                Select(r);
                return;
            }
        Select(&AddApp({app, L"", path}, true));
        list->ScrollTo(0);
    }

    HWND owner;
    std::function<void(const Modes::Rule&)> added;
    std::vector<std::wstring> taken;
    ScrollPage* list = nullptr;
    ModeGrid* modes = nullptr;
    Button* add = nullptr;
    std::vector<AppRow*> rows;
    AppRow* selected = nullptr;
};

} // namespace

void Open(HWND owner, const std::vector<std::wstring>& taken, std::function<void(const Modes::Rule&)> added, bool activate)
{
    auto* dialog = new Dialog(owner, taken, std::move(added));   // (it deletes itself when it closes)
    RECT o{};
    GetWindowRect(owner, &o);
    float scale = GetDpiForWindow(owner) / 96.0f;
    POINT at{(o.left + o.right) / 2 - static_cast<LONG>(220 * scale), o.top + static_cast<LONG>(40 * scale)};
    std::wstring title = Lang::T(L"Add an app");
    DirectWindow::Options options;
    options.title = title.c_str();
    options.owner = owner;
    options.dialog = true;
    dialog->Open(440, 536, &at, options);
    dialog->Fill();
    EnableWindow(owner, FALSE);
    dialog->Show(activate);
}

} // namespace bb::AppRuleDialog
