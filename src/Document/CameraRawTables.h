#pragma once

#include "Document/CameraRaw.h"
extern "C" {
#include "AdjustPixels.h"
}

#include <optional>
#include <vector>

// Photoshop's Light and Color sliders, measured as 17³ colour tables.
namespace CameraRawTables {
constexpr int size = 17;
// The grid the stages compose onto, finer than the tables.
constexpr int grid = 33;

// Every table, red slowest, 3 bytes a colour; loaded once.
const std::vector<uint8_t> &tables();
// Temperature and Tint: the four grid tables around them.
CameraRawStage whiteBalance(double temperature, double tint);
struct Stages {
    std::vector<CameraRawStage> before;
    std::vector<CameraRawStage> after;
};
// Before Highlights and Shadows, and after; zero sliders left out.
Stages stages(const CameraRawSettings &settings);
// The stages over the composing grid; empty without stages.
std::vector<float> compose(const std::vector<CameraRawStage> &stages);
// Temperature and Tint turning a straight sRGB colour gray.
std::optional<CameraRawSettings::Balance> neutralize(double red, double green, double blue);
}
