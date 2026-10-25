#include "features/tone/ToneLab.h"
#include "app/App.h"
#include "core/Lang.h"
#include "core/ToneCurve.h"
#include "platform/Files.h"
#include "platform/Text.h"
#include "ui/Theme.h"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>

namespace bb {
namespace {

using namespace ui;

constexpr float LabW = 1180, LabH = 720, X0 = 788, PW = 380;   // client size and the control column, in 96-dpi units
constexpr UINT_PTR ClockTimer = 1;
const wchar_t* const EngineNames[] = {L"Standard", L"Smart (GPU)", L"Smart layer", L"DesktopLUT"};
constexpr int EngineModes[] = {0, 2, 3, 4};
const std::vector<std::wstring> Answers[] = {
    {L"Too bright", L"Comfortable", L"Too dark"},
    {L"Hidden", L"Hard to see", L"Clear"},
    {L"Hard to read", L"Fine", L"Easy"},
    {L"Wrong", L"A bit off", L"Natural"},
    {L"Bad", L"OK", L"Good", L"Great"},
};
const wchar_t* const Questions[] = {L"White areas", L"Dark details (grey steps, dark panel)", L"Text", L"Colours", L"Overall"};
const wchar_t* const AnswerKeys[] = {L"whites", L"dark", L"text", L"colours", L"overall"};

// A button that does something while it is held down.
class HoldButton : public Button {
public:
    using Button::Button;
    std::function<void(bool)> onHold;
    void MouseDown(D2D1_POINT_2F p) override
    {
        Button::MouseDown(p);
        if (onHold) onHold(true);
    }
    void MouseUp(D2D1_POINT_2F p) override
    {
        Button::MouseUp(p);
        if (onHold) onHold(false);
    }
};

// The picture the lab is judged on: a bright document, a dark editor, grey steps, colour chips and a photo-like
// scene, each with the details a tone curve tends to lose (near-white and near-black steps, dim text). Drawn in its
// own 760-wide units, scaled to the box.
class TestScene : public Control {
public:
    TestScene()
    {
        titleFont = gfx::Font(15, gfx::Weight::Semibold);   // (20, 13, 11 and 12 pixels)
        bodyFont = gfx::Font(9.75f);
        fine = gfx::Font(8.25f);
        codeFont = gfx::Font(9, gfx::Weight::Normal, L"Consolas");
        for (auto* f : {titleFont.Get(), bodyFont.Get(), fine.Get(), codeFont.Get()}) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    void Paint(Canvas& c) override
    {
        ID2D1RenderTarget* t = c.Target();
        float k = W() / 760;
        D2D1_MATRIX_3X2_F before;
        t->GetTransform(&before);
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * before);
        c.Fill(Rect(0, 0, 760, 696), Palette::Argb(43, 43, 43));
        Document(c);
        Editor(c);
        Ramp(c);
        Chips(c);
        Photo(c);
        t->SetTransform(before);
    }

private:
    static int Grey(int v) { return Palette::Argb(v, v, v); }
    static int Rgb(int rgb) { return static_cast<int>(0xFF000000u | static_cast<unsigned>(rgb)); }

    void Write(Canvas& c, std::wstring_view text, IDWriteTextFormat* f, int argb, float x, float y, float w = 600, float h = 24, bool wrap = false)
    {
        c.Text(text, f, Rect(x, y, w, h), argb, HAlign::Left, wrap, true);
    }

    void Document(Canvas& c)
    {
        c.Fill(Rect(0, 0, 470, 330), Rgb(0xFFFFFF));
        Write(c, L"Quarterly notes", titleFont.Get(), Rgb(0x111111), 20, 14, 430, 30);
        bodyFont->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        Write(c, L"Bright pages like this one are where a tone curve matters most. The page should feel calm rather than glaring, "
                 L"while the text stays crisp and easy to follow from line to line.", bodyFont.Get(), Rgb(0x222222), 20, 48, 430, 70, true);
        bodyFont->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        Write(c, L"Secondary text in grey  ·  updated 3 minutes ago", fine.Get(), Rgb(0x6B6B6B), 20, 124);
        const wchar_t* linkText = L"A blue link, to check colour and contrast";
        Write(c, linkText, bodyFont.Get(), Rgb(0x0B66D0), 20, 144);
        c.Line(20, 161, 20 + TextWidth(linkText, bodyFont.Get()), 161, Rgb(0x0B66D0), 1, false);   // (underlined)
        const wchar_t* rows[4][2] = {{L"Screen time", L"6 h 40 min"}, {L"Breaks taken", L"14 of 18"}, {L"Blink reminders", L"212"},
                                     {L"Night mode", L"21:00 to 07:00"}};
        for (int i = 0; i < 4; i++) {
            float y = 172.0f + i * 24;
            c.Fill(Rect(20, y, 430, 24), i % 2 == 0 ? Rgb(0xFFFFFF) : Rgb(0xF3F3F3));
            c.Line(20, y + 24, 450, y + 24, Grey(227), 1, false);
            Write(c, rows[i][0], bodyFont.Get(), Rgb(0x333333), 26, y + 3);
            Write(c, rows[i][1], bodyFont.Get(), Rgb(0x333333), 300, y + 3);
        }
        Write(c, L"Near-white steps", fine.Get(), Rgb(0x6B6B6B), 20, 276);
        const int whites[] = {255, 247, 238, 228, 216, 200};
        for (int i = 0; i < 6; i++) {
            D2D1_RECT_F r = Rect(20.0f + i * 72, 294, 68, 28);
            c.Fill(r, Grey(whites[i]));
            c.Target()->DrawRectangle(Inset(r, 0.5f, 0.5f), c.Brush(Grey(200)));
            Write(c, std::to_wstring(whites[i]), fine.Get(), Rgb(0x555555), 26.0f + i * 72, 300);
        }
    }

    void Editor(Canvas& c)
    {
        c.Fill(Rect(480, 0, 280, 330), Rgb(0x1E1E1E));
        c.Fill(Rect(480, 0, 280, 24), Rgb(0x2D2D2D));
        Write(c, L"reminders.cs", fine.Get(), Rgb(0xCCCCCC), 490, 5);
        struct Line { const wchar_t* text; int rgb; };
        const Line lines[] = {
            {L"// dim green comment", 0x6A9955}, {L"void Blink(int seconds)", 0x569CD6}, {L"{", 0xD4D4D4},
            {L"    var text = \"look away\";", 0xCE9178}, {L"    Remind(text, 20);", 0xDCDCAA}, {L"    count += 1;", 0x9CDCFE},
            {L"}", 0xD4D4D4}, {L"// faint text, hardest to see", 0x4A4A4A},
        };
        for (int i = 0; i < 8; i++) {
            float y = 34.0f + i * 22;
            Write(c, std::to_wstring(i + 1), codeFont.Get(), Rgb(0x5A5A5A), 488, y, 24);
            Write(c, lines[i].text, codeFont.Get(), Rgb(lines[i].rgb), 512, y, 240);
        }
        Write(c, L"Near-black steps", fine.Get(), Rgb(0x8A8A8A), 490, 250);
        const int dark[] = {0, 10, 20, 31, 42, 54};
        for (int i = 0; i < 6; i++) {
            c.Fill(Rect(490.0f + i * 44, 270, 40, 32), Grey(dark[i]));
            Write(c, std::to_wstring(dark[i]), fine.Get(), Rgb(0x8A8A8A), 494.0f + i * 44, 306, 40);
        }
    }

    void Ramp(Canvas& c)
    {
        Write(c, L"Grey steps, black to white", fine.Get(), Rgb(0x9A9A9A), 0, 338);
        float w = 760.0f / 17;
        for (int i = 0; i <= 16; i++) {
            int v = std::min(255, i * 16);
            c.Fill(Rect(i * w, 356, w + 1, 36), Grey(v));
            Write(c, std::to_wstring(v), fine.Get(), Rgb(0x9A9A9A), i * w + 4, 394, w);
        }
    }

    void Chips(Canvas& c)
    {
        Write(c, L"Strong colours, then soft colours, skin tones and dark colours", fine.Get(), Rgb(0x9A9A9A), 0, 414);
        const int strong[] = {0xFF0000, 0x00C800, 0x0050FF, 0xFFE600, 0x00E5FF, 0xFF00D4, 0xFF8A00, 0x8A2BE2};
        const int soft[] = {0xF8BBD0, 0xBBDEFB, 0xC8E6C9, 0xE1BEE7, 0xF1C27D, 0xE0AC69, 0x8D5524, 0x1A237E, 0x5D1010, 0x1B4D20};
        for (int i = 0; i < 8; i++) c.Fill(Rect(i * 95.0f, 432, 91, 34), Rgb(strong[i]));
        for (int i = 0; i < 10; i++) c.Fill(Rect(i * 76.0f, 470, 72, 34), Rgb(soft[i]));
    }

    void Photo(Canvas& c)
    {
        ID2D1RenderTarget* t = c.Target();
        auto gradient = [t](D2D1_POINT_2F a, D2D1_POINT_2F b, int from, int to) {
            D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(from)}, {1, gfx::Color(to)}};
            gfx::ComPtr<ID2D1GradientStopCollection> collection;
            gfx::ComPtr<ID2D1LinearGradientBrush> brush;
            t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
            t->CreateLinearGradientBrush({a, b}, collection.Get(), brush.GetAddressOf());
            return brush;
        };
        t->FillRectangle(Rect(0, 514, 760, 182), gradient({0, 514}, {0, 696}, Palette::Argb(46, 111, 216), Palette::Argb(246, 211, 101)).Get());
        {   // the sun: white in the middle, fading out
            D2D1_GRADIENT_STOP stops[] = {{0, gfx::Color(Rgb(0xFFFFFF))}, {1, gfx::Color(Palette::Argb(255, 240, 180), 0)}};
            gfx::ComPtr<ID2D1GradientStopCollection> collection;
            gfx::ComPtr<ID2D1RadialGradientBrush> sun;
            t->CreateGradientStopCollection(stops, 2, collection.GetAddressOf());
            t->CreateRadialGradientBrush({{605, 579}, {0, 0}, 45, 45}, collection.Get(), sun.GetAddressOf());
            t->FillEllipse({{605, 579}, 45, 45}, sun.Get());
        }
        gfx::ComPtr<ID2D1PathGeometry> hills;
        gfx::D2D()->CreatePathGeometry(hills.GetAddressOf());
        gfx::ComPtr<ID2D1GeometrySink> sink;
        hills->Open(sink.GetAddressOf());
        const D2D1_POINT_2F points[] = {{140, 600}, {300, 630}, {470, 590}, {640, 625}, {760, 605}, {760, 696}, {0, 696}};
        sink->BeginFigure({0, 640}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(points, 7);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        t->FillGeometry(hills.Get(), gradient({0, 590}, {0, 696}, Palette::Argb(46, 94, 52), Palette::Argb(10, 26, 12)).Get());
        c.Fill(Rect(110, 610, 8, 70), Rgb(0x050505));   // a tree
        t->FillEllipse({{114, 598}, 26, 26}, c.Brush(Grey(12)));
        Write(c, L"Photo-like scene: sun highlight, sky gradient, dark ground", fine.Get(), Rgb(0xFFFFFF), 8, 518);
    }

    gfx::ComPtr<IDWriteTextFormat> titleFont, bodyFont, fine, codeFont;
};

} // namespace

std::wstring ToneLab::FeedbackFile() { return RoamingAppData() + L"\\BlinkBuddy\\tone-feedback.txt"; }

ToneLab::ToneLab(App& app) : app(app)
{
    const Settings& s = app.S();
    Container& area = Root();
    area.Add<TestScene>().SetBounds(12, 12, 760, 696);

    area.Add<Label>(L"Tone test lab", Fonts::Heading()).SetBounds(X0, 10, PW, 28);
    auto& hint = area.Add<Label>(L"Rate the picture on the left for each engine and strength. Hold the button below to compare with no "
                                 L"tone. Your own settings come back when you close this window.", Fonts::Small(), Theme::Dim);
    hint.wrap = hint.top = true;
    hint.SetBounds(X0, 40, PW, 52);

    area.Add<Label>(L"Engine", Fonts::Small(), Theme::Dim).SetBounds(X0, 96, PW, 18);
    engine = &area.Add<Segmented>(std::vector<std::wstring>{L"Standard", L"Smart GPU", L"Layer", L"DesktopLUT"});
    engine->SetBounds(X0, 116, PW, 32);
    auto mode = std::find(std::begin(EngineModes), std::end(EngineModes), s.ToneMode);
    engine->SetSelected(mode == std::end(EngineModes) ? 0 : static_cast<int>(mode - std::begin(EngineModes)));
    area.Add<Label>(L"Strength", Fonts::Small(), Theme::Dim).SetBounds(X0, 154, PW, 18);
    level = &area.Add<Segmented>(std::vector<std::wstring>(ToneCurve::Levels, ToneCurve::Levels + 4));
    level->SetBounds(X0, 174, PW, 32);
    level->SetSelected(s.Level());

    area.Add<Label>(L"Brightness", Fonts::Small(), Theme::Dim).SetBounds(X0, 212, 200, 18);
    brightText = &area.Add<Label>(L"", Fonts::Small(), Theme::Dim);
    brightText->SetPlainText(std::to_wstring(s.Brightness) + L"%");
    brightText->align = HAlign::Right;
    brightText->SetBounds(X0 + PW - 80, 212, 80, 18);
    auto& bright = area.Add<Slider>(20, 100, s.Brightness);
    bright.SetBounds(X0 - 8, 230, PW + 16, 30);
    bright.onChange = [this](int v) {
        this->app.S().Brightness = v;
        brightText->SetPlainText(std::to_wstring(v) + L"%");
        this->app.Changed(true);
    };

    auto& compare = area.Add<HoldButton>(L"Hold to see it without tone");
    compare.SetBounds(X0, 266, PW, 32);
    compare.onHold = [this](bool held) { this->app.LabCompare(held); };

    progress = &area.Add<Label>(L"", Fonts::Small(), Theme::Dim);
    progress->wrap = progress->top = true;
    progress->SetBounds(X0, 302, PW, 52);

    for (int i = 0; i < 5; i++) {
        float y = 358.0f + i * 52;
        area.Add<Label>(Questions[i], Fonts::Small(), Theme::Fg).SetBounds(X0, y, PW, 18);
        answers[i] = &area.Add<Segmented>(Answers[i]);
        answers[i]->SetBounds(X0, y + 19, PW, 28);
        answers[i]->SetSelected(-1);   // nothing chosen yet
    }
    note = &area.Add<TextBox>(200);
    note->cue = Lang::T(L"Anything else? (optional)");
    note->SetBounds(X0, 622, PW, 26);
    note->onKey = [this](UINT key) {
        if (key == VK_RETURN) Save();
    };
    auto& save = area.Add<Button>(L"Save and next");
    save.SetBounds(X0, 658, 170, 34);
    save.SetTint(Theme::Accent());
    save.onClick = [this] { Save(); };
    auto& skip = area.Add<Button>(L"Skip");
    skip.SetBounds(X0 + 180, 658, 80, 34);
    skip.onClick = [this] { Next(); };
    auto& open = area.Add<Button>(L"Open answers");
    open.SetBounds(X0 + 270, 658, 110, 34);
    open.onClick = [] {
        std::wstring file = FeedbackFile();
        if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    engine->onChange = [this](int) { ShowCombo(engine->Selected(), level->Selected()); };
    level->onChange = [this](int) { ShowCombo(engine->Selected(), level->Selected()); };
}

void ToneLab::Start(HICON icon)
{
    Options opts;
    opts.title = L"BlinkBuddy tone lab";
    opts.icon = icon;
    Open(LabW, LabH, nullptr, opts);
    SetTimer(Handle(), ClockTimer, 1000, nullptr);
    ShowCombo(engine->Selected(), level->Selected());
    Show(true);
}

bool ToneLab::Shortcut(UINT key)
{
    if (key == VK_RETURN) Save();
    else if (key == VK_ESCAPE) Close();
    else return false;
    return true;
}

LRESULT ToneLab::OnMessage(UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_TIMER && w == ClockTimer) {
        UpdateProgress(L"");
        return 0;
    }
    return DirectWindow::OnMessage(message, w, l);
}

void ToneLab::ShowCombo(int e, int l)
{
    if (switching) return;
    switching = true;
    engine->SetSelected(e);
    level->SetSelected(l);
    switching = false;
    app.S().ComfortLevel = l;
    if (app.S().ToneMode != EngineModes[e]) app.SetToneMode(EngineModes[e]);
    else app.Changed(true);
    for (auto* a : answers) a->SetSelected(-1);
    note->SetText(L"");
    UpdateProgress(L"");
}

void ToneLab::UpdateProgress(const std::wstring& message)
{
    int done = static_cast<int>(std::count(std::begin(rated), std::end(rated), true));
    std::wstring state = Lang::T(EngineNames[engine->Selected()]) + L", " + Lang::T(ToneCurve::Levels[level->Selected()]) +
                         (rated[Index()] ? Lang::T(L" (rated)") : L"") + L". " +
                         Lang::F(L"Rated {0} of 16.", {std::to_wstring(done)});
    if (EngineModes[engine->Selected()] == 4 && !app.LutStatus().empty()) state += L"\n" + app.LutStatus();
    if (!app.GuardNote().empty()) state += L"\n" + app.GuardNote();
    progress->SetPlainText(message.empty() ? state : Lang::T(message) + L"\n" + state);
}

void ToneLab::Save()
{
    for (auto* a : answers)
        if (a->Selected() < 0) {
            UpdateProgress(L"Answer all five first.");
            return;
        }
    const Settings& s = app.S();
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    std::wstring line = std::wstring(stamp) + L" | " + EngineNames[engine->Selected()] + L" | " + ToneCurve::Levels[level->Selected()] +
                        L" | brightness " + std::to_wstring(s.Brightness) +
                        (s.HardwareBrightness ? L"%, backlight " + std::to_wstring(s.Backlight) + L"%" : L"%") +
                        L" | blue filter " + std::to_wstring(s.BlueFilter) + L"%";
    for (int i = 0; i < 5; i++) line += L" | " + std::wstring(AnswerKeys[i]) + L"=" + Answers[i][answers[i]->Selected()];
    if (!app.GuardNote().empty()) line += L" | " + app.GuardNote();
    std::wstring text = Trim(note->Text());
    std::replace(text.begin(), text.end(), L'|', L'/');
    if (!text.empty()) line += L" | note: " + text;
    line += L"\r\n";

    std::wstring file = FeedbackFile();
    SHCreateDirectoryExW(nullptr, file.substr(0, file.find_last_of(L'\\')).c_str(), nullptr);
    HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::string bytes = ToUtf8(line);
    DWORD written = 0;
    bool ok = h != INVALID_HANDLE_VALUE && WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    DWORD error = GetLastError();
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!ok) {
        UpdateProgress(Lang::F(L"Couldn't save (error {0}).", {std::to_wstring(error)}));
        return;
    }
    rated[Index()] = true;
    Next();
}

// The next combination not rated yet, engine by engine.
void ToneLab::Next()
{
    for (int step = 1; step <= 16; step++) {
        int i = (Index() + step) % 16;
        if (rated[i]) continue;
        ShowCombo(i / 4, i % 4);
        return;
    }
    UpdateProgress(L"All 16 rated, thank you. Tell Claude you're done.");
}

} // namespace bb
