#pragma once
#include "IO/ProjectStore.h"
#include <QByteArray>
#include <QImage>
#include <functional>
#include <optional>
#include <stdexcept>

class ExportError : public std::runtime_error {
public:
    enum class Kind { tooLarge, render, encode };
    explicit ExportError(Kind kind);
    const Kind kind;
};

// Swift's CancellationError: a newer request superseded this one.
class CancellationError : public std::exception {};

struct ExportRaster {
    QImage image;
    // Pixels per inch.
    double resolution = 72;
};

struct JPEGOptions {
    double quality = 0.85;
    // The matte behind transparent pixels.
    double red = 1;
    double green = 1;
    double blue = 1;
    bool operator==(const JPEGOptions &) const = default;
};

struct JPEGResult {
    QByteArray data;
    // Decoded from `data`, at most `previewLimit` pixels a side.
    QImage preview;
    static constexpr int previewLimit = 8192;
};

namespace ImageExporter {
ExportRaster render(const ProjectSnapshot &snapshot);
// Flattened on white, 1024 a side; none past 50 megapixels.
std::optional<QuickLookImages> quickLookImages(const ProjectSnapshot &snapshot);
QByteArray pngData(const ProjectSnapshot &snapshot);
QByteArray pngData(const ExportRaster &raster);
// Export As's size: resampled smooth, the resolution kept.
ExportRaster resized(const ExportRaster &raster, int width, int height);
// The picture over a solid colour, as pages show it.
QImage flattened(const ExportRaster &raster, const QColor &background);
// One page the printed size, the pixels lossless, alpha kept.
QByteArray pdfData(const ProjectSnapshot &snapshot);
// `background` fills the page under the picture; none stays clear.
QByteArray pdfData(const ExportRaster &raster, std::optional<QColor> background = std::nullopt);
void exportPDF(const ProjectSnapshot &snapshot, const QString &path);
// Stops at Swift's three points once `cancelled` says so.
JPEGResult jpeg(const ExportRaster &raster, const JPEGOptions &options, const std::function<bool()> &cancelled = {});
void exportPNG(const ProjectSnapshot &snapshot, const QString &path);
// Replaces the file in one step, or not at all.
void write(const QByteArray &data, const QString &path);
}
