#include "features/tone/SmartGpu.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace bb {
namespace {

// Pass 1 (half resolution) writes linear brightness; its mipmaps are blurred "backgrounds" at every scale. Pass 2
// gives each pixel an edge-aware background, remaps that background (bright toward mid-grey, dark a little deeper,
// never brighter) and scales the pixel by the same factor, so its contrast with its surroundings is unchanged; one
// factor for R, G and B keeps hue and saturation.
constexpr char Hlsl[] = R"(
Texture2D src : register(t0);
Texture2D<float> lum : register(t1);
SamplerState bilinear : register(s0);
// mul: rgb = brightness and warmth, w = invert. tone: x = compress, y = expand, z = dim (stops), w = background mip level.
// size: xy = 1 / output size, zw = 1 / size of the half-resolution brightness map.
cbuffer Params : register(b0) { float4 mul; float4 tone; float4 size; };

static const float3 Luma = float3(0.2126, 0.7152, 0.0722);
static const float Anchor = -2.47;

float3 fetch(int2 p)
{
    float3 c = src.Load(int3(p, 0)).rgb;
    return mul.w > 0.5 ? 1 - c : c;
}

float lin(float3 c) { return dot(pow(c, 2.2), Luma); }

float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Pass 1, half resolution: linear brightness of each 2x2 block. Averaging in linear light (not log) means a few black
// glyphs lower a cell only by their share of its area, so text doesn't drag the page's background down.
float PSLum(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy) * 2;
    return 0.25 * (lin(fetch(p)) + lin(fetch(p + int2(1, 0))) + lin(fetch(p + int2(0, 1))) + lin(fetch(p + int2(1, 1))));
}

// Smart layer, one output pixel per 8x8 cell: linear brightness of the cell's brightest pixel (thin white text, small
// bright icons), which the cell's average hides.
float PSPeak(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy) * 8;
    float m = 0;
    [unroll] for (int j = 0; j < 8; j++)
        [unroll] for (int i = 0; i < 8; i++)
            m = max(m, lin(fetch(p + int2(i, j))));
    return m;
}

// The pixel's background: nine samples of the blurred map, each trusted by how close it is to the pixel's own
// brightness (sigma one stop), so a white page doesn't bleed into the dark text or photo next to it (no halos).
float background(float2 uv, float l)
{
    float2 step = size.zw * exp2(tone.w);
    float sum = 1.5 * l, weights = 1.5;
    [unroll] for (int j = -1; j <= 1; j++)
        [unroll] for (int i = -1; i <= 1; i++)
        {
            float b = log2(max(lum.SampleLevel(bilinear, uv + float2(i, j) * step, tone.w), 1e-4));
            float w = exp(-0.5 * (b - l) * (b - l) - 0.5 * (i * i + j * j)) + 0.001;
            sum += b * w;
            weights += w;
        }
    return sum / weights;
}

float4 PS(float4 pos : SV_Position) : SV_Target
{
    float3 c = fetch(int2(pos.xy));
    float l = log2(max(lin(c), 1e-4));
    float b = background(pos.xy * size.xy, l);
    float nb = (b > Anchor ? Anchor + (b - Anchor) * tone.x : Anchor + (b - Anchor) * tone.y) + tone.z;
    float k = exp2(min(nb - b, 0));
    return float4(saturate(c * pow(k, 1 / 2.2) * mul.rgb), 1);
}
)";

float Half(uint16_t h)
{
    int e = (h >> 10) & 31, mant = h & 1023;
    float v = e == 0 ? mant * 5.9604645e-8f : e == 31 ? 3.4e38f : static_cast<float>((1024 + mant) * std::pow(2.0, e - 25));
    return (h & 0x8000) ? -v : v;
}

std::wstring Hex(const wchar_t* what, HRESULT hr)
{
    wchar_t text[160];
    swprintf_s(text, L"%s failed, 0x%08X", what, static_cast<unsigned>(hr));
    return text;
}

} // namespace

// The adapter and output that show this screen (on hybrid-GPU laptops that isn't always the default GPU).
bool SmartGpu::FindOutput(const RECT& bounds, std::wstring& deviceName)
{
    for (UINT a = 0;; a++) {
        Ptr<IDXGIAdapter1> ad;
        if (FAILED(factory->EnumAdapters1(a, &ad))) return false;
        for (UINT o = 0;; o++) {
            Ptr<IDXGIOutput> out;
            if (FAILED(ad->EnumOutputs(o, &out))) break;
            DXGI_OUTPUT_DESC d{};
            out->GetDesc(&d);
            if (EqualRect(&d.DesktopCoordinates, &bounds)) {
                adapter = ad;
                output = out;
                deviceName = d.DeviceName;
                return true;
            }
        }
    }
}

SmartGpu::Ptr<ID3DBlob> SmartGpu::Compile(const char* entry, const char* target, std::wstring& error)
{
    Ptr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(Hlsl, sizeof Hlsl - 1, "BlinkBuddy", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        error = L"Compiling the shader failed";
        return nullptr;
    }
    return code;
}

bool SmartGpu::Init(HWND window, const RECT& bounds, std::wstring& error)
{
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return error = Hex(L"Creating the DXGI factory", hr), false;
    std::wstring deviceName;
    if (!FindOutput(bounds, deviceName)) return error = L"couldn't find the display on any graphics adapter", false;
    if (HDC dc = CreateDCW(deviceName.c_str(), nullptr, nullptr, nullptr)) {
        refreshHz = GetDeviceCaps(dc, VREFRESH);
        DeleteDC(dc);
    }
    hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                           &device, nullptr, &ctx);
    if (FAILED(hr)) return error = Hex(L"Creating the Direct3D 11 device", hr), false;
    if (FAILED(hr = output.As(&output1))) return error = Hex(L"Getting IDXGIOutput1", hr), false;
    if (FAILED(hr = output1->DuplicateOutput(device.Get(), &dup))) return error = Hex(L"Starting Desktop Duplication", hr), false;

    width = bounds.right - bounds.left;
    height = bounds.bottom - bounds.top;
    if (window) {
        // Flip model is the efficient one; fall back to the older blit model if the window type refuses it.
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc = {width, height, {0, 0}, DXGI_FORMAT_B8G8R8A8_UNORM};
        desc.SampleDesc = {1, 0};
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(device.Get(), &desc, &swap))) {
            desc.BufferCount = 1;
            desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            if (FAILED(hr = factory->CreateSwapChain(device.Get(), &desc, &swap))) return error = Hex(L"Creating the swap chain", hr), false;
        }
        Ptr<ID3D11Texture2D> back;
        if (FAILED(hr = swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return error = Hex(L"Getting the back buffer", hr), false;
        if (FAILED(hr = device->CreateRenderTargetView(back.Get(), nullptr, &rtv))) return error = Hex(L"Creating the render target", hr), false;
    }

    auto vsCode = Compile("VS", "vs_4_0", error), psCode = Compile("PS", "ps_4_0", error), lumCode = Compile("PSLum", "ps_4_0", error);
    if (!vsCode || !psCode || !lumCode) return false;
    device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs);
    device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps);
    device->CreatePixelShader(lumCode->GetBufferPointer(), lumCode->GetBufferSize(), nullptr, &psLum);
    D3D11_BUFFER_DESC bd{48, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    if (FAILED(hr = device->CreateBuffer(&bd, nullptr, &cb))) return error = Hex(L"Creating the constant buffer", hr), false;

    // The half-resolution brightness map with a full mip chain: each level is a wider blur of the screen.
    lumWidth = (width + 1) / 2;
    lumHeight = (height + 1) / 2;
    D3D11_TEXTURE2D_DESC ld{};
    ld.Width = lumWidth;
    ld.Height = lumHeight;
    ld.MipLevels = 0;
    ld.ArraySize = 1;
    ld.Format = DXGI_FORMAT_R16_FLOAT;
    ld.SampleDesc = {1, 0};
    ld.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ld.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(hr = device->CreateTexture2D(&ld, nullptr, &lumTex))) return error = Hex(L"Creating the brightness map", hr), false;
    device->CreateRenderTargetView(lumTex.Get(), nullptr, &lumRtv);
    device->CreateShaderResourceView(lumTex.Get(), nullptr, &lumSrv);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &sampler);

    // Backgrounds are judged about 1/50 of the screen height across, whatever the resolution.
    constants[7] = static_cast<float>(std::max(0.0, std::round(std::log2(lumHeight / 50.0))));
    constants[8] = 1.0f / width;
    constants[9] = 1.0f / height;
    constants[10] = 1.0f / lumWidth;
    constants[11] = 1.0f / lumHeight;
    SetParams({});   // defined contents before the first draw
    return true;
}

void SmartGpu::SetParams(const Params& p)
{
    constants[0] = p.r;
    constants[1] = p.g;
    constants[2] = p.b;
    constants[3] = p.invert ? 1.0f : 0.0f;
    constants[4] = p.compress;
    constants[5] = p.expand;
    constants[6] = p.dimStops;   // [7..11] are fixed at Init
    ctx->UpdateSubresource(cb.Get(), 0, nullptr, constants, 0, 0);
}

bool SmartGpu::CreateSource(ID3D11Texture2D* desktop)
{
    D3D11_TEXTURE2D_DESC d{};
    desktop->GetDesc(&d);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &source)) && SUCCEEDED(device->CreateShaderResourceView(source.Get(), nullptr, &srv));
}

int SmartGpu::NextFrame(UINT timeoutMs, LONGLONG& presentedAt, LONGLONG* changedPixels)
{
    presentedAt = 0;
    if (changedPixels) *changedPixels = 0;
    if (!dup) return -1;
    DXGI_OUTDUPL_FRAME_INFO info{};
    Ptr<IDXGIResource> resource;
    HRESULT hr = dup->AcquireNextFrame(timeoutMs, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return 0;
    if (FAILED(hr)) return -1;
    int result = 0;
    if (info.LastPresentTime.QuadPart != 0) {
        if (changedPixels && info.TotalMetadataBufferSize) {
            // The area of the rectangles Windows says changed (moved areas count by their destination).
            if (metadata.size() < info.TotalMetadataBufferSize) metadata.resize(info.TotalMetadataBufferSize);
            UINT used = 0;
            auto area = [](const RECT& r) { return static_cast<LONGLONG>(std::max(0L, r.right - r.left)) * std::max(0L, r.bottom - r.top); };
            if (SUCCEEDED(dup->GetFrameMoveRects(static_cast<UINT>(metadata.size()), reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata.data()), &used)))
                for (UINT o = 0; o + sizeof(DXGI_OUTDUPL_MOVE_RECT) <= used; o += sizeof(DXGI_OUTDUPL_MOVE_RECT))
                    *changedPixels += area(reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(metadata.data() + o)->DestinationRect);
            if (SUCCEEDED(dup->GetFrameDirtyRects(static_cast<UINT>(metadata.size()), reinterpret_cast<RECT*>(metadata.data()), &used)))
                for (UINT o = 0; o + sizeof(RECT) <= used; o += sizeof(RECT)) *changedPixels += area(*reinterpret_cast<RECT*>(metadata.data() + o));
        }
        Ptr<ID3D11Texture2D> tex;
        if (SUCCEEDED(resource.As(&tex)) && (source || CreateSource(tex.Get()))) {
            ctx->CopyResource(source.Get(), tex.Get());
            presentedAt = info.LastPresentTime.QuadPart;
            result = 1;
        }
    }
    resource.Reset();
    dup->ReleaseFrame();   // hand the desktop image back as early as possible
    return result;
}

void SmartGpu::Reacquire()
{
    dup.Reset();
    srv.Reset();
    source.Reset();
    output1->DuplicateOutput(device.Get(), &dup);
}

// Pass 1: the half-resolution brightness map (unbound as an input before drawing into it).
void SmartGpu::LumPass()
{
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);   // each pass is one triangle covering its target
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ID3D11Buffer* buffers[] = {cb.Get()};
    ctx->PSSetConstantBuffers(0, 1, buffers);
    ID3D11ShaderResourceView* views[] = {srv.Get(), nullptr};
    ctx->PSSetShaderResources(0, 2, views);
    ID3D11RenderTargetView* targets[] = {lumRtv.Get()};
    ctx->OMSetRenderTargets(1, targets, nullptr);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(lumWidth), static_cast<float>(lumHeight), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->PSSetShader(psLum.Get(), nullptr, 0);
    ctx->Draw(3, 0);
}

void SmartGpu::Render()
{
    LumPass();
    // Its blurred copies, then pass 2 to the screen.
    ID3D11RenderTargetView* targets[] = {rtv.Get()};
    ctx->OMSetRenderTargets(1, targets, nullptr);
    ctx->GenerateMips(lumSrv.Get());
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* views[] = {srv.Get(), lumSrv.Get()};
    ctx->PSSetShaderResources(0, 2, views);
    ID3D11SamplerState* samplers[] = {sampler.Get()};
    ctx->PSSetSamplers(0, 1, samplers);
    ctx->Draw(3, 0);
    swap->Present(0, 0);
}

std::vector<float> SmartGpu::Probe(int level, int& w, int& h)
{
    w = std::max(1, static_cast<int>(lumWidth >> level));
    h = std::max(1, static_cast<int>(lumHeight >> level));
    LumPass();
    ID3D11RenderTargetView* none[] = {nullptr};
    ctx->OMSetRenderTargets(1, none, nullptr);   // released as a target so its mipmaps can be built
    ctx->GenerateMips(lumSrv.Get());
    if (!stageMap) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16_FLOAT;
        d.SampleDesc = {1, 0};
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device->CreateTexture2D(&d, nullptr, &stageMap);
    }
    ctx->CopySubresourceRegion(stageMap.Get(), 0, 0, 0, 0, lumTex.Get(), level, nullptr);
    return ReadMap(stageMap.Get(), w, h);
}

std::vector<float> SmartGpu::ProbePeaks(int w, int h)
{
    if (!psPeak) {
        std::wstring error;
        auto code = Compile("PSPeak", "ps_4_0", error);
        if (!code) return std::vector<float>(static_cast<size_t>(w) * h, 0.0f);
        device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &psPeak);
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16_FLOAT;
        d.SampleDesc = {1, 0};
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        device->CreateTexture2D(&d, nullptr, &peakTex);
        device->CreateRenderTargetView(peakTex.Get(), nullptr, &peakRtv);
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device->CreateTexture2D(&d, nullptr, &stagePeak);
    }
    ID3D11ShaderResourceView* views[] = {srv.Get(), nullptr};
    ctx->PSSetShaderResources(0, 2, views);
    ID3D11RenderTargetView* targets[] = {peakRtv.Get()};
    ctx->OMSetRenderTargets(1, targets, nullptr);
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->PSSetShader(psPeak.Get(), nullptr, 0);
    ctx->Draw(3, 0);
    ID3D11RenderTargetView* none[] = {nullptr};
    ctx->OMSetRenderTargets(1, none, nullptr);
    ctx->CopyResource(stagePeak.Get(), peakTex.Get());
    return ReadMap(stagePeak.Get(), w, h);
}

std::vector<float> SmartGpu::ReadMap(ID3D11Texture2D* staging, int w, int h)
{
    std::vector<float> map(static_cast<size_t>(w) * h, 0.0f);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (!staging || FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) return map;
    for (int y = 0; y < h; y++) {
        auto* row = reinterpret_cast<const uint16_t*>(static_cast<const BYTE*>(m.pData) + static_cast<size_t>(y) * m.RowPitch);
        for (int x = 0; x < w; x++) map[static_cast<size_t>(y) * w + x] = Half(row[x]);
    }
    ctx->Unmap(staging, 0);
    return map;
}

} // namespace bb
