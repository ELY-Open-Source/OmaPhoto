#include "Document/ContentFill.h"
#include "Document/BrushStroke.h"
#include "Document/PixelAdjust.h"
#include "IO/ImageExporter.h"
#include "Rendering/PoolMap.h"
#include <numeric>
extern "C" {
#include "PatchPixels.h"
}

// The kernel's bands, where Swift's kernel calls dispatch_apply.
extern "C" void patch_bands(size_t bands, void (*band)(void *context, size_t index), void *context)
{
    std::vector<size_t> indices(bands);
    std::iota(indices.begin(), indices.end(), size_t(0));
    PoolMap::blocking(indices, [band, context](size_t index) { band(context, index); });
}

ContentFillError::ContentFillError(Kind kind)
    : std::runtime_error("Not enough unselected, opaque image pixels to synthesize a fill. Use a smaller selection with some surrounding image."), kind(kind)
{
}

QImage ContentFill::run(const FilterJob &job)
{
    if (!job.selection)
        throw ContentFillError(ContentFillError::Kind::noSource);
    const int width = job.image.width(), height = job.image.height();
    QImage pixels = BrushRaster::context(width, height, false);
    QPainter painter(&pixels);
    BrushRaster::draw(job.image, QRectF(0, 0, width, height), painter);
    painter.end();
    // White through the selection: the pixels to make.
    const QImage mask = PixelAdjust::coverage(*job.selection, width, height, job.mapping);
    // Multi-scale PatchMatch, as Spot Healing's Content-Aware mode.
    const int result = patch_fill(pixels.bits(), size_t(pixels.bytesPerLine()), mask.constBits(), size_t(mask.bytesPerLine()), width, height, job.seed);
    if (result == 0)
        throw ContentFillError(ContentFillError::Kind::noSource);
    if (result != 1)
        throw ExportError(ExportError::Kind::render);
    return pixels;
}
