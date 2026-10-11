#pragma once

#include "Document/CameraRaw.h"
extern "C" {
#include "AdjustPixels.h"
}

#include <optional>
#include <vector>

// Photoshop's Light and Color, measured as 17³ tables; some adapt.
namespace CameraRawTables {
constexpr int size = 17;
// The grid the stages compose onto, finer than the tables.
constexpr int grid = 33;

// Every table, red slowest, 3 bytes a colour; loaded once.
const std::vector<uint8_t> &tables();
// Temperature and Tint: the four grid tables around them.
CameraRawStage whiteBalance(double temperature, double tint);
// Exposure's 21 tables in turn, for the lens vignette.
const uint8_t *exposureTables();
// What adaptive sliders read first, in sRGB levels.
struct Brightness {
    double brightest = 128;
    double luminance = 128;
    double logBrightest = 128;
};
Brightness brightness(const uchar *pixels, int width, int height, qsizetype stride);
// Camera Raw's order, sliders at zero left out.
std::vector<CameraRawStage> stages(const CameraRawSettings &settings, const Brightness &brightness);
// Color Grading's stages: luminance, global, midtones, highlights, shadows.
std::vector<CameraRawStage> gradingStages(const CameraRawGradingSettings &grading);
// The Mixer's stages: luminance, then saturation, then hue.
std::vector<CameraRawStage> mixerStages(const CameraRawMixerSettings &mixer);
// The stages over the composing grid; empty without stages.
std::vector<float> compose(const std::vector<CameraRawStage> &stages);
// Temperature and Tint turning a straight sRGB colour gray.
std::optional<CameraRawSettings::Balance> neutralize(double red, double green, double blue);
}
