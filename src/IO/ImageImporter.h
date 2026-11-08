#pragma once
#include "Document/DocumentLimits.h"
#include <QImage>
#include <QString>
#include <QUuid>
#include <compare>
#include <map>
#include <memory>
#include <stdexcept>

class RasterSnapshot;
struct PSDDocument;

// Which pixels an asset holds, as CGImage identity did.
struct ImageIdentity {
    const RasterSnapshot *raster = nullptr;
    qint64 cacheKey = 0;
    auto operator<=>(const ImageIdentity &) const = default;
};

class ImportedImage {
public:
    ImportedImage(QImage image, QImage thumbnail, QString name);
    ImportedImage(std::shared_ptr<const RasterSnapshot> raster, QImage thumbnail, QString name);

    QSize size() const;
    // Painted tiles flatten on first use.
    QImage image() const;
    ImageIdentity identity() const;
    qint64 byteCount() const;

    QImage thumbnail;
    QString name;
    std::shared_ptr<const RasterSnapshot> raster;

private:
    QImage m_image;
};

class ImageImportError : public std::runtime_error {
public:
    // whiteBalance is Linux's: LibRaw cannot take every white.
    enum class Kind { unreadable, unsupported, tooLarge, whiteBalance };
    explicit ImageImportError(Kind kind);
    Kind kind;
};

namespace ImageImporter {
// Qt refuses decodes over 128 MB; the app's budgets rule.
void liftAllocationLimit();
// JPEG, PNG, TIFF, WebP, HEIC, a layerless PSD: upright sRGB.
ImportedImage decode(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget(), bool flattenedPhotoshop = false);
PSDDocument loadPhotoshop(const QString &path, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
std::map<QUuid, ImportedImage> photoshopAssets(const PSDDocument &document);
// Swift's `UTType.svg` by suffix: `svg` and `svgz`.
bool isSVG(const QString &path);
// Drawn once: fitted to the canvas, else its declared size.
ImportedImage decodeSVG(const QString &path, std::optional<QSizeF> fitting, qint64 remainingPixels = DocumentLimits::documentPixelBudget());
// Swift's `UTType.importableImages`, as name filters.
QStringList importableGlobs();
}

// A folder brought in as the images directly inside it.
namespace ImageFolder {
// A plain folder: no link, no project.
bool matches(const QString &path);
// In the file manager's name order: "2" before "10".
QStringList images(const QString &folder);
}
