#include "IO/ImageExporter.h"
#include "Document/DocumentLimits.h"
#include "Document/LayerEffects+Renderer.h"
#include "Document/LayerGroups.h"
#include "Document/LayerMask.h"
#include "Document/LiveLayerMask.h"
#include "IO/ImageImporter.h"
#include "Logging.h"
#include "Rendering/LayerRenderer.h"
#include "Rendering/LiveMaskRenderer.h"
#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace {
std::string description(ExportError::Kind kind)
{
    switch (kind) {
    case ExportError::Kind::tooLarge:
        return QStringLiteral("Image export supports canvases up to %1 megapixels and %2 pixels per side.").arg(DocumentLimits::maxSurfaceMegapixels()).arg(DocumentLimits::maxSideText()).toStdString();
    case ExportError::Kind::render:
        return "The canvas could not be rendered. Try a smaller canvas.";
    case ExportError::Kind::encode:
        return "The image could not be encoded.";
    }
    throw std::logic_error("unknown ExportError kind");
}

// Quality runs 0 to 100; -1 means the format's own.
QByteArray encode(QImage image, const char *format, int quality, double resolution)
{
    const int dotsPerMeter = qRound(resolution / 0.0254);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    image.setColorSpace(QColorSpace::SRgb);
    QByteArray data;
    QBuffer buffer(&data);
    QImageWriter writer(&buffer, format);
    writer.setQuality(quality);
    if (!writer.write(image))
        throw ExportError(ExportError::Kind::encode);
    return data;
}

QByteArray scaledJPEG(const QImage &image, double longSide)
try {
    const double scale = std::min(1.0, longSide / std::max(image.width(), image.height()));
    const QSize size(std::max(1, int(std::round(image.width() * scale))), std::max(1, int(std::round(image.height() * scale))));
    const QImage scaled = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage flattened(size, QImage::Format_RGB888);
    if (scaled.isNull() || flattened.isNull())
        throw ExportError(ExportError::Kind::render);
    flattened.fill(Qt::white);
    {
        QPainter painter(&flattened);
        painter.drawImage(QRectF(flattened.rect()), scaled);
    }
    return encode(flattened, "jpeg", 80, 72);
} catch (const std::bad_alloc &) {
    throw ExportError(ExportError::Kind::render);
}
}

ExportError::ExportError(Kind kind) : std::runtime_error(description(kind)), kind(kind) {}

ExportRaster ImageExporter::render(const ProjectSnapshot &snapshot)
try {
    const qint64 width = snapshot.manifest.width, height = snapshot.manifest.height;
    if (width < 1 || width > DocumentLimits::maxSide || height < 1 || height > DocumentLimits::maxSide || width * height > DocumentLimits::maxSurfacePixels)
        throw ExportError(ExportError::Kind::tooLarge);
    QImage image(int(width), int(height), QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    image.fill(0);
    const std::vector<ProjectLayerRecord> &layers = snapshot.manifest.layers;
    std::map<QUuid, ProjectLayerRecord> records;
    for (const ProjectLayerRecord &layer : layers) {
        if ((layer.imageFile && !snapshot.images.contains(layer.id)) || (layer.maskFile && !snapshot.masks.contains(layer.id)))
            throw ProjectError(ProjectError::Kind::missingImage);
        records.insert({layer.id, layer});
    }
    LiveMaskGraph::validate(layers);
    const auto parent = [&](QUuid id) { return records.contains(id) ? records.at(id).parentID : std::nullopt; };
    LiveMaskRenderer live([&](QUuid id) { return records.contains(id) ? records.at(id).maskSourceID : std::nullopt; },
                          [&](QUuid id, QPainter &target, const QImage &clip) {
                              const auto asset = snapshot.images.find(id);
                              if (!records.contains(id) || asset == snapshot.images.end())
                                  return;
                              const ProjectLayerRecord &layer = records.at(id);
                              const QImage pixels = asset->second.image();
                              const std::optional<LayerMask> mask = snapshot.mask(layer);
                              const std::optional<QImage> shown =
                                  mask ? mask->clipImage(mask->placement, layer.transform, pixels.width(), pixels.height()) : std::nullopt;
                              const double opacity = layer.effectiveOpacity(records);
                              const LayerBlendMode mode = layer.blendMode.value_or(LayerBlendMode::normal);
                              // With effects the mask is in their image already.
                              if (const auto effects = LayerEffectsRenderer::cached(pixels, shown, layer.effects)) {
                                  const LayerTransform grown = LayerEffectsRenderer::placed(layer.transform, effects->image, effects->inset);
                                  LayerRenderer::draw(effects->image, grown, grown.center(), target, {.opacity = opacity, .blendMode = mode, .clip = clip});
                                  return;
                              }
                              LayerRenderer::draw(pixels, layer.transform, layer.transform.center(), target,
                                                  {.opacity = opacity, .blendMode = mode, .mask = shown.value_or(QImage()), .clip = clip});
                          });
    live.adjustment = [&](QUuid id) { return records.contains(id) ? records.at(id).adjustment : std::nullopt; };
    live.adjustmentOpacity = [&](QUuid id) { return records.at(id).effectiveOpacity(records); };
    live.adjustmentClip = [&](QUuid id, const QPainter &context, QImage &coverage) {
        const ProjectLayerRecord &layer = records.at(id);
        const std::optional<LayerMask> mask = snapshot.mask(layer);
        if (const std::optional<QImage> image = mask ? mask->enabledImage() : std::nullopt)
            FolderMaskClip{*image, layer.transform}.apply(layer.transform.center(), context, coverage);
    };
    std::vector<QUuid> visible;
    for (const ProjectLayerRecord &layer : LayerHierarchy::visibleLayers(layers))
        visible.push_back(layer.id);
    live.prepareStacks(visible, parent, [&](QUuid id) { return records.at(id).blendMode.value_or(LayerBlendMode::normal); });
    {
        QPainter painter(&image);
        FolderMaskClip::draw(visible, parent, [&](QUuid id) -> std::optional<FolderMaskClip::Applier> {
            const std::optional<LayerMask> mask = records.contains(id) ? snapshot.mask(records.at(id)) : std::nullopt;
            const std::optional<QImage> enabled = mask ? mask->enabledImage() : std::nullopt;
            if (!enabled)
                return std::nullopt;
            const FolderMaskClip clip{*enabled, records.at(id).transform};
            return [clip](const QPainter &context, QImage &coverage) { clip.apply(clip.transform.center(), context, coverage); };
        }, painter, [&](QUuid id, const QImage &clip) { live.drawComposite(id, painter, clip); });
    }
    qCInfo(lcIO) << "rendered" << width << "x" << height << "from" << visible.size() << "visible layers";
    return {image, snapshot.manifest.resolution.value_or(72)};
} catch (const std::bad_alloc &) {
    // Memory that runs out fails the render, as contexts do.
    throw ExportError(ExportError::Kind::render);
}

std::optional<QuickLookImages> ImageExporter::quickLookImages(const ProjectSnapshot &snapshot)
{
    if (snapshot.manifest.width * snapshot.manifest.height > 50'000'000) {
        qCInfo(lcIO) << "no Quick Look preview for" << snapshot.manifest.width << "x" << snapshot.manifest.height;
        return std::nullopt;
    }
    try {
        return QuickLookImages{scaledJPEG(render(snapshot).image, 1024)};
    } catch (const std::runtime_error &error) {
        // Swift's try? saves without one; the log says why.
        qCWarning(lcIO) << "no Quick Look preview:" << error.what();
        return std::nullopt;
    }
}

QByteArray ImageExporter::pngData(const ProjectSnapshot &snapshot)
{
    return pngData(render(snapshot));
}

QByteArray ImageExporter::pngData(const ExportRaster &raster)
{
    return encode(raster.image, "png", -1, raster.resolution);
}

ExportRaster ImageExporter::resized(const ExportRaster &raster, int width, int height)
try {
    if (width == raster.image.width() && height == raster.image.height())
        return raster;
    if (width < 1 || width > DocumentLimits::maxSide || height < 1 || height > DocumentLimits::maxSide
        || qint64(width) * height > DocumentLimits::maxSurfacePixels)
        throw ExportError(ExportError::Kind::tooLarge);
    // CoreGraphics' high quality: an area average shrinking.
    const QImage image = raster.image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (image.isNull())
        throw ExportError(ExportError::Kind::render);
    return {image, raster.resolution};
} catch (const std::bad_alloc &) {
    throw ExportError(ExportError::Kind::render);
}

QImage ImageExporter::flattened(const ExportRaster &raster, const QColor &background)
try {
    QImage flat(raster.image.size(), QImage::Format_RGB888);
    if (flat.isNull())
        throw ExportError(ExportError::Kind::render);
    flat.fill(background);
    QPainter painter(&flat);
    painter.drawImage(QRectF(flat.rect()), raster.image);
    return flat;
} catch (const std::bad_alloc &) {
    throw ExportError(ExportError::Kind::render);
}

JPEGResult ImageExporter::jpeg(const ExportRaster &raster, const JPEGOptions &options, const std::function<bool()> &cancelled)
try {
    const auto check = [&cancelled] {
        if (cancelled && cancelled())
            throw CancellationError();
    };
    check();
    // JPEG has no alpha: the image lies over the matte.
    QImage flattened(raster.image.size(), QImage::Format_RGB888);
    if (flattened.isNull())
        throw ExportError(ExportError::Kind::render);
    flattened.fill(QColor::fromRgbF(float(options.red), float(options.green), float(options.blue)));
    {
        QPainter painter(&flattened);
        painter.drawImage(QRectF(flattened.rect()), raster.image);
    }
    check();
    QByteArray data = encode(flattened, "jpeg", qRound(std::clamp(options.quality, 0.0, 1.0) * 100), raster.resolution);
    check();
    // The preview shows what the file holds, compression and all.
    ImageImporter::liftAllocationLimit();
    QBuffer buffer(&data);
    QImageReader reader(&buffer, "jpeg");
    const QSize size = reader.size();
    // Full size for the 100% view, capped; one pixel across.
    if (std::max(size.width(), size.height()) > JPEGResult::previewLimit)
        reader.setScaledSize(size.scaled(JPEGResult::previewLimit, JPEGResult::previewLimit, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
    const QImage preview = reader.read();
    if (preview.isNull())
        throw ExportError(ExportError::Kind::encode);
    return {data, preview};
} catch (const std::bad_alloc &) {
    throw ExportError(ExportError::Kind::render);
}

QByteArray ImageExporter::pdfData(const ProjectSnapshot &snapshot)
{
    return pdfData(render(snapshot));
}

// Swift's Core Graphics PDF context: one page, one Flate image.
QByteArray ImageExporter::pdfData(const ExportRaster &raster, std::optional<QColor> background)
try {
    const QImage straight = raster.image.convertToFormat(QImage::Format_RGBA8888);
    if (straight.isNull())
        throw ExportError(ExportError::Kind::render);
    const int width = straight.width(), height = straight.height();
    QByteArray colour, alpha;
    colour.reserve(qsizetype(width) * height * 3);
    alpha.reserve(qsizetype(width) * height);
    for (int y = 0; y < height; ++y) {
        const uchar *row = straight.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            colour.append(reinterpret_cast<const char *>(row + x * 4), 3);
            alpha.append(char(row[x * 4 + 3]));
        }
    }
    // Pixels at the document's resolution: points are 1/72 inch.
    const double points = 72 / (raster.resolution > 0 ? raster.resolution : 72);
    const auto number = [](double value) { return QByteArray::number(value, 'f', 4); };
    const QByteArray pageWidth = number(width * points), pageHeight = number(height * points);
    QByteArray content = "q\n";
    if (background)
        content += number(background->redF()) + ' ' + number(background->greenF()) + ' ' + number(background->blueF()) + " rg 0 0 " + pageWidth + ' '
            + pageHeight + " re f\n";
    content += pageWidth + " 0 0 " + pageHeight + " 0 0 cm /Im0 Do\nQ";
    // zlib's stream, without Qt's four-byte length.
    const auto stream = [](const QByteArray &dictionary, const QByteArray &bytes, bool deflate) {
        const QByteArray compressed = deflate ? qCompress(bytes, 9) : QByteArray();
        // qCompress says no memory with an empty array.
        if (deflate && compressed.isEmpty())
            throw ExportError(ExportError::Kind::render);
        const QByteArray body = deflate ? compressed.mid(4) : bytes;
        return "<< " + dictionary + (deflate ? " /Filter /FlateDecode" : "") + " /Length " + QByteArray::number(body.size()) + " >>\nstream\n" + body
            + "\nendstream";
    };
    const QByteArray image = "/Type /XObject /Subtype /Image /Width " + QByteArray::number(width) + " /Height " + QByteArray::number(height)
        + " /BitsPerComponent 8";
    const std::vector<QByteArray> objects{"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                          "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + pageWidth + ' ' + pageHeight
                                              + "] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>",
                                          stream("", content, false), stream(image + " /ColorSpace /DeviceRGB /SMask 6 0 R", colour, true),
                                          stream(image + " /ColorSpace /DeviceGray", alpha, true)};
    QByteArray pdf = "%PDF-1.4\n";
    std::vector<qsizetype> offsets;
    for (size_t index = 0; index < objects.size(); ++index) {
        offsets.push_back(pdf.size());
        pdf += QByteArray::number(qsizetype(index + 1)) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const qsizetype table = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(qsizetype(objects.size() + 1)) + "\n0000000000 65535 f \n";
    for (const qsizetype offset : offsets)
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(qsizetype(objects.size() + 1)) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(table) + "\n%%EOF\n";
    return pdf;
} catch (const std::bad_alloc &) {
    throw ExportError(ExportError::Kind::render);
}

void ImageExporter::exportPDF(const ProjectSnapshot &snapshot, const QString &path)
{
    write(pdfData(snapshot), path);
}

void ImageExporter::exportPNG(const ProjectSnapshot &snapshot, const QString &path)
{
    write(pngData(snapshot), path);
}

void ImageExporter::write(const QByteArray &data, const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        qCWarning(lcIO) << "could not write" << path << ":" << file.errorString();
        throw std::runtime_error(file.errorString().toStdString());
    }
    qCInfo(lcIO) << "wrote" << data.size() << "bytes to" << path;
}

