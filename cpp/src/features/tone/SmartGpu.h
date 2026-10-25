#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace bb {

// The graphics card's side of the smart engines, owned by the worker thread that made it. Desktop Duplication hands
// over each new desktop image (only when something changed). With a window, two shader passes tone-map it into that
// window's swap chain (Smart GPU); without one, it only measures the screen's brightness (Smart layer).
class SmartGpu {
public:
    // The shader's settings: brightness and warmth per channel, invert, and the tone curve (see ToneCurve::ForLevel).
    struct Params {
        float r = 1, g = 1, b = 1, compress = 1, expand = 1, dimStops = 0;
        bool invert = false;
    };

    SmartGpu() = default;
    SmartGpu(const SmartGpu&) = delete;
    SmartGpu& operator=(const SmartGpu&) = delete;

    bool Init(HWND window, const RECT& bounds, std::wstring& error);   // window null: measuring only
    void SetParams(const Params& p);
    // 1 = a new desktop image was copied, 0 = nothing new (timeout, or only the pointer moved), -1 = desktop lost
    // (lock screen, a UAC prompt, a resolution change, an exclusive full-screen game). changedPixels: the area Windows
    // says changed.
    int NextFrame(UINT timeoutMs, LONGLONG& presentedAt, LONGLONG* changedPixels = nullptr);
    void Reacquire();
    void Render();   // the two passes, then present

    // Smart layer: linear brightness of the latest image, averaged over cells of 2^(level+1) pixels; and the
    // brightest pixel of each 8x8 cell on the same grid.
    std::vector<float> Probe(int level, int& w, int& h);
    std::vector<float> ProbePeaks(int w, int h);

    int refreshHz = 60;

private:
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    bool FindOutput(const RECT& bounds, std::wstring& deviceName);
    Ptr<ID3DBlob> Compile(const char* entry, const char* target, std::wstring& error);
    bool CreateSource(ID3D11Texture2D* desktop);
    std::vector<float> ReadMap(ID3D11Texture2D* staging, int w, int h);
    void LumPass();

    Ptr<IDXGIFactory1> factory;
    Ptr<IDXGIAdapter1> adapter;
    Ptr<IDXGIOutput> output;
    Ptr<IDXGIOutput1> output1;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> ctx;
    Ptr<IDXGIOutputDuplication> dup;
    Ptr<IDXGISwapChain> swap;
    Ptr<ID3D11RenderTargetView> rtv;
    Ptr<ID3D11VertexShader> vs;
    Ptr<ID3D11PixelShader> ps, psLum, psPeak;
    Ptr<ID3D11Buffer> cb;
    Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11Texture2D> source, lumTex, stageMap, peakTex, stagePeak;
    Ptr<ID3D11ShaderResourceView> srv, lumSrv;
    Ptr<ID3D11RenderTargetView> lumRtv, peakRtv;
    UINT width = 0, height = 0, lumWidth = 0, lumHeight = 0;
    float constants[12] = {};
    std::vector<BYTE> metadata;
};

} // namespace bb
