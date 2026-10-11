#include "Document/CameraRawTables.h"

#include "IO/ImageExporter.h"
#include "Logging.h"

#include <QFile>
#include <algorithm>
#include <stdexcept>
#include <zlib.h>

namespace CameraRawTables {
namespace {
constexpr size_t entries = size_t(size) * size * size * 3;
// White balance 9 × 9, then the six other sliders.
constexpr int exposureStart = 81, contrastStart = 102, whitesStart = 111, blacksStart = 120;
constexpr int saturationStart = 129, vibranceStart = 138, count = 147;

// Raw DEFLATE, as Foundation's `.zlib` writes it.
std::vector<uint8_t> inflated(const QByteArray &packed, size_t expected)
{
    std::vector<uint8_t> out(expected);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        throw std::logic_error("Camera Raw's tables: inflate would not start");
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed.constData()));
    stream.avail_in = uInt(packed.size());
    stream.next_out = out.data();
    stream.avail_out = uInt(out.size());
    const int status = inflate(&stream, Z_FINISH);
    const size_t made = stream.total_out;
    inflateEnd(&stream);
    if (status != Z_STREAM_END || made != expected)
        throw std::logic_error("Camera Raw's tables do not inflate to their size");
    return out;
}

// Each entry is a running difference from no change.
std::vector<uint8_t> load()
{
    QFile file(QStringLiteral(":/CameraRawTables.bin"));
    if (!file.open(QIODevice::ReadOnly))
        throw std::logic_error("Camera Raw's tables are missing from the build");
    const QByteArray bytes = file.readAll();
    const auto byte = [&](int at) { return int(uchar(bytes[at])); };
    if (bytes.size() <= 8 || !bytes.startsWith("CRT2") || (byte(4) | byte(5) << 8) != size || (byte(6) | byte(7) << 8) != count)
        throw std::logic_error("Camera Raw's tables have the wrong header");
    const std::vector<uint8_t> planes = inflated(bytes.mid(8), count * entries);
    std::vector<uint8_t> out(count * entries);
    const size_t cells = size_t(size) * size * size;
    uint8_t level[size];
    for (int i = 0; i < size; ++i)
        level[i] = uint8_t(std::lround(i * 255.0 / (size - 1)));
    for (size_t table = 0; table < count; ++table) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const size_t base = (table * 3 + channel) * cells;
            uint8_t running = 0;
            for (size_t cell = 0; cell < cells; ++cell) {
                running = uint8_t(running + planes[base + cell]);
                const size_t axis = channel == 0 ? cell / (size * size) : channel == 1 ? (cell / size) % size : cell % size;
                out[table * entries + cell * 3 + channel] = uint8_t(running + level[axis]);
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

// The two tables beside `value`, and how far between.
Between between(double value, double low, double step, int tables)
{
    const double position = std::min(double(tables - 1), std::max(0.0, (value - low) / step));
    const int index = std::min(tables - 2, int(position));
    return {index, position - index};
}

CameraRawStage stage(int start, double value, double low, double step, int tables)
{
    const Between at = between(value, low, step, tables);
    return CameraRawStage{{table(start + at.index), table(start + at.index + 1), nullptr, nullptr},
                          {float(1 - at.fraction), float(at.fraction), 0, 0}};
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
    return CameraRawStage{{table(row.index * 9 + column.index), table(row.index * 9 + column.index + 1), table((row.index + 1) * 9 + column.index),
                           table((row.index + 1) * 9 + column.index + 1)},
                          {float((1 - down) * (1 - across)), float((1 - down) * across), float(down * (1 - across)), float(down * across)}};
}

Stages stages(const CameraRawSettings &settings)
{
    Stages out;
    if (settings.exposure != 0)
        out.before.push_back(stage(exposureStart, settings.exposure, -5, 0.5, 21));
    if (settings.temperature != 0 || settings.tint != 0)
        out.before.push_back(whiteBalance(settings.temperature, settings.tint));
    if (settings.contrast != 0)
        out.before.push_back(stage(contrastStart, settings.contrast, -100, 25, 9));
    if (settings.whites != 0)
        out.after.push_back(stage(whitesStart, settings.whites, -100, 25, 9));
    if (settings.blacks != 0)
        out.after.push_back(stage(blacksStart, settings.blacks, -100, 25, 9));
    if (settings.saturation != 0)
        out.after.push_back(stage(saturationStart, settings.saturation, -100, 25, 9));
    if (settings.vibrance != 0)
        out.after.push_back(stage(vibranceStart, settings.vibrance, -100, 25, 9));
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
