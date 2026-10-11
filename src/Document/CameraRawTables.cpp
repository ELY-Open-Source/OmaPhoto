#include "Document/CameraRawTables.h"

#include "IO/ImageExporter.h"
#include "Logging.h"

#include <QFile>
#include <array>
#include <cmath>
#include <span>
#include <algorithm>
#include <stdexcept>
#include <lzma.h>

namespace CameraRawTables {
namespace {
constexpr size_t entries = size_t(size) * size * size * 3;
// White balance, five sliders, then three adaptive ones.
constexpr int exposureStart = 81, whitesStart = 102, blacksStart = 111, saturationStart = 120, vibranceStart = 129;
constexpr int contrastStart = 138, highlightsStart = 187, shadowsStart = 236;
// Then the Mixer, Calibration, the parametric curve, Color Grading.
constexpr int mixerStart = 285, calibrationStart = 405, parametricStart = 435, gradingStart = 455, gradingLuminanceStart = 743, count = 763;
constexpr std::array<double, 7> brightnesses{53, 80, 104, 130, 160, 190, 224};
constexpr std::array<double, 7> adaptiveValues{-100, -50, -25, 0, 25, 50, 100};

// An xz stream, as Foundation's `.lzma` writes it.
std::vector<uint8_t> unpacked(const QByteArray &packed, size_t expected)
{
    std::vector<uint8_t> out(expected);
    uint64_t memory = UINT64_MAX;
    size_t read = 0, made = 0;
    const lzma_ret status = lzma_stream_buffer_decode(&memory, 0, nullptr, reinterpret_cast<const uint8_t *>(packed.constData()), &read,
                                                      size_t(packed.size()), out.data(), &made, out.size());
    if (status != LZMA_OK || made != expected || read != size_t(packed.size()))
        throw std::logic_error("Camera Raw's tables do not unpack to their size");
    return out;
}

// Each entry: a running difference from no change, stepped.
std::vector<uint8_t> load()
{
    QFile file(QStringLiteral(":/CameraRawTables.bin"));
    if (!file.open(QIODevice::ReadOnly))
        throw std::logic_error("Camera Raw's tables are missing from the build");
    const QByteArray bytes = file.readAll();
    const auto field = [&](int at) { return int(uchar(bytes[at])) | int(uchar(bytes[at + 1])) << 8; };
    if (bytes.size() <= 10 || !bytes.startsWith("CRT3") || field(4) != size || field(6) != count || field(8) <= 0)
        throw std::logic_error("Camera Raw's tables have the wrong header");
    const int step = field(8);
    const std::vector<uint8_t> planes = unpacked(bytes.mid(10), count * entries);
    std::vector<uint8_t> out(count * entries);
    const size_t cells = size_t(size) * size * size;
    int level[size];
    for (int i = 0; i < size; ++i)
        level[i] = int(std::lround(i * 255.0 / (size - 1)));
    for (size_t table = 0; table < count; ++table) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const size_t base = (table * 3 + channel) * cells;
            uint8_t running = 0;
            for (size_t cell = 0; cell < cells; ++cell) {
                running = uint8_t(running + planes[base + cell]);
                const size_t axis = channel == 0 ? cell / (size * size) : channel == 1 ? (cell / size) % size : cell % size;
                out[table * entries + cell * 3 + channel] = uint8_t(std::clamp(level[axis] + int(int8_t(running)) * step, 0, 255));
            }
        }
    }
    qCInfo(lcRendering) << "Camera Raw's tables loaded:" << count << "tables";
    return out;
}

const uint8_t *table(int index)
{
    return tables().data() + size_t(index) * entries;
}

struct Between {
    int index;
    double fraction;
};

struct Part {
    const uint8_t *table;
    double weight;
};

// Up to eight tables a stage; null means no change.
CameraRawStage blend(const std::vector<Part> &parts)
{
    CameraRawStage stage{};
    for (size_t index = 0; index < std::min<size_t>(parts.size(), 8); ++index) {
        stage.table[index] = parts[index].table;
        stage.weight[index] = float(parts[index].weight);
    }
    return stage;
}

// The two tables beside `value`, and how far between.
Between between(double value, double low, double step, int tables)
{
    const double position = std::min(double(tables - 1), std::max(0.0, (value - low) / step));
    const int index = std::min(tables - 2, int(position));
    return {index, position - index};
}

// The two of `points` (rising) beside `value`, and how far.
Between between(double value, std::span<const double> points)
{
    const double held = std::min(points.back(), std::max(points.front(), value));
    int index = 0;
    while (index < int(points.size()) - 2 && held > points[index + 1])
        ++index;
    return {index, (held - points[index]) / (points[index + 1] - points[index])};
}

// An adaptive slider: four tables round its value and brightness.
CameraRawStage adaptive(int start, double value, double brightness)
{
    const Between row = between(brightness, brightnesses), column = between(value, adaptiveValues);
    const double down = row.fraction, across = column.fraction;
    const int width = int(adaptiveValues.size()), base = start + row.index * width + column.index;
    return blend({{table(base), (1 - down) * (1 - across)}, {table(base + 1), (1 - down) * across}, {table(base + width), down * (1 - across)},
                  {table(base + width + 1), down * across}});
}

CameraRawStage stage(int start, double value, double low, double step, int tables)
{
    const Between at = between(value, low, step, tables);
    return blend({{table(start + at.index), 1 - at.fraction}, {table(start + at.index + 1), at.fraction}});
}

CameraRawStage fifty(int start, double value)
{
    return stage(start, value, -100, 50, 5);
}
}

const std::vector<uint8_t> &tables()
{
    static const std::vector<uint8_t> loaded = load();
    return loaded;
}

CameraRawStage whiteBalance(double temperature, double tint)
{
    const Between row = between(temperature, -100, 25, 9), column = between(tint, -100, 25, 9);
    const double down = row.fraction, across = column.fraction;
    const int base = row.index * 9 + column.index;
    return blend({{table(base), (1 - down) * (1 - across)}, {table(base + 1), (1 - down) * across}, {table(base + 9), down * (1 - across)},
                  {table(base + 10), down * across}});
}

const uint8_t *exposureTables()
{
    return table(exposureStart);
}

Brightness brightness(const uchar *pixels, int width, int height, qsizetype stride)
{
    double out[3] = {0, 0, 0};
    camera_raw_statistics(pixels, size_t(width), size_t(height), size_t(stride), out);
    return {out[0] * 255, out[1] * 255, out[2] * 255};
}

std::vector<CameraRawStage> stages(const CameraRawSettings &settings, const Brightness &brightness)
{
    std::vector<CameraRawStage> out;
    const CameraRawCalibrationSettings &calibration = settings.calibration;
    const std::array<double, 6> primaries{calibration.redHue,   calibration.redSaturation,  calibration.greenHue,
                                          calibration.greenSaturation, calibration.blueHue, calibration.blueSaturation};
    for (int index = 0; index < 6; ++index)
        if (primaries[size_t(index)] != 0)
            out.push_back(fifty(calibrationStart + index * 5, primaries[size_t(index)]));
    if (settings.exposure != 0)
        out.push_back(stage(exposureStart, settings.exposure, -5, 0.5, 21));
    if (settings.temperature != 0 || settings.tint != 0)
        out.push_back(whiteBalance(settings.temperature, settings.tint));
    if (settings.contrast != 0)
        out.push_back(adaptive(contrastStart, settings.contrast, brightness.brightest));
    if (settings.highlights != 0)
        out.push_back(adaptive(highlightsStart, settings.highlights, brightness.logBrightest));
    if (settings.shadows != 0)
        out.push_back(adaptive(shadowsStart, settings.shadows, brightness.luminance));
    if (settings.whites != 0)
        out.push_back(stage(whitesStart, settings.whites, -100, 25, 9));
    if (settings.blacks != 0)
        out.push_back(stage(blacksStart, settings.blacks, -100, 25, 9));
    if (settings.saturation != 0)
        out.push_back(stage(saturationStart, settings.saturation, -100, 25, 9));
    if (settings.vibrance != 0)
        out.push_back(stage(vibranceStart, settings.vibrance, -100, 25, 9));
    const CameraRawCurveSettings &curve = settings.curve;
    if (curve.hasMeasuredDividers()) {
        const std::array<double, 4> regions{curve.shadows, curve.darks, curve.lights, curve.highlights};
        for (int index = 0; index < 4; ++index)
            if (regions[size_t(index)] != 0)
                out.push_back(fifty(parametricStart + index * 5, regions[size_t(index)]));
    }
    return out;
}

std::vector<CameraRawStage> gradingStages(const CameraRawGradingSettings &grading)
{
    std::vector<CameraRawStage> out;
    const std::array<CameraRawGradeWheel, 4> lumas{grading.shadows, grading.midtones, grading.highlights, grading.global};
    for (int index = 0; index < 4; ++index)
        if (lumas[size_t(index)].luminance != 0)
            out.push_back(fifty(gradingLuminanceStart + index * 5, lumas[size_t(index)].luminance));
    const double balance = grading.balance / 100;
    const double highlightShare = std::max(0.0, balance < 0 ? 1 + 1.4 * balance : 1 + 2 * balance);
    const double shadowShare = std::max(0.0, balance < 0 ? 1 - 1.2 * balance : 1 - 1.4 * balance);
    constexpr std::array<double, 3> blendings{0, 50, 100};
    constexpr std::array<double, 4> saturations{0, 25, 50, 100};
    const Between blended = between(grading.blending, blendings);
    // Blending 0 holds two wheels, 50 all four, 100 two.
    const auto section = [](int blendIndex, int wheel) { return gradingStart + (blendIndex == 0 ? 0 : blendIndex == 1 ? 72 : 216) + wheel * 36; };
    struct Wheel {
        int index;
        CameraRawGradeWheel color;
        double share;
        bool blends;
    };
    for (const Wheel &wheel : {Wheel{3, grading.global, 1, false}, Wheel{2, grading.midtones, 1, false}, Wheel{1, grading.highlights, highlightShare, true},
                               Wheel{0, grading.shadows, shadowShare, true}}) {
        if (!(wheel.color.saturation * wheel.share > 0))
            continue;
        const double position = std::fmod(wheel.color.hue, 360) / 30;
        const int first = int(position) % 12, second = (first + 1) % 12;
        const double around = position - int(position);
        const Between level = between(wheel.color.saturation * wheel.share, saturations);
        std::vector<Part> parts;
        const std::vector<std::pair<int, double>> blending = wheel.blends
            ? std::vector<std::pair<int, double>>{{blended.index, 1 - blended.fraction}, {blended.index + 1, blended.fraction}}
            : std::vector<std::pair<int, double>>{{1, 1}};
        for (const auto &[blendIndex, blendWeight] : blending) {
            const int base = section(blendIndex, wheel.index);
            for (const auto &[hue, hueWeight] : {std::pair{first, 1 - around}, std::pair{second, around}})
                for (const auto &[saturation, saturationWeight] : {std::pair{level.index, 1 - level.fraction}, std::pair{level.index + 1, level.fraction}})
                    parts.push_back({saturation == 0 ? nullptr : table(base + hue * 3 + saturation - 1), blendWeight * hueWeight * saturationWeight});
        }
        out.push_back(blend(parts));
    }
    return out;
}

std::vector<CameraRawStage> mixerStages(const CameraRawMixerSettings &mixer)
{
    std::vector<CameraRawStage> out;
    const std::array<const std::array<double, 8> *, 3> kinds{&mixer.luminance, &mixer.saturation, &mixer.hue};
    for (int kind = 0; kind < 3; ++kind)
        for (int color = 0; color < 8; ++color)
            if ((*kinds[size_t(kind)])[size_t(color)] != 0)
                out.push_back(fifty(mixerStart + (kind * 8 + color) * 5, (*kinds[size_t(kind)])[size_t(color)]));
    return out;
}

std::vector<float> compose(const std::vector<CameraRawStage> &stages)
{
    if (stages.empty())
        return {};
    std::vector<float> out;
    try {
        out.resize(size_t(grid) * grid * grid * 3);
    } catch (const std::bad_alloc &) {
        throw ExportError(ExportError::Kind::render);
    }
    camera_raw_compose(out.data(), grid, stages.data(), int(stages.size()), size);
    return out;
}

std::optional<CameraRawSettings::Balance> neutralize(double red, double green, double blue)
{
    if (!(std::min({red, green, blue}) > 1e-4))
        return std::nullopt;
    const auto cast = [&](double temperature, double tint) {
        const CameraRawStage stage = whiteBalance(temperature, tint);
        double color[3] = {red, green, blue};
        camera_raw_stage_color(&stage, size, color);
        return std::max({color[0], color[1], color[2]}) - std::min({color[0], color[1], color[2]});
    };
    CameraRawSettings::Balance best{0, 0};
    double bestCast = cast(0, 0);
    for (const auto &[step, reach] : {std::pair{10.0, 100.0}, {2.0, 10.0}, {0.5, 2.0}}) {
        const CameraRawSettings::Balance center = best;
        const int steps = int(std::lround(2 * reach / step));
        for (int i = 0; i <= steps; ++i) {
            const double temperature = center.temperature - reach + step * i;
            for (int j = 0; j <= steps; ++j) {
                const double tint = center.tint - reach + step * j;
                if (std::abs(temperature) > 100 || std::abs(tint) > 100)
                    continue;
                const double found = cast(temperature, tint);
                if (found < bestCast) {
                    best = {temperature, tint};
                    bestCast = found;
                }
            }
        }
    }
    return best;
}
}
