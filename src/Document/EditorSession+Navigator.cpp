#include "Document/EditorSession.h"
#include "Document/BrushStroke.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <QPainter>
#include <cmath>

// Swift's EditorSession+Navigator.swift: the document in small.
namespace {
// Every layer and placed mask scaled; blurs shrink with them.
CanvasDocument placed(const CanvasDocument &document, double x, double y, int width, int height)
{
    const auto scaled = [x, y](LayerTransform transform) {
        transform.origin = QPointF(transform.origin.x() * x, transform.origin.y() * y);
        transform.size = QSizeF(transform.size.width() * x, transform.size.height() * y);
        return transform;
    };
    CanvasDocument small = document;
    small.width = width;
    small.height = height;
    for (ImageLayer &layer : small.layers) {
        layer.transform = scaled(layer.transform);
        if (layer.mask && layer.mask->placement)
            layer.mask->placement = scaled(*layer.mask->placement);
        if (layer.adjustment && layer.adjustment->blurRadius)
            layer.adjustment->blurRadius = *layer.adjustment->blurRadius * x;
        if (layer.adjustment && layer.adjustment->motionDistance)
            layer.adjustment->motionDistance = *layer.adjustment->motionDistance * x;
    }
    return small;
}
}

// At the picture's size: adjustment surfaces take what they draw.
std::optional<QImage> EditorSession::navigatorImage(int maxSide) const
{
    if (!m_document || m_document->width <= 0 || m_document->height <= 0 || maxSide <= 0)
        return std::nullopt;
    const CanvasDocument &document = *m_document;
    const double scale = std::min(1.0, double(maxSide) / std::max(document.width, document.height));
    const int width = std::max(1, int(std::lround(document.width * scale))), height = std::max(1, int(std::lround(document.height * scale)));
    QImage context;
    try {
        context = BrushRaster::context(width, height, false);
    } catch (const ExportError &error) {
        qCWarning(lcRendering) << "the navigator's picture could not be made:" << error.what();
        return std::nullopt;
    }
    QPainter painter(&context);
    // A bare session: edits in progress stay out.
    EditorSession().drawLiveComposite(placed(document, double(width) / document.width, double(height) / document.height, width, height), painter);
    painter.end();
    return context;
}
