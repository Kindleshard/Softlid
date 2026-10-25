#include "core/ColorMath.h"
#include <algorithm>
#include <cmath>

namespace bb::ColorMath {
namespace {

// Tanner Helland's black-body approximation (0..255 per channel).
void Helland(double kelvin, double& r, double& g, double& b)
{
    double t = kelvin / 100;
    r = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -0.1332047592);
    g = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60, -0.0755148492);
    b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
    r = std::clamp(r, 0.0, 255.0);
    g = std::clamp(g, 0.0, 255.0);
    b = std::clamp(b, 0.0, 255.0);
}

// An effect as a 3x3 mix (m[in][out]) plus offsets.
struct Mix { float m[3][3] = {}; float c[3] = {}; };

Mix MixFor(int effect, int style)
{
    Mix x;
    float w709[3] = {0.2126f, 0.7152f, 0.0722f}, w601[3] = {0.299f, 0.587f, 0.114f};
    const float* w = style == 0 ? w709 : w601;
    switch (effect) {
    case Grayscale: {
        // Black and white: more contrast, but greys above about 15% stay visible.
        float gain = style == 2 ? 1.4f : 1.0f, lift = style == 2 ? -0.2f : 0.0f;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) x.m[i][j] = w[i] * gain;
        x.c[0] = x.c[1] = x.c[2] = lift;
        break;
    }
    case Sepia: {
        const float s[3][3] = {{0.393f, 0.349f, 0.272f}, {0.769f, 0.686f, 0.534f}, {0.189f, 0.168f, 0.131f}};
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) x.m[i][j] = s[i][j];
        break;
    }
    case Eink: {
        const float paper[3] = {1.0f, 0.965f, 0.89f};
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) x.m[i][j] = w601[i] * 0.84f * paper[j];
        for (int j = 0; j < 3; j++) x.c[j] = 0.08f * paper[j];
        break;
    }
    case RedNight:
        for (int i = 0; i < 3; i++) x.m[i][0] = w[i];
        break;
    default:
        x.m[0][0] = x.m[1][1] = x.m[2][2] = 1;
        break;
    }
    return x;
}

} // namespace

double Kelvin(double percent) { return 6500 - percent * 46; }

int KelvinFor(double percent) { return static_cast<int>(std::lround(Kelvin(percent) / 50.0)) * 50; }

Rgb KelvinToRgb(double kelvin)
{
    double r0, g0, b0, r1, g1, b1;
    Helland(6500, r0, g0, b0);
    Helland(kelvin, r1, g1, b1);
    return {static_cast<float>(std::min(1.0, r1 / r0)), static_cast<float>(std::min(1.0, g1 / g0)),
            static_cast<float>(std::min(1.0, b1 / b0))};
}

Matrix Build(Rgb gains, bool invert, int effect, int style)
{
    // out_j = k_j * (mix_j + c_j), or k_j * (1 - mix_j - c_j) inverted.
    Matrix e{};
    const float k[3] = {gains.r, gains.g, gains.b};
    Mix x = MixFor(effect, style);
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) e[i * 5 + j] = (invert ? -x.m[i][j] : x.m[i][j]) * k[j];
        e[20 + j] = (invert ? 1 - x.c[j] : x.c[j]) * k[j];
    }
    e[18] = 1;   // alpha passes through
    e[24] = 1;
    return e;
}

void Through(int effect, int style, int& r, int& g, int& b)
{
    Mix x = MixFor(effect, style);
    const float in[3] = {r / 255.0f, g / 255.0f, b / 255.0f};
    int* out[3] = {&r, &g, &b};
    for (int j = 0; j < 3; j++) {
        float v = in[0] * x.m[0][j] + in[1] * x.m[1][j] + in[2] * x.m[2][j] + x.c[j];
        *out[j] = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255);
    }
}

} // namespace bb::ColorMath
