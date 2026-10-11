#pragma once
#include "Document/Dither.h"

// Filter › Scanlines: the picture as a CRT draws it.
struct ScanlinesSettings {
    static constexpr double lineSpacingLow = 2, lineSpacingHigh = 32;
    static constexpr double wobbleLow = 0, wobbleHigh = 64;
    static constexpr double displaceLow = -100, displaceHigh = 100;
    static constexpr double splitLow = 0, splitHigh = 16;
    // How far apart the lines are, in layer pixels.
    double lineSpacing = 4;
    // How much of the gap a bright line fills, 5–100%.
    double thickness = 70;
    // 0–100%: light blooming round the lines.
    double glow = 0;
    // 0–100%: darker tones bead the lines into dots.
    double dots = 0;
    // Pixels the lines waver sideways, down the screen.
    double wobble = 0;
    // Pixels a line rises over brightness; negative, falls.
    double displace = 0;
    // 0–100%: how far brightness is smoothed before displacing.
    double smoothness = 50;
    // 0–100%: darker tones draw no line.
    double threshold = 0;
    // Pixels the red and blue move apart.
    double split = 0;
    // −100…100: darker or lighter, flatter or punchier.
    double density = 0;
    double contrast = 0;
    // 0–100%: the lines' brightness over black.
    double blackLevel = 0;
    DitherColors colors = DitherColors::blackWhite;
    AdjustmentColor dark = AdjustmentColor(0, 0, 0);
    AdjustmentColor light = AdjustmentColor(1, 1, 1);
    ScanlinesSettings normalized() const;
    QImage apply(const QImage &image) const;
    friend bool operator==(const ScanlinesSettings &, const ScanlinesSettings &) = default;

private:
    QImage glowing(const QImage &image) const;
};
