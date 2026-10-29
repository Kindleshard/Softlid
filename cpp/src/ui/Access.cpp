#include "ui/Access.h"
#include "ui/DirectWindow.h"
#include "ui/controls/Basic.h"
#include <uiautomation.h>
#include <algorithm>
#include <map>
#include <vector>

#pragma comment(lib, "uiautomationcore.lib")

namespace bb::ui {
namespace {

// The controls a screen reader sees, in Tab order: shown, and saying what they are.
void Collect(Control* c, std::vector<Control*>& out)
{
    if (!c->Visible()) return;
    if (c->Access().role != Role::None && c->W() > 0 && c->H() > 0) out.push_back(c);
    if (auto box = dynamic_cast<Container*>(c))
        for (auto& child : box->Children()) Collect(child.get(), out);
}

// A control without a name of its own is named by the first label in its row ("Start with Windows" for its switch),
// or its tip, or else the last label before it (a slider in the row under its title).
std::wstring NameOf(const Control* c, const AccessInfo& info)
{
    if (!info.name.empty()) return info.name;
    if (Container* row = c->Parent())
        for (auto& sibling : row->Children())
            if (auto label = dynamic_cast<const Label*>(sibling.get()); label && label->Visible() && !label->Text().empty())
                return label->Text();
    if (!c->tip.empty()) return c->tip;
    DirectWindow* w = c->Window();
    if (!w) return L"";
    std::vector<Control*> list;
    Collect(&w->Root(), list);
    std::wstring before;
    for (Control* other : list) {
        if (other == c) return before;
        if (auto label = dynamic_cast<const Label*>(other)) before = label->Text();
    }
    return L"";
}

VARIANT Text(const std::wstring& s)
{
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(s.c_str());
    return v;
}

VARIANT Number(int n)
{
    VARIANT v;
    v.vt = VT_I4;
    v.lVal = n;
    return v;
}

VARIANT Bool(bool b)
{
    VARIANT v;
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
}

VARIANT Double(double d)
{
    VARIANT v;
    v.vt = VT_R8;
    v.dblVal = d;
    return v;
}

int ControlType(Role role)
{
    switch (role) {
    case Role::Text: return UIA_TextControlTypeId;
    case Role::Button: return UIA_ButtonControlTypeId;
    case Role::Link: return UIA_HyperlinkControlTypeId;
    case Role::Switch: return UIA_CheckBoxControlTypeId;
    case Role::Slider: return UIA_SliderControlTypeId;
    case Role::Spinner: return UIA_SpinnerControlTypeId;
    case Role::Choice: return UIA_ComboBoxControlTypeId;
    case Role::Radio: return UIA_RadioButtonControlTypeId;
    default: return UIA_CustomControlTypeId;
    }
}

} // namespace

class AccessElement;

// The window itself: UI Automation gets its name and place from the window (the host provider); this adds the
// controls as its children.
class AccessRoot : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot {
public:
    explicit AccessRoot(DirectWindow& window) : window(&window) {}

    DirectWindow* window;   // null once the window is gone
    std::map<Control*, AccessElement*> elements;
    int nextId = 1;

    AccessElement* ElementFor(Control* c);
    std::vector<Control*> List() const
    {
        std::vector<Control*> out;
        if (window) Collect(&window->Root(), out);
        return out;
    }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = InterlockedDecrement(&refs);
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (id == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (id == __uuidof(IRawElementProviderFragmentRoot)) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override
    {
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override
    {
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID, VARIANT* out) override
    {
        out->vt = VT_EMPTY;   // (the window's own name and type come from the host provider)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override
    {
        *out = nullptr;
        return window ? UiaHostProviderFromHwnd(window->Handle(), out) : UIA_E_ELEMENTNOTAVAILABLE;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override
    {
        *out = nullptr;   // (the host provider's)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override
    {
        *out = {};   // (the host provider's)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override
    {
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override
    {
        *out = this;
        AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

private:
    ~AccessRoot() = default;
    LONG refs = 1;
};

// One control. It keeps no state of its own: every question is answered from the control as it is now.
class AccessElement : public IRawElementProviderSimple, public IRawElementProviderFragment, public IInvokeProvider,
                      public IToggleProvider, public IRangeValueProvider, public IValueProvider,
                      public IExpandCollapseProvider, public ISelectionItemProvider {
public:
    AccessElement(AccessRoot* root, Control* control, int id) : root(root), control(control), id(id) { root->AddRef(); }

    AccessRoot* root;
    Control* control;   // null once it is gone
    int id;

    HRESULT Alive() const { return control && root->window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE; }
    AccessInfo Info() const { return control->Access(); }

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = InterlockedDecrement(&refs);
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else if (iid == __uuidof(IExpandCollapseProvider)) *out = static_cast<IExpandCollapseProvider*>(this);
        else if (iid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override
    {
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override
    {
        *out = nullptr;
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        AccessInfo info = Info();
        bool has = false;
        switch (pattern) {
        case UIA_InvokePatternId: has = info.role == Role::Button || info.role == Role::Link; break;
        case UIA_TogglePatternId: has = info.role == Role::Switch; break;
        case UIA_RangeValuePatternId: has = info.role == Role::Slider || info.role == Role::Spinner; break;
        case UIA_ValuePatternId: has = info.role == Role::Choice || !info.value.empty(); break;
        case UIA_ExpandCollapsePatternId: has = info.expanded >= 0; break;
        case UIA_SelectionItemPatternId: has = info.role == Role::Radio; break;
        }
        if (has) QueryInterface(__uuidof(IUnknown), reinterpret_cast<void**>(out));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override
    {
        out->vt = VT_EMPTY;
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        AccessInfo info = Info();
        switch (property) {
        case UIA_ControlTypePropertyId: *out = Number(ControlType(info.role)); break;
        case UIA_NamePropertyId: *out = Text(NameOf(control, info)); break;
        case UIA_HelpTextPropertyId:
            if (!control->tip.empty() && control->tip != NameOf(control, info)) *out = Text(control->tip);
            break;
        case UIA_IsKeyboardFocusablePropertyId: *out = Bool(control->focusable); break;
        case UIA_HasKeyboardFocusPropertyId: *out = Bool(control->Focused()); break;
        case UIA_IsEnabledPropertyId: *out = Bool(control->Enabled()); break;
        case UIA_IsOffscreenPropertyId: {
            D2D1_RECT_F r = control->VisibleRect();
            *out = Bool(r.right <= r.left || r.bottom <= r.top);
            break;
        }
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId: *out = Bool(true); break;
        case UIA_ProviderDescriptionPropertyId: *out = Text(L"Softlid control"); break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override
    {
        *out = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override
    {
        *out = nullptr;
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent) return root->QueryInterface(__uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(out));
        if (direction != NavigateDirection_NextSibling && direction != NavigateDirection_PreviousSibling) return S_OK;
        std::vector<Control*> list = root->List();
        auto at = std::find(list.begin(), list.end(), control);
        if (at == list.end()) return S_OK;
        if (direction == NavigateDirection_NextSibling && at + 1 != list.end()) *out = Fragment(*(at + 1));
        if (direction == NavigateDirection_PreviousSibling && at != list.begin()) *out = Fragment(*(at - 1));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override
    {
        int parts[] = {UiaAppendRuntimeId, id};
        *out = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!*out) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; i++) SafeArrayPutElement(*out, &i, &parts[i]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override
    {
        *out = {};
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        RECT px = root->window->ToPixels(control->VisibleRect());
        if (px.right <= px.left || px.bottom <= px.top) return S_OK;
        MapWindowPoints(root->window->Handle(), nullptr, reinterpret_cast<POINT*>(&px), 2);
        *out = {static_cast<double>(px.left), static_cast<double>(px.top), static_cast<double>(px.right - px.left),
                static_cast<double>(px.bottom - px.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override
    {
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        if (control->focusable) control->Focus();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override
    {
        *out = root;
        root->AddRef();
        return S_OK;
    }

    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke() override { return Act(); }

    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override { return Act(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Info().on ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    // IRangeValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(double v) override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        control->AccessSet(v);
        Access::Changed(root, control);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return Get(out, &AccessInfo::now); }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Info().role == Role::Choice || !control->Enabled();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return Get(out, &AccessInfo::max); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return Get(out, &AccessInfo::min); }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override
    {
        HRESULT hr = Get(out, &AccessInfo::step);
        *out *= 10;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override { return Get(out, &AccessInfo::step); }

    // IValueProvider (the words for what it is set to; read-only)
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override
    {
        *out = nullptr;
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = SysAllocString(Info().value.c_str());
        return S_OK;
    }

    // IExpandCollapseProvider
    HRESULT STDMETHODCALLTYPE Expand() override { return Info().expanded == 0 ? Act() : S_OK; }
    HRESULT STDMETHODCALLTYPE Collapse() override { return Info().expanded == 1 ? Act() : S_OK; }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* out) override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Info().expanded == 1 ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }

    // ISelectionItemProvider (a radio row)
    HRESULT STDMETHODCALLTYPE Select() override { return Info().on ? S_OK : Act(); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Info().on;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override
    {
        *out = nullptr;
        return S_OK;
    }

private:
    ~AccessElement() { root->Release(); }

    IRawElementProviderFragment* Fragment(Control* c)
    {
        AccessElement* e = root->ElementFor(c);
        e->AddRef();
        return e;
    }
    HRESULT Act()
    {
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!control->Enabled()) return UIA_E_ELEMENTNOTENABLED;
        Control* c = control;
        AccessRoot* r = root;
        r->AddRef();
        c->AccessDo();   // (it may rebuild the page, and this control with it)
        if (r->elements.count(c)) Access::Changed(r, c);
        r->Release();
        return S_OK;
    }
    HRESULT Get(double* out, double AccessInfo::*field)
    {
        *out = 0;
        if (FAILED(Alive())) return UIA_E_ELEMENTNOTAVAILABLE;
        *out = Info().*field;
        return S_OK;
    }

    LONG refs = 1;
};

AccessElement* AccessRoot::ElementFor(Control* c)
{
    auto found = elements.find(c);
    if (found != elements.end()) return found->second;
    auto e = new AccessElement(this, c, nextId++);   // (the map holds this first reference)
    elements[c] = e;
    return e;
}

HRESULT AccessRoot::Navigate(NavigateDirection direction, IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild) return S_OK;
    std::vector<Control*> list = List();
    if (list.empty()) return S_OK;
    AccessElement* e = ElementFor(direction == NavigateDirection_FirstChild ? list.front() : list.back());
    e->AddRef();
    *out = e;
    return S_OK;
}

HRESULT AccessRoot::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
    POINT p{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(window->Handle(), &p);
    float s = window->Scale();
    for (Control* c = window->Root().HitTest({p.x / s, p.y / s}); c; c = c->Parent())
        if (c->Access().role != Role::None) {
            AccessElement* e = ElementFor(c);
            e->AddRef();
            *out = e;
            return S_OK;
        }
    return S_OK;
}

HRESULT AccessRoot::GetFocus(IRawElementProviderFragment** out)
{
    *out = nullptr;
    if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
    Control* c = window->Focused();
    if (c && c->Access().role != Role::None) {
        AccessElement* e = ElementFor(c);
        e->AddRef();
        *out = e;
    }
    return S_OK;
}

namespace Access {

LRESULT Answer(DirectWindow& window, AccessRoot*& root, WPARAM w, LPARAM l)
{
    if (static_cast<long>(l) != UiaRootObjectId) return DefWindowProcW(window.Handle(), WM_GETOBJECT, w, l);
    if (!root) root = new AccessRoot(window);
    return UiaReturnRawElementProvider(window.Handle(), w, l, root);
}

void FocusMoved(AccessRoot* root, Control* control)
{
    if (!root || !control || control->Access().role == Role::None || !UiaClientsAreListening()) return;
    UiaRaiseAutomationEvent(root->ElementFor(control), UIA_AutomationFocusChangedEventId);
}

void Changed(AccessRoot* root, Control* control)
{
    if (!root || !control || !UiaClientsAreListening()) return;
    AccessInfo info = control->Access();
    if (info.role == Role::None) return;
    AccessElement* e = root->ElementFor(control);
    VARIANT none;
    none.vt = VT_EMPTY;
    auto raise = [&](PROPERTYID id, VARIANT now) {
        UiaRaiseAutomationPropertyChangedEvent(e, id, none, now);
        VariantClear(&now);
    };
    raise(UIA_NamePropertyId, Text(NameOf(control, info)));
    if (info.role == Role::Switch) raise(UIA_ToggleToggleStatePropertyId, Number(info.on ? ToggleState_On : ToggleState_Off));
    if (info.role == Role::Slider || info.role == Role::Spinner) raise(UIA_RangeValueValuePropertyId, Double(info.now));
    if (info.role == Role::Choice || !info.value.empty()) raise(UIA_ValueValuePropertyId, Text(info.value));
    if (info.role == Role::Radio) raise(UIA_SelectionItemIsSelectedPropertyId, Bool(info.on));
    if (info.expanded >= 0)
        raise(UIA_ExpandCollapseExpandCollapseStatePropertyId,
              Number(info.expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed));
}

void Gone(AccessRoot* root, Control* control)
{
    if (!root) return;
    auto found = root->elements.find(control);
    if (found == root->elements.end()) return;
    found->second->control = nullptr;
    found->second->Release();
    root->elements.erase(found);
}

void Announce(HWND window, const std::wstring& text)
{
    BOOL reader = FALSE;
    if (!window || text.empty() || !SystemParametersInfoW(SPI_GETSCREENREADER, 0, &reader, 0) || !reader) return;
    // (Windows 10 1709 and later; looked up so Softlid still starts without it.)
    using Raise = HRESULT(WINAPI*)(IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);
    static auto raise = reinterpret_cast<Raise>(
        GetProcAddress(LoadLibraryExW(L"uiautomationcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "UiaRaiseNotificationEvent"));
    IRawElementProviderSimple* host = nullptr;
    if (!raise || FAILED(UiaHostProviderFromHwnd(window, &host)) || !host) return;
    BSTR words = SysAllocString(text.c_str()), id = SysAllocString(L"Softlid.Announce");
    raise(host, NotificationKind_Other, NotificationProcessing_ImportantMostRecent, words, id);
    SysFreeString(words);
    SysFreeString(id);
    host->Release();
}

void WindowGone(AccessRoot*& root)
{
    if (!root) return;
    for (auto& [control, e] : root->elements) {
        e->control = nullptr;
        UiaDisconnectProvider(e);
        e->Release();
    }
    root->elements.clear();
    root->window = nullptr;
    UiaDisconnectProvider(root);
    root->Release();
    root = nullptr;
}

} // namespace Access
} // namespace bb::ui
