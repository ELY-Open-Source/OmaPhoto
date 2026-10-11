#include "Document/Scanlines.h"
#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include <algorithm>
#include <cmath>
extern "C" {
#include "DitherPixels.h"
}

// Swift's Scanlines.swift: its settings, the kernel, the glow.
namespace {
std::array<uint8_t, 3> bytes(const AdjustmentColor &color)
{
    return {uint8_t(std::lround(color.red * 255)), uint8_t(std::lround(color.green * 255)), uint8_t(std::lround(color.blue * 255))};
}
}

ScanlinesSettings ScanlinesSettings::normalized() const
{
    using ImageAdjustmentPixels::clamp;
    ScanlinesSettings result = *this;
    result.lineSpacing = std::round(clamp(lineSpacing, lineSpacingLow, lineSpacingHigh, 4));
    result.thickness = clamp(thickness, 5, 100, 70);
    result.glow = clamp(glow, 0, 100, 0);
    result.dots = clamp(dots, 0, 100, 0);
    result.wobble = clamp(wobble, wobbleLow, wobbleHigh, 0);
    result.displace = clamp(displace, displaceLow, displaceHigh, 0);
    result.smoothness = clamp(smoothness, 0, 100, 50);
    result.threshold = clamp(threshold, 0, 100, 0);
    result.split = std::round(clamp(split, splitLow, splitHigh, 0));
    result.density = clamp(density, -100, 100, 0);
    result.contrast = clamp(contrast, -100, 100, 0);
    result.blackLevel = clamp(blackLevel, 0, 100, 0);
    result.dark = dark.clamped();
    result.light = light.clamped();
    return result;
}

QImage ScanlinesSettings::apply(const QImage &image) const
{
    const ScanlinesSettings settings = normalized();
    const bool two = settings.colors == DitherColors::twoColors;
    const std::array<uint8_t, 3> darkColor = two ? bytes(settings.dark) : std::array<uint8_t, 3>{0, 0, 0};
    const std::array<uint8_t, 3> lightColor = two ? bytes(settings.light) : std::array<uint8_t, 3>{255, 255, 255};
    bool failed = false;
    const QImage drawn = ImageAdjustmentPixels::run(image, [&](uchar *pixels, int width, int height, qsizetype stride) {
        ScanlinesParams params{.spacing = int(settings.lineSpacing), .thickness = float(settings.thickness / 100), .dots = float(settings.dots / 100),
                               .wobble = float(settings.wobble), .displace = float(settings.displace), .threshold = float(settings.threshold / 100),
                               .split = float(settings.split), .density = float(settings.density / 100), .contrast = float(settings.contrast / 100),
                               .blackLevel = float(settings.blackLevel / 100), .smoothness = float(settings.smoothness / 100),
                               .originalColors = settings.colors == DitherColors::original ? 1 : 0, .dark = {darkColor[0], darkColor[1], darkColor[2]},
                               .light = {lightColor[0], lightColor[1], lightColor[2]}};
        failed = scanlines_apply(pixels, size_t(width), size_t(height), size_t(stride), &params) == 0;
    });
    if (failed)
        throw ExportError(ExportError::Kind::render);
    return settings.glow > 0 ? settings.glowing(drawn) : drawn;
}

// The lines' light blurred wide and added back: phosphor bloom.
QImage ScanlinesSettings::glowing(const QImage &image) const
{
    // Blurred small, scaled back up: same light, less work.
    const double sigma = lineSpacing * 3 + 3, shrink = std::max(1.0, std::floor(sigma / 4));
    const int width = image.width(), height = image.height();
    // Swift clamps first: edges repeat outward before shrinking.
    const int step = int(shrink), pad = int(std::ceil(sigma * 3 / shrink)) * step;
    // Far sides fill whole steps: both scales are exact.
    const auto whole = [step](int side) { return (side + step - 1) / step * step; };
    QImage padded = BrushRaster::context(whole(width + 2 * pad), whole(height + 2 * pad), false);
    for (int y = 0; y < padded.height(); ++y) {
        const uchar *source = image.constScanLine(std::clamp(y - pad, 0, height - 1));
        uchar *out = padded.scanLine(y);
        for (int x = 0; x < padded.width(); ++x)
            std::copy_n(source + std::clamp(x - pad, 0, width - 1) * 4, 4, out + x * 4);
    }
    const int smallWidth = padded.width() / step, smallHeight = padded.height() / step;
    const QImage small = padded.scaled(smallWidth, smallHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        throw ExportError(ExportError::Kind::render);
    const QImage blurred = PixelAdjust::gaussianBlur(small, sigma / shrink, true)
                               .scaled(smallWidth * step, smallHeight * step, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    QImage result = image.copy();
    if (blurred.isNull() || result.isNull())
        throw ExportError(ExportError::Kind::render);
    // The scaled light's own rows, cropped to the image.
    QImage bloom = BrushRaster::context(width, height, false);
    for (int y = 0; y < height; ++y)
        std::copy_n(blurred.constScanLine(y + pad) + pad * 4, width * 4, bloom.scanLine(y));
    dither_glow(result.bits(), bloom.constBits(), size_t(width), size_t(height), size_t(result.bytesPerLine()), float(glow / 100 * 2.5));
    return result;
}
