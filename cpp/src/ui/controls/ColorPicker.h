#pragma once
#include "ui/Control.h"
#include <functional>
#include <vector>

namespace bb::ui {

class ColorPopup;

// Colours as hue (0..360), saturation and value (0..1).
namespace Hsv {
int ToArgb(double h, double s, double v);
void FromArgb(int argb, double& h, double& s, double& v);
} // namespace Hsv

// A colour, shown as a round swatch; clicking it opens the colour wheel pop-up. onChange fires while you pick (for a
// live preview), onChosen once the pop-up closes. With allowAccent the pop-up also offers "Match accent", and
// UseAccent then means the colour follows the accent (Value is ignored).
class ColorButton : public Control {
public:
    ColorButton();
    ~ColorButton() override;
    int Value() const { return value; }
    void SetValue(int argb);
    bool UseAccent() const { return useAccent; }
    void SetUseAccent(bool on);
    int Shown() const;

    bool allowAccent = false;
    std::vector<int> presets;   // quick picks (the accent colours unless set)
    std::function<void()> onChange, onChosen;

    void Paint(Canvas& canvas) override;
    void Click() override;
    bool Key(UINT key) override;

private:
    int value = 0xFF000000;
    bool useAccent = false;
    ColorPopup* popup = nullptr;   // open now (it deletes itself when it closes)
};

} // namespace bb::ui
